#include "LogSinkImpl.h"

#include "Instance.h"

#include <cstdio>

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
		_path = logPath.data;
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

	if (!_discards) {
		// 24 bytes for the timestamp prefix. A file log is two halves (the current file and `<path>.1`), so
		// the current file turns over at half of the cap; the in-memory log drops its head at the whole cap.
		const size_t added = message.size() + 24;
		const size_t limit = _file.is_open() ? _maxBytes / 2 : _maxBytes;
		if (_writtenBytes + added > limit) {
			dropOldestHalf();
		}
		_writtenBytes += added;
		auto &stream = _file.is_open() ? (std::ostream&)_file : _data;
		stream
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

// A file log turns over by rename (no read-back, no rewrite) because this runs inside WebRTC's global log lock:
// the full file becomes `<path>.1` (replacing the previous one) and a new file starts with a marker, so the
// newest `_maxBytes / 2` to `_maxBytes` of the log survive across the two files. The in-memory log keeps the
// newest half from a line start behind the marker.
void LogSinkImpl::dropOldestHalf() {
	static const char *const marker = "log head dropped: older lines were removed to keep the log under its size limit\n";
	if (_file.is_open()) {
		_file.flush();
		_file.close();
		const std::string rotated = _path + ".1";
		std::remove(rotated.c_str());
		if (std::rename(_path.c_str(), rotated.c_str()) != 0) {
			// Could not keep the old half: the new file below truncates it, which still bounds the size.
		}
		_file.open(_path, std::ios::out | std::ios::trunc);
		if (_file.is_open()) {
			_file << marker;
			_writtenBytes = std::string(marker).size();
			return;
		}
	}
	const size_t keep = _maxBytes / 2;
	const std::string all = _data.str();
	std::string tail = all.size() > keep ? all.substr(all.size() - keep) : all;
	_data.str(std::string());
	_data.clear();
	const auto lineStart = tail.find('\n');
	if (lineStart != std::string::npos && tail.size() > keep / 2) {
		tail.erase(0, lineStart + 1);
	}
	_data << marker << tail;
	_writtenBytes = std::string(marker).size() + tail.size();
}

void configureDebugLogging(rtc::LoggingSeverity debugBuildSeverity) {
#ifdef DEBUG
	rtc::LogMessage::LogToDebug(debugBuildSeverity);
#else
	rtc::LogMessage::LogToDebug(rtc::LS_NONE);
#endif
}

} // namespace tgcalls
