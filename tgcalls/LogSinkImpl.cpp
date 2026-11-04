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
		// 24 bytes for the timestamp prefix.
		_writtenBytes += message.size() + 24;
		if (_writtenBytes > _maxBytes) {
			dropOldestHalf();
		}
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

// Keeps the newest half of the log, starting at a line, behind a marker; the log restarts at about half of the cap.
void LogSinkImpl::dropOldestHalf() {
	static const char *const marker = "log head dropped: older lines were removed to keep the log under its size limit\n";
	const size_t keep = _maxBytes / 2;
	std::string tail;
	if (_file.is_open()) {
		_file.flush();
		std::ifstream reader(_path, std::ios::binary | std::ios::ate);
		if (reader.is_open()) {
			const std::streamoff size = reader.tellg();
			const std::streamoff start = size > (std::streamoff)keep ? size - (std::streamoff)keep : 0;
			reader.seekg(start);
			tail.resize((size_t)(size - start));
			reader.read(&tail[0], (std::streamsize)tail.size());
			tail.resize((size_t)reader.gcount());
		}
		_file.close();
		_file.open(_path, std::ios::out | std::ios::trunc);
	} else {
		const std::string all = _data.str();
		tail = all.size() > keep ? all.substr(all.size() - keep) : all;
		_data.str(std::string());
		_data.clear();
	}
	const auto lineStart = tail.find('\n');
	if (lineStart != std::string::npos && tail.size() > keep / 2) {
		tail.erase(0, lineStart + 1);
	}
	auto &stream = _file.is_open() ? (std::ostream&)_file : _data;
	stream << marker << tail;
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
