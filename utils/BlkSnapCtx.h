#ifndef BLK_SNAP_BLKSNAPCTX_H
#define BLK_SNAP_BLKSNAPCTX_H

#include <blk-snap/snapshot_ctl.h>

class BlkSnapCtx
{
public:
    BlkSnapCtx();
    ~BlkSnapCtx();

    struct snap_ctx* raw();

private:
    struct snap_ctx* m_ctx;
};

#endif // BLK_SNAP_BLKSNAPCTX_H
