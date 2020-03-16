#include "resolver_impl.h"
#include "api_config.h"
#include "resolve_attempt_udp.h"
#include "socket_utils.h"
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/placeholders.hpp>
#include <boost/bind.hpp>
#include <boost/thread/thread_only.hpp>
#include <memory>


// === implementation of the resolver_impl class ===

using namespace lsl;
using namespace lslboost::asio;
using err_t = const lslboost::system::error_code &;

resolver_impl::resolver_impl()
	: cfg_(api_config::get_instance()), cancelled_(false), expired_(false), forget_after_(FOREVER),
	  fast_mode_(true), io_(std::make_shared<io_context>()), resolve_timeout_expired_(*io_),
	  wave_timer_(*io_), unicast_timer_(*io_) {
	// parse the multicast addresses into endpoints and store them
	std::vector<std::string> mcast_addrs = cfg_->multicast_addresses();
	uint16_t mcast_port = cfg_->multicast_port();
	for (const auto &mcast_addr : mcast_addrs) {
		try {
			mcast_endpoints_.emplace_back(ip::make_address(mcast_addr), (uint16_t)mcast_port);
		} catch (std::exception &) {}
	}

	// parse the per-host addresses into endpoints, and store them, too
	std::vector<std::string> peers = cfg_->known_peers();
	udp::resolver udp_resolver(*io_);
	// for each known peer...
	for (const auto &peer : peers) {
		try {
			// resolve the name
			// for each endpoint...
			for (auto &res : udp_resolver.resolve(peer, std::to_string(cfg_->base_port()))) {
				// for each port in the range...
				for (int p = cfg_->base_port(); p < cfg_->base_port() + cfg_->port_range(); p++)
					// add a record
					ucast_endpoints_.emplace_back(res.endpoint().address(), p);
			}
		} catch (std::exception &) {}
	}

	// generate the list of protocols to use
	if (cfg_->allow_ipv6()) {
		udp_protocols_.push_back(udp::v6());
		tcp_protocols_.push_back(tcp::v6());
	}
	if (cfg_->allow_ipv4()) {
		udp_protocols_.push_back(udp::v4());
		tcp_protocols_.push_back(tcp::v4());
	}
}

void check_query(const std::string &query) {
	try {
		pugi::xpath_query(query.c_str());
	} catch (std::exception &e) {
		throw std::invalid_argument((("Invalid query '" + query) += "': ") += e.what());
	}
}

std::string resolver_impl::build_query(const char *pred_or_prop, const char *value) {
	std::string query("session_id='");
	query += api_config::get_instance()->session_id();
	query += '\'';
	if (pred_or_prop) (query += " and ") += pred_or_prop;
	if (value) ((query += "='") += value) += '\'';
	return query;
}

resolver_impl *resolver_impl::create_resolver(
	double forget_after, const char *pred_or_prop, const char *value) noexcept {
	try {
		auto *resolver = new resolver_impl();
		resolver->resolve_continuous(build_query(pred_or_prop, value), forget_after);
		return resolver;
	} catch (std::exception &e) {
		LOG_F(ERROR, "Error while creating a continuous_resolver: %s", e.what());
		return nullptr;
	}
}

// === resolve functions ===

std::vector<stream_info_impl> resolver_impl::resolve_oneshot(
	const std::string &query, int minimum, double timeout, double minimum_time) {
	check_query(query);
	// reset the IO service & set up the query parameters
	io_->restart();
	query_ = query;
	minimum_ = minimum;
	wait_until_ = lsl_clock() + minimum_time;
	results_.clear();
	forget_after_ = FOREVER;
	fast_mode_ = true;
	expired_ = false;

	// start a timer that cancels all outstanding IO operations and wave schedules after the timeout
	// has expired
	if (timeout != FOREVER) {
		resolve_timeout_expired_.expires_after(timeout_sec(timeout));
		resolve_timeout_expired_.async_wait([this](err_t err) {
			if (err != error::operation_aborted) cancel_ongoing_resolve();
		});
	}

	// start the first wave of resolve packets
	next_resolve_wave();

	// run the IO operations until finished
	if (!cancelled_) {
		io_->run();
		// collect output
		std::vector<stream_info_impl> output;
		for (auto &result : results_) output.push_back(result.second.first);
		return output;
	} else
		return std::vector<stream_info_impl>();
}

void resolver_impl::resolve_continuous(const std::string &query, double forget_after) {
	check_query(query);
	// reset the IO service & set up the query parameters
	io_->restart();
	query_ = query;
	minimum_ = 0;
	wait_until_ = 0;
	results_.clear();
	forget_after_ = forget_after;
	fast_mode_ = false;
	expired_ = false;
	// start a wave of resolve packets
	next_resolve_wave();
	// spawn a thread that runs the IO operations
	background_io_.reset(new lslboost::thread(lslboost::bind(&io_context::run, io_)));
}

std::vector<stream_info_impl> resolver_impl::results(uint32_t max_results) {
	std::vector<stream_info_impl> output;
	lslboost::lock_guard<lslboost::mutex> lock(results_mut_);
	double expired_before = lsl_clock() - forget_after_;

	for (auto it = results_.begin(); it != results_.end();) {
		if (it->second.second < expired_before)
			it = results_.erase(it);
		else {
			if (output.size() < max_results) output.push_back(it->second.first);
			it++;
		}
	}
	return output;
}

// === timer-driven async handlers ===

void resolver_impl::next_resolve_wave() {
	std::size_t num_results = 0;
	{
		lslboost::lock_guard<lslboost::mutex> lock(results_mut_);
		num_results = results_.size();
	}
	if (cancelled_ || expired_ ||
		(minimum_ && (num_results >= (std::size_t)minimum_) && lsl_clock() >= wait_until_)) {
		// stopping criteria satisfied: cancel the ongoing operations
		cancel_ongoing_resolve();
	} else {
		// start a new multicast wave
		udp_multicast_burst();

		auto wave_timer_timeout =
			(fast_mode_ ? 0 : cfg_->continuous_resolve_interval()) + cfg_->multicast_min_rtt();
		if (!ucast_endpoints_.empty()) {
			// we have known peer addresses: we spawn a unicast wave
			unicast_timer_.expires_after(timeout_sec(cfg_->multicast_min_rtt()));
			unicast_timer_.async_wait([this](err_t ec) { this->udp_unicast_burst(ec); });
			// delay the next multicast wave
			wave_timer_timeout += cfg_->unicast_min_rtt();
		}
		wave_timer_.expires_after(timeout_sec(wave_timer_timeout));
		wave_timer_.async_wait([this](err_t err) {
			if (err != error::operation_aborted) next_resolve_wave();
		});
	}
}

void resolver_impl::udp_multicast_burst() {
	// start one per IP stack under consideration
	for (std::size_t k = 0, failures = 0; k < udp_protocols_.size(); k++) {
		try {
			resolve_attempt_udp_p attempt(new resolve_attempt_udp(*io_, udp_protocols_[k],
				mcast_endpoints_, query_, results_, results_mut_, cfg_->multicast_max_rtt(), this));
			attempt->begin();
		} catch (std::exception &e) {
			if (++failures == udp_protocols_.size())
				LOG_F(ERROR,
					"Could not start a multicast resolve attempt for any of the allowed "
					"protocol stacks: %s",
					e.what());
		}
	}
}

void resolver_impl::udp_unicast_burst(error_code err) {
	if (err != error::operation_aborted) {
		// start one per IP stack under consideration
		for (std::size_t k = 0, failures = 0; k < udp_protocols_.size(); k++) {
			try {
				resolve_attempt_udp_p attempt(
					new resolve_attempt_udp(*io_, udp_protocols_[k], ucast_endpoints_, query_,
						results_, results_mut_, cfg_->unicast_max_rtt(), this));
				attempt->begin();
			} catch (std::exception &e) {
				if (++failures == udp_protocols_.size())
					LOG_F(WARNING,
						"Could not start a unicast resolve attempt for any of the allowed protocol "
						"stacks: %s",
						e.what());
			}
		}
	}
}

// === cancellation and teardown ===

void resolver_impl::cancel() {
	cancelled_ = true;
	cancel_ongoing_resolve();
}

void resolver_impl::cancel_ongoing_resolve() {
	// make sure that ongoing handler loops terminate
	expired_ = true;
	// timer fires: cancel the next wave schedule
	post(*io_, [this]() { wave_timer_.cancel(); });
	post(*io_, [this]() { unicast_timer_.cancel(); });
	// and cancel the timeout, too
	post(*io_, [this]() { resolve_timeout_expired_.cancel(); });
	// cancel all currently active resolve attempts
	cancel_all_registered();
}

resolver_impl::~resolver_impl() {
	try {
		if (background_io_) {
			cancel();
			background_io_->join();
		}
	} catch (std::exception &e) {
		LOG_F(WARNING, "Error during destruction of a resolver_impl: %s", e.what());
	} catch (...) { LOG_F(ERROR, "Severe error during destruction of a resolver_impl."); }
}
