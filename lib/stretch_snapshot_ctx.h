#ifndef BLK_SNAP_STRETCH_SNAPSHOT_CTX_H
#define BLK_SNAP_STRETCH_SNAPSHOT_CTX_H

#include <blk-snap/stretch_snapshot_ctrl.h>
#include <stdint.h>

#define ERROR_LENGTH 4096
struct snap_stretch_store_ctx
{
    struct snap_ctx* ctx;
    struct stretch_callbacks* callbacks;
    struct snap_store* snap_store;
    void* client_data;
    int error;
    char error_str[ERROR_LENGTH];
    uint64_t space;
};

#endif //BLK_SNAP_STRETCH_SNAPSHOT_CTX_H
