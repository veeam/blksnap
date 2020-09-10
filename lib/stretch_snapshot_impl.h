#ifndef BLK_SNAP_STRETCH_SNAPSHOT_IMPL_H
#define BLK_SNAP_STRETCH_SNAPSHOT_IMPL_H

#include <blk-snap/snapshot_ctl.h>
#include <blk-snap/stretch_snapshot_ctrl.h>
#include <blk-snap/types.h>
#include <stdint.h>

#include "stretch_snapshot_ctx.h"
#include "stretch_module_response.h"

#pragma pack(push, 1)

struct stretch_range
{
    uint64_t left;
    uint64_t right;
};

struct stretch_space_portion_params
{
    uint32_t cmd;
    unsigned char id[SNAP_ID_LENGTH];
    uint32_t range_count;
    struct stretch_range ranges[0];
};

#pragma pack(pop)

struct snap_store *stretch_create_snap_store(struct snap_stretch_store_ctx* stretch_ctx, uint64_t limit, struct ioctl_dev_id_s* snapStoreId,
                                             struct ioctl_dev_id_s *snapDevId, uint32_t devCount);

int stretch_add_space(struct snap_stretch_store_ctx* stretch_ctx, struct snap_ranges_space* space);

int stretch_run_maintenance_loop(struct snap_stretch_store_ctx* stretch_ctx);
int process_half_fill_response(struct snap_stretch_store_ctx* stretch_ctx, struct halfFill_response* half_fill);
int process_overflow_response(struct snap_stretch_store_ctx* stretch_ctx, struct overflow_response* overflow);
int process_terminate(struct snap_stretch_store_ctx* stretch_ctx, struct terminate_response* terminate);

int read_response(struct snap_ctx* snapCtx, int timeout, struct stretch_response* response);
int read_ack(struct snap_ctx* snapCtx, int timeout);

#endif //BLK_SNAP_STRETCH_SNAPSHOT_IMPL_H
