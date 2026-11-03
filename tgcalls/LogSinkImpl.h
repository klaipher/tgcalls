#ifndef TGCALLS_LOG_SINK_IMPL_H
#define TGCALLS_LOG_SINK_IMPL_H

#include "rtc_base/logging.h"
#include <fstream>

namespace tgcalls {

struct FilePath;

class LogSinkImpl final : public rtc::LogSink {
public:
	// A call log that runs for hours must not fill the disk or the heap: after `maxBytes` the sink writes
	// one marker line and drops the rest.
	static constexpr size_t kDefaultMaxBytes = 16 * 1024 * 1024;

	LogSinkImpl(const FilePath &logPath, size_t maxBytes = kDefaultMaxBytes);

	void OnLogMessage(const std::string &msg, rtc::LoggingSeverity severity, const char *tag) override;
	void OnLogMessage(const std::string &message, rtc::LoggingSeverity severity) override;
	void OnLogMessage(const std::string &message) override;

	std::string result() const {
		return _data.str();
	}

private:
	std::ofstream _file;
	std::ostringstream _data;
	bool _discards = false;
	size_t _maxBytes = kDefaultMaxBytes;
	size_t _writtenBytes = 0;
	bool _truncated = false;

};

void configureDebugLogging(rtc::LoggingSeverity debugBuildSeverity);

} // namespace tgcalls

#endif
