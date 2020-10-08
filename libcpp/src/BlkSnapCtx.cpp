#include "../include/blk-snap-cpp/BlkSnapCtx.h"

#include <system_error>

BlkSnapCtx::BlkSnapCtx()
: m_ctx(nullptr)
{
    if (snap_ctx_create(&m_ctx) != 0)
        throw std::system_error(errno, std::generic_category(), "Failed to create snap context");
}

BlkSnapCtx::~BlkSnapCtx()
{
    snap_ctx_destroy(m_ctx);
}

struct snap_ctx* BlkSnapCtx::raw()
{
    return m_ctx;
}
