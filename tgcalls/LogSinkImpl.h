#ifndef TGCALLS_LOG_SINK_IMPL_H
#define TGCALLS_LOG_SINK_IMPL_H

#include "rtc_base/logging.h"
#include <fstream>
#include <sstream>
#include <string>

namespace tgcalls {

struct FilePath;

class LogSinkImpl final : public rtc::LogSink {
public:
	// A call log that runs for hours must not fill the disk or the heap. The END of a call is what a bug report
	// needs, so at `maxBytes` the sink keeps the newest half (from a line start) behind one marker line and
	// goes on: the log never exceeds `maxBytes` and never loses the last lines.
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
	std::string _path;

	void dropOldestHalf();

};

void configureDebugLogging(rtc::LoggingSeverity debugBuildSeverity);

} // namespace tgcalls

#endif
