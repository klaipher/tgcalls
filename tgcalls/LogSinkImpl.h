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
	// needs, so a file log turns over at half of `maxBytes`: the full file is renamed to `<path>.1` and a new one
	// starts behind a marker line. Together the two files never exceed `maxBytes` and never lose the last lines
	// (collectors read `<path>.1` first, then `<path>`). The in-memory log keeps the newest half at `maxBytes`.
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
