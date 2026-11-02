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

int main() {
    emptyPathKeepsNothing();
    filePathWritesLines();
    unopenablePathKeepsLines();
    return failures == 0 ? 0 : 1;
}
