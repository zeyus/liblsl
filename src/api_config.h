#ifndef API_CONFIG_H
#define API_CONFIG_H

#include "netinterfaces.h"
#include "util/inireader.hpp"
#include <cstdint>
#include <loguru.hpp>
#include <string>
#include <vector>

namespace ip = asio::ip;

namespace lsl {
/**
 * A configuration object: holds all the configurable settings of liblsl.
 * These settings can be set via a configuration file that is automatically searched
 * by stream providers and recipients in a series of locations:
 *  - First, the content set via `lsl_set_config_content()`
 *  - Second, the file set via `lsl_set_config_filename()`
 *  - Third, the file `lsl_api.cfg` in the current working directory
 *  - Fourth, the file `lsl_api.cfg` in the home directory (e.g., `~/lsl_api/lsl_api.cfg`)
 *  - Fifth, the file `lsl_api.cfg` in the system configuration directory (e.g., `/etc/lsl_api/lsl_api.cfg`)
 *
 * Note that, while in some cases it might seem sufficient to override configurations
 * only for a subset of machines involved in a recording session (e.g., the servers),
 * it is recommended that the same settings are used by all machines (stream recipients
 * and providers) to avoid subtle bugs.
 */
class api_config {
public:
	///  Get a pointer to this singleton.
	static const api_config *get_instance();

	// === core parameters ===

	/**
	 * Lowest port used to provide data streams & service information.
	 *
	 * Up to port_range successively higher port numbers may be utilized,
	 * depending on how many streams are being served on one machine.
	 * If an outlet shall be reachable from outside a firewall, all TCP/UDP
	 * ports starting from `base_port` up to `base_port+port_range-1`, as well as
	 * the multicast_port should be open. If an inlet is behind a firewall,
	 * the UDP ports starting from base_port up to base_port+port_range-1 should
	 * be opened in order to allow for return packets in response to stream
	 * discovery queries.
	 */
	uint16_t base_port() const { return base_port_; }

	/** Number of ports available on a machine for serving streams.
	 *
	 * This is the number of ports, starting from the base_port that can be allocated for
	 * serving streams. This limits the number of outlets that can coexist on a single machine
	 * to port_range; by increasing this number this limit can be expanded.
	 */
	uint16_t port_range() const { return port_range_; }

	/**
	 * Whether to allow binding to a randomly assigned port.
	 *
	 * This can be used when the regular port range has been exhausted.
	 */
	int allow_random_ports() const { return allow_random_ports_; }

	/**
	 * Port over which multi-cast communication is handled.
	 * This is the communication medium for the announcement and discovery of streams
	 * between inlets and outlets. Note that according to the router configuration some
	 * multicast address ranges or ports may be blocked.
	 */
	uint16_t multicast_port() const { return multicast_port_; }

	/**
	 * @brief How the IPv4 / IPv6 protocols should be handled.
	 *
	 * The option "ports.IPv6" can be "disable" (use only IPv4), "force" (use only IPv6),
	 * or "allow" (use both protocol stacks).
	 */
	bool allow_ipv6() const { return allow_ipv6_; }
	bool allow_ipv4() const { return allow_ipv4_; }



	/**
	* @brief Set the configuration directly from a string.
	* 
	* This allows passing in configuration content directly rather than from a file.
	* This MUST be called before the first call to get_instance() to have any effect.
	*/
    static void set_api_config_content(const std::string &content) {
        api_config_content_ = content;
    }

	/**
	 * @brief An additional settings path to load configuration from.
	 */
	const std::string &api_config_filename() const { return api_config_filename_; }

	/**
	 * @brief Set the config file name used to load the settings.
	 * 
	 * This MUST be called before the first call to get_instance() to have any effect.
	 */
	static void set_api_config_filename(const std::string &filename) {
		api_config_filename_ = filename;
	}


	/**
	 * @brief The range or scope of stream lookup when using multicast-based discovery
	 *
	 * determines the output of the member functions multicast_addresses() and multicast_ttl().
	 * Can take the values "machine", "link", "site", "organization", or "global".
	 */
	const std::string &resolve_scope() const { return resolve_scope_; }

	/**
	 * List of multicast addresses on which inlets / outlets advertise/discover streams.
	 *
	 * This is merged from several other config file entries
	 * (LocalAddresses,SiteAddresses,OrganizationAddresses, GlobalAddresses)
	 * goverened according to the ResolveScope setting.
	 * Each participant in the network is aware of all addresses in this list, and will try all
	 * of them if necessary.
	 * For smooth operation this list should ideally include both IPv4 and IPv6 addresses to
	 * work on networks on which one of the two is disabled.
	 * Specifically, the list should contain both the broadcast address
	 * 255.255.255.255 and link-local multicast addresses.
	 * To communicate across routers within a site (depending on local policy, e.g., the
	 * department) or organization (e.g., the campus), or at larger scope, multicast addresses
	 * with the according scope need to be included.
	 */
	const std::vector<ip::address> &multicast_addresses() const { return multicast_addresses_; }

	/**
	 * @brief The address of the local interface on which to listen to multicast traffic.
	 *
	 * The default is an empty string, i.e. bind to the default interface(s).
	 */
	const std::string &listen_address() const { return listen_address_; }

	/**
	 * A list of local interface addresses the multicast packets should be
	 * sent from.
	 *
	 * The ini file may contain IPv4 addresses and/or IPv6 addresses with the
	 * interface index as scope id, e.g. `1234:5678::2%3`
	 **/
	std::vector<lsl::netif> multicast_interfaces;

	/**
	 * The TTL setting (time-to-live) for the multicast packets.
	 * This is determined according to the ResolveScope setting if not overridden by the TTLOverride
	 * setting. The higher this number (0-255), the broader their distribution. Routers (if
	 * correctly configured) employ various thresholds below which packets are not further
	 * forwarded. These are: 0: Restricted to the same host -- not forwarded by a network card. 1:
	 * Restricted to the same subnet -- not forwarded by a router. 32: Restricted to the same site,
	 * organization or department. 64: Restricted to the same region (definition of region varies).
	 * 128: Restricted to the same continent.
	 * 255: Not restricted in scope (global).
	 */
	int multicast_ttl() const { return multicast_ttl_; }

	/**
	 * @brief The configured session ID.
	 * Allows to keep recording operations isolated from each other (precluding unwanted
	 * interference).
	 */
	const std::string &session_id() const { return session_id_; }

	/**
	 * @brief List of known host names that may provide LSL streams.
	 * Can serve as a fallback if multicast/broadcast communication fails on a given network.
	 */
	const std::vector<std::string> &known_peers() const { return known_peers_; }

	// === tuning parameters ===

	/// The network protocol version to use.
	int use_protocol_version() const { return use_protocol_version_; }
	/// The interval at which the watchdog checks if connections are still fine.
	double watchdog_check_interval() const { return watchdog_check_interval_; }
	/// The watchdog takes no action if not at least this much time has passed since the last
	/// receipt of data. In seconds.
	double watchdog_time_threshold() const { return watchdog_time_threshold_; }
	/// The minimum assumed round-trip-time for a multicast query. Any subsequent packet wave would
	/// be started no earlier than this.
	double multicast_min_rtt() const { return multicast_min_rtt_; }
	/// The maximum assumed round-trip-time for a multicast query. We will stop waiting for return
	/// packets for a wave after this time.
	double multicast_max_rtt() const { return multicast_max_rtt_; }
	/// The minimum assumed round-trip-time for a multi-peer/multi-port unicast query. Any
	/// subsequent packet wave would be started no earlier than this.
	double unicast_min_rtt() const { return unicast_min_rtt_; }
	/// The maximum assumed round-trip-time for a multi-peer/multi-port unicast query.
	double unicast_max_rtt() const { return unicast_max_rtt_; }
	/// The interval at which resolve queries are emitted for continuous/background resolve
	/// activities. This is in addition to the assumed RTT's.
	double continuous_resolve_interval() const { return continuous_resolve_interval_; }
	/// Desired timer resolution in ms (0 means no change). Currently only affects Windows operating
	/// systems, where values other than 1 can increase LSL transmission latency.
	int timer_resolution() const { return timer_resolution_; }
	/// The maximum number of most-recently-used queries that is cached.
	int max_cached_queries() const { return max_cached_queries_; }
	/// Interval between background time correction updates.
	double time_update_interval() const { return time_update_interval_; }
	/// Minimum number of probes that must have been successful to perform a time update.
	int time_update_minprobes() const { return time_update_minprobes_; }
	/// Number of time probes that are being sent for a single update.
	int time_probe_count() const { return time_probe_count_; }
	/// Interval between the individual time probes that are sent to calculate an update.
	double time_probe_interval() const { return time_probe_interval_; }
	/// Maximum assumed RTT of a time probe (= extra waiting time).
	double time_probe_max_rtt() const { return time_probe_max_rtt_; }
	/// Default pre-allocated buffer size for the outlet, in ms (regular streams).
	int outlet_buffer_reserve_ms() const { return outlet_buffer_reserve_ms_; }
	/// Default pre-allocated buffer size for the outlet, in samples (irregular streams).
	int outlet_buffer_reserve_samples() const { return outlet_buffer_reserve_samples_; }
	/// Default socket send buffer size, in bytes.
	int socket_send_buffer_size() const { return socket_send_buffer_size_; }
	/// Default pre-allocated buffer size for the inlet, in ms (regular streams).
	int inlet_buffer_reserve_ms() const { return inlet_buffer_reserve_ms_; }
	/// Default pre-allocated buffer size for the inlet, in samples (irregular streams).
	int inlet_buffer_reserve_samples() const { return inlet_buffer_reserve_samples_; }
	/// Default socket receive buffer size, in bytes.
	int socket_receive_buffer_size() const { return socket_receive_buffer_size_; }
	/// Default halftime of the time-stamp smoothing window (if enabled), in seconds.
	float smoothing_halftime() const { return smoothing_halftime_; }
	/// Override timestamps with lsl clock if True
	bool force_default_timestamps() const { return force_default_timestamps_; }

	/// Deleted copy constructor (noncopyable).
	api_config(const api_config &rhs) = delete;

	/// Deleted assignment operator (noncopyable).
	api_config &operator=(const api_config &rhs) = delete;

private:
	/// Get the api_config singleton after thread-safe initialization if needed
	static api_config *get_instance_internal();

	/**
	 * Constructor.
	 * Applies default settings and overrides them based on a config file (if present).
	 */
	api_config();

	/**
	 * @brief Load a configuration file (or use defaults if a filename is empty).
	 * @param filename Platform-native config file name
	 */
	void load_from_file(const std::string &filename = std::string());

	/**
	 * @brief Load a configuration from a string.
	 * @param content The configuration content to parse
	 */
	void load_from_content(const std::string &content);

	/**
	 * @brief Load the configuration from an INI object.
	 * @param pt The INI object to load the configuration from
	 */
	void load(INI &pt);

	// config overrides
	static std::string api_config_filename_;
	static std::string api_config_content_;

	// core parameters
	bool allow_ipv6_, allow_ipv4_;
	uint16_t base_port_;
	uint16_t port_range_;
	bool allow_random_ports_;
	uint16_t multicast_port_;
	std::string resolve_scope_;
	std::vector<ip::address> multicast_addresses_;
	int multicast_ttl_;
	std::string listen_address_;
	std::vector<std::string> known_peers_;
	std::string session_id_;
	// tuning parameters
	int use_protocol_version_;
	double watchdog_time_threshold_;
	double watchdog_check_interval_;
	double multicast_min_rtt_;
	double multicast_max_rtt_;
	double unicast_min_rtt_;
	double unicast_max_rtt_;
	double continuous_resolve_interval_;
	int timer_resolution_;
	int max_cached_queries_;
	double time_update_interval_;
	int time_update_minprobes_;
	int time_probe_count_;
	double time_probe_interval_;
	double time_probe_max_rtt_;
	int outlet_buffer_reserve_ms_;
	int outlet_buffer_reserve_samples_;
	int socket_send_buffer_size_;
	int inlet_buffer_reserve_ms_;
	int inlet_buffer_reserve_samples_;
	int socket_receive_buffer_size_;
	float smoothing_halftime_;
	bool force_default_timestamps_;
};

// initialize configuration file name
inline std::string api_config::api_config_filename_ = "";
inline std::string api_config::api_config_content_ = "";

} // namespace lsl

#endif
