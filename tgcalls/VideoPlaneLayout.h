#ifndef TGCALLS_VIDEO_PLANE_LAYOUT_H
#define TGCALLS_VIDEO_PLANE_LAYOUT_H

#include <cstddef>

namespace tgcalls {

// Rows in a chroma plane of 4:2:0 video (I420 U and V, NV12 interleaved UV) for a luma plane
// of `lumaHeight` rows.
inline int chromaPlaneRows(int lumaHeight) {
    return lumaHeight > 0 ? (lumaHeight + 1) / 2 : 0;
}

// Bytes in a plane of `rows` rows laid out with `stride` bytes per row.
inline size_t videoPlaneByteLength(int stride, int rows) {
    if (stride <= 0 || rows <= 0) {
        return 0;
    }
    return (size_t)stride * (size_t)rows;
}

} // namespace tgcalls

#endif
