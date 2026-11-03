#ifndef TGCALLS_BOUNDED_HISTORY_H
#define TGCALLS_BOUNDED_HISTORY_H

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace tgcalls {

// Appends to a debug-log history that would otherwise grow for as long as the call lasts.
// Past `limit` the oldest quarter is dropped in one move, so the cost per append stays small.
template<typename T, typename U>
void appendBounded(std::vector<T> &history, U &&value, size_t limit) {
    if (limit == 0) {
        history.clear();
        return;
    }
    if (history.size() >= limit) {
        const size_t drop = history.size() - limit + (limit + 3) / 4;
        history.erase(history.begin(), history.begin() + (std::ptrdiff_t)std::min(drop, history.size()));
    }
    history.emplace_back(std::forward<U>(value));
}

} // namespace tgcalls

#endif
