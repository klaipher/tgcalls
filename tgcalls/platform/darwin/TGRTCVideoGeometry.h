#ifndef TGRTCVideoGeometry_h
#define TGRTCVideoGeometry_h

#include <cstddef>

namespace tgcalls {
namespace videogeometry {

inline int chromaHeight(int height) {
    return height > 0 ? (height + 1) / 2 : 0;
}

inline size_t planeLength(int stride, int rows) {
    return stride > 0 && rows > 0 ? (size_t)stride * (size_t)rows : 0;
}

}
}

#endif
