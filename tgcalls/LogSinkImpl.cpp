#include "LogSinkImpl.h"

#include "Instance.h"

#ifdef WEBRTC_WIN
#include "windows.h"
#include <ctime>
#else // WEBRTC_WIN
#include <sys/time.h>
#endif // WEBRTC_WIN

namespace tgcalls {

LogSinkImpl::LogSinkImpl(const FilePath &logPath, size_t maxBytes) :
_maxBytes(maxBytes) {
	if (!logPath.data.empty()) {
		_file.open(logPath.data);
	} else {
		_discards = true;
	}
}

void LogSinkImpl::OnLogMessage(const std::string &msg, rtc::LoggingSeverity severity, const char *tag) {
	OnLogMessage(std::string(tag) + ": " + msg);
}

void LogSinkImpl::OnLogMessage(const std::string &message, rtc::LoggingSeverity severity) {
	OnLogMessage(message);
}

void LogSinkImpl::OnLogMessage(const std::string &message) {
#if !DEBUG
	if (_discards) {
		return;
	}
#endif
	time_t rawTime;
	time(&rawTime);
	struct tm timeinfo;

#ifdef WEBRTC_WIN
	localtime_s(&timeinfo, &rawTime);

	FILETIME ft;
	unsigned __int64 full = 0;
	GetSystemTimeAsFileTime(&ft);

	full |= ft.dwHighDateTime;
	full <<= 32;
	full |= ft.dwLowDateTime;

	const auto deltaEpochInMicrosecs = 11644473600000000Ui64;
	full -= deltaEpochInMicrosecs;
	full /= 10;
	int32_t milliseconds = (long)(full % 1000000UL) / 1000;
#else
	timeval curTime = { 0 };
	localtime_r(&rawTime, &timeinfo);
	gettimeofday(&curTime, nullptr);
	int32_t milliseconds = curTime.tv_usec / 1000;
#endif

	if (!_discards && !_truncated) {
		auto &stream = _file.is_open() ? (std::ostream&)_file : _data;
		// 24 bytes for the timestamp prefix.
		_writtenBytes += message.size() + 24;
		if (_writtenBytes > _maxBytes) {
			_truncated = true;
			stream << "log truncated: size limit reached\n";
		} else stream
			<< (timeinfo.tm_year + 1900)
			<< "-" << (timeinfo.tm_mon + 1)
			<< "-" << (timeinfo.tm_mday)
			<< " " << timeinfo.tm_hour
			<< ":" << timeinfo.tm_min
			<< ":" << timeinfo.tm_sec
			<< ":" << milliseconds
			<< " " << message;
	}

#if DEBUG
    printf("%d-%d-%d %d:%d:%d:%d %s\n", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, milliseconds, message.c_str());
#endif
}

void configureDebugLogging(rtc::LoggingSeverity debugBuildSeverity) {
#ifdef DEBUG
	rtc::LogMessage::LogToDebug(debugBuildSeverity);
#else
	rtc::LogMessage::LogToDebug(rtc::LS_NONE);
#endif
}

} // namespace tgcalls
