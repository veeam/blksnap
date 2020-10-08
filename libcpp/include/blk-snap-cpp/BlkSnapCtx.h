#ifndef BLK_SNAP_BLKSNAPCTX_H
#define BLK_SNAP_BLKSNAPCTX_H

#include <blk-snap/snapshot_ctl.h>
#include <memory>

class BlkSnapCtx
{
public:
    using Ptr = std::shared_ptr<BlkSnapCtx>;

    BlkSnapCtx();
    ~BlkSnapCtx();

    struct snap_ctx* raw();

private:
    struct snap_ctx* m_ctx;
};

#endif // BLK_SNAP_BLKSNAPCTX_H
