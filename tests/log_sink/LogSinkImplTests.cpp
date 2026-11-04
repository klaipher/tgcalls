#include "LogSinkImpl.h"
#include "Instance.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

using tgcalls::FilePath;
using tgcalls::LogSinkImpl;

static int failures = 0;

static void expect(bool condition, const char *name) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", name);
        failures++;
    } else {
        std::fprintf(stderr, "ok: %s\n", name);
    }
}

static void emptyPathKeepsNothing() {
    FilePath path;
    LogSinkImpl sink(path);
    for (int i = 0; i < 100000; i++) {
        sink.OnLogMessage("(group_instance.cc:1): an INFO line from a WebRTC thread", rtc::LS_INFO);
    }
    expect(sink.result().size() == 0, "an empty log path keeps no lines in memory");
}

static void filePathWritesLines() {
    const char *directory = std::getenv("TMPDIR");
    std::string pattern = std::string(directory ? directory : "/tmp") + "/tgcalls-log-sink-XXXXXX";
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back(0);
    int fd = mkstemp(name.data());
    close(fd);
    FilePath path;
    path.data = name.data();
    {
        LogSinkImpl sink(path);
        for (int i = 0; i < 10; i++) {
            sink.OnLogMessage("line", rtc::LS_INFO);
        }
        expect(sink.result().size() == 0, "a file log path keeps no lines in memory");
    }
    std::ifstream file(name.data());
    std::stringstream contents;
    contents << file.rdbuf();
    expect(contents.str().find("line") != std::string::npos, "a file log path writes lines to the file");
    unlink(name.data());
}

static void unopenablePathKeepsLines() {
    FilePath path;
    path.data = "/nonexistent-directory-for-tgcalls-test/log.txt";
    LogSinkImpl sink(path);
    sink.OnLogMessage("line", rtc::LS_INFO);
    expect(sink.result().find("line") != std::string::npos, "a path that cannot be opened still keeps lines for the debug log");
}

static void debugLogThresholdFollowsBuild() {
    rtc::LogMessage::LogToDebug(rtc::LS_INFO);
    tgcalls::configureDebugLogging(rtc::LS_VERBOSE);
#ifdef DEBUG
    expect(rtc::LogMessage::GetLogToDebug() == rtc::LS_VERBOSE, "debug builds print the requested severity to the debug log");
#else
    expect(rtc::LogMessage::GetLogToDebug() == rtc::LS_NONE, "release builds print nothing to the debug log");
    expect(!RTC_LOG_CHECK_LEVEL(LS_INFO), "release builds skip formatting INFO lines when no sink wants them");
#endif
}

static std::string readFile(const char *path) {
    std::ifstream file(path);
    std::stringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

static void fileLogKeepsTheTailWithinItsSizeCap() {
    const char *directory = std::getenv("TMPDIR");
    std::string pattern = std::string(directory ? directory : "/tmp") + "/tgcalls-log-cap-XXXXXX";
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back(0);
    close(mkstemp(name.data()));
    FilePath path;
    path.data = name.data();
    {
        LogSinkImpl sink(path, 4096);
        for (int i = 0; i < 10000; i++) {
            sink.OnLogMessage("a log line that is long enough to fill the cap quickly " + std::to_string(i) + "\n", rtc::LS_INFO);
        }
        sink.OnLogMessage("the very last line\n", rtc::LS_INFO);
        expect(sink.result().size() == 0, "a capped file log still keeps nothing in memory");
    }
    const auto contents = readFile(name.data());
    expect(contents.size() <= 4096 + 128, "a file log does not grow past its cap");
    expect(contents.find("log head dropped") != std::string::npos, "a capped file log says its head was dropped");
    expect(contents.find("the very last line") != std::string::npos, "the newest line survives the cap");
    expect(contents.find("filling") == std::string::npos && contents.find(" 9999\n") != std::string::npos, "the last numbered line is kept");
    expect(contents.find(" 0\n") == std::string::npos, "the oldest lines are dropped");
    unlink(name.data());
}

static void memoryLogKeepsTheTailWithinItsSizeCap() {
    FilePath path;
    path.data = "/nonexistent-directory-for-tgcalls-test/log.txt";
    LogSinkImpl sink(path, 4096);
    for (int i = 0; i < 10000; i++) {
        sink.OnLogMessage("a log line that is long enough to fill the cap quickly " + std::to_string(i) + "\n", rtc::LS_INFO);
    }
    sink.OnLogMessage("the very last line\n", rtc::LS_INFO);
    const auto result = sink.result();
    expect(result.size() <= 4096 + 128, "the in-memory log is capped too");
    expect(result.find("the very last line") != std::string::npos, "the in-memory log keeps the newest line");
}

int main() {
    fileLogKeepsTheTailWithinItsSizeCap();
    memoryLogKeepsTheTailWithinItsSizeCap();
    emptyPathKeepsNothing();
    filePathWritesLines();
    unopenablePathKeepsLines();
    debugLogThresholdFollowsBuild();
    return failures == 0 ? 0 : 1;
}
