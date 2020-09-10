#ifndef BLK_SNAP_STRETCH_SNAPSHOT_CTRL_H
#define BLK_SNAP_STRETCH_SNAPSHOT_CTRL_H

#ifdef  __cplusplus
extern "C" {
#endif

#include "snapshot_ctl.h"

struct snap_stretch_store_ctx;


typedef struct snap_ranges_space* snap_require_snapshot_store_space_callback(struct snap_stretch_store_ctx* stretch_ctx, uint64_t filled);
typedef void snap_space_added_result_callback(struct snap_stretch_store_ctx* stretch_ctx, struct snap_ranges_space*, int result);
typedef void snap_snapshot_overflow_callback(struct snap_stretch_store_ctx* stretch_ctx, uint64_t filledStatus); // just notify, snapshot should be destroyed
typedef void snap_snapshot_terminated_callback(struct snap_stretch_store_ctx* stretch_ctx, uint64_t filledStatus); // just notify, snapshot should be destroyed


struct stretch_callbacks
{
    snap_require_snapshot_store_space_callback* require_snapstore_space;
    snap_space_added_result_callback* snap_space_added_result;
    snap_snapshot_overflow_callback* snapshot_overflow;
    snap_snapshot_terminated_callback* snapshot_terminated;
};

int stretch_store_ctx_create(struct snap_stretch_store_ctx** stretch_ctx);

int stretch_store_ctx_init(struct snap_stretch_store_ctx* stretch_ctx,
                           uint64_t limit,
                           struct ioctl_dev_id_s* store_dev,
                           struct ioctl_dev_id_s* snap_dev,
                           unsigned int snap_dev_count);

//@todo: if not called=> stretch_store_maintenance_loop call it first
int stretch_store_ctx_add_first_space(struct snap_stretch_store_ctx* stretch_ctx, struct snap_ranges_space* space);
int stretch_store_maintenance_loop(struct snap_stretch_store_ctx* stretch_ctx, struct stretch_callbacks* callbacks);

int stretch_store_ctx_destroy(struct snap_stretch_store_ctx* stretch_ctx);
int stretch_store_ctx_get_errno(struct snap_stretch_store_ctx* stretch_ctx);
const char* stretch_store_ctx_get_error_str(struct snap_stretch_store_ctx* stretch_ctx);

void* stretch_store_ctx_get_user_data(struct snap_stretch_store_ctx* stretch_ctx);
void stretch_store_ctx_set_user_data(struct snap_stretch_store_ctx* stretch_ctx, void* user_data);

//void stretch_store_ctx_set_callbacks(struct snap_stretch_store_ctx* stretch_ctx, struct stretch_callbacks* callbacks);
struct stretch_callbacks* stretch_store_ctx_get_callbacks(struct snap_stretch_store_ctx* stretch_ctx);

struct snap_store* stretch_store_ctx_get_snap_store(struct snap_stretch_store_ctx* stretch_ctx);

#ifdef  __cplusplus
}
#endif

#endif //BLK_SNAP_STRETCH_SNAPSHOT_CTRL_H
