#include "common.h"
#include "api_config.h"
#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <chrono>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// include mmsystem.h after windows.h
#include <mmsystem.h>
#endif

thread_local char last_error[512] = {0};

int64_t lsl::lsl_local_clock_ns() {
	return std::chrono::nanoseconds(std::chrono::steady_clock::now().time_since_epoch()).count();
}

extern "C" {

LIBLSL_C_API int32_t lsl_protocol_version() {
	return lsl::api_config::get_instance()->use_protocol_version();
}

LIBLSL_C_API int32_t lsl_library_version() { return LSL_LIBRARY_VERSION; }

LIBLSL_C_API double lsl_local_clock() {
	const auto ns_per_s = 1000000000;
	const auto seconds_since_epoch = std::lldiv(lsl::lsl_local_clock_ns(), ns_per_s);
	/* For large timestamps, converting to double and then dividing by 1e9 loses precision
	   because double has only 53 bits of precision.
	   So we calculate everything we can as integer and only cast to double at the end */
	return seconds_since_epoch.quot + static_cast<double>(seconds_since_epoch.rem) / ns_per_s;
}

LIBLSL_C_API void lsl_destroy_string(char *s) {
	if (s) free(s);
}

LIBLSL_C_API const char *lsl_last_error(void) { return last_error; }
}

// === implementation of misc functions ===

void lsl::ensure_lsl_initialized() {
	static bool is_initialized = false;

	if (!is_initialized) {
		is_initialized = true;

		loguru::g_stderr_verbosity = loguru::Verbosity_INFO;
#if LOGURU_DEBUG_LOGGING
		// Initialize loguru, mainly to print stacktraces on segmentation faults
		int argc = 1;
		const char *argv[] = {"liblsl", nullptr};
		loguru::init(argc, const_cast<char **>(argv));
#else
#endif
		LOG_F(INFO, "%s", lsl_library_info());

#ifdef _WIN32
		// if a timer resolution other than 0 is requested (0 means don't override)...
		if (int desired_timer_resolution = lsl::api_config::get_instance()->timer_resolution()) {
			// then override it for the lifetime of this program
			struct override_timer_resolution_until_exit {
				override_timer_resolution_until_exit(int res) : res_(res) { timeBeginPeriod(res_); }
				~override_timer_resolution_until_exit() { timeEndPeriod(res_); }
				int res_;
			};
			static override_timer_resolution_until_exit overrider(desired_timer_resolution);
		}
#endif
	}
}

std::vector<std::string> lsl::splitandtrim(
	const std::string &input, char separator, bool keepempty) {
	std::vector<std::string> parts;
	auto it = input.cbegin();
	while (true) {
		// Skip whitespace in the beginning
		while (it != input.cend() && std::isspace(*it)) ++it;

		// find the next separator or end of string
		auto endit = std::find(it, input.cend(), separator);
		// mark beginning of next part if not at the end
		auto next = endit;

		// shrink the range so it doesn't include whitespace at the end
		while (it < endit && std::isspace(*(endit - 1))) --endit;
		if (endit != it || keepempty) parts.emplace_back(it, endit);

		if (next != input.cend())
			it = next + 1;
		else
			break;
	}
	return parts;
}

std::string lsl::trim(const std::string &input) {
	auto first = input.find_first_not_of(" \t\r\n"), last = input.find_last_not_of(" \t\r\n");
	if (first == std::string::npos || last == std::string::npos) return "";
	return input.substr(first, last - first + 1);
}
