#include "BoundedHistory.h"

#include <cstdio>
#include <vector>

namespace {

int g_failures = 0;

#define CHECK_TRUE(cond)                                                  \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            g_failures++;                                                 \
        }                                                                 \
    } while (0)

using namespace tgcalls;

void TestHistoryKeepsTheNewestRecords() {
    std::vector<int> history;
    for (int i = 0; i < 10000; i++) {
        appendBounded(history, i, 100);
        CHECK_TRUE(history.size() <= 100);
    }
    CHECK_TRUE(history.back() == 9999);
    CHECK_TRUE(history.size() >= 50);
    // Still in order, still contiguous.
    for (size_t i = 1; i < history.size(); i++) {
        CHECK_TRUE(history[i] == history[i - 1] + 1);
    }
}

void TestShortHistoryIsUntouched() {
    std::vector<int> history;
    for (int i = 0; i < 10; i++) {
        appendBounded(history, i, 100);
    }
    CHECK_TRUE(history.size() == 10 && history.front() == 0);
}

void TestZeroLimitKeepsNothing() {
    std::vector<int> history;
    appendBounded(history, 1, 0);
    CHECK_TRUE(history.empty());
}

} // namespace

int main() {
    TestHistoryKeepsTheNewestRecords();
    TestShortHistoryIsUntouched();
    TestZeroLimitKeepsNothing();
    if (g_failures == 0) {
        std::printf("ok\n");
    }
    return g_failures == 0 ? 0 : 1;
}
