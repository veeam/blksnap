#ifndef BLKSNAP_LIB_SNAPSHOT_CTL_H
#define BLKSNAP_LIB_SNAPSHOT_CTL_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include "types.h"

#define SNAP_ID_LENGTH 16
struct snap_ctx;
struct snap_store
{
    unsigned char id[SNAP_ID_LENGTH];
};

//@todo: [TBD] little documentation
//return 0 - success
int snap_ctx_create(struct snap_ctx** ctx);
int snap_ctx_destroy(struct snap_ctx* ctx);

int snap_store_ctx_free(struct snap_store* ctx);

//@todo: [TBD] little documentation
int snap_add_to_tracking(struct snap_ctx* ctx, dev_t dev);
int snap_remove_from_tracking(struct snap_ctx* ctx, dev_t dev);
int snap_get_tracking(struct snap_ctx* ctx, struct cbt_info_s* cbtInfos, unsigned int* count);
unsigned int snap_get_tracking_block_size(struct snap_ctx* ctx);
int snap_read_cbt(struct snap_ctx* ctx, dev_t dev, unsigned int offset, int length, unsigned char* buffer);

struct snap_store* snap_create_snapshot_store(struct snap_ctx* ctx,
                                              struct ioctl_dev_id_s store_dev,
                                              struct ioctl_dev_id_s snap_dev);

int snap_snapshot_store_cleanup(struct snap_ctx* ctx,
                                struct snap_store* store_ctx);

int snap_create_inmemory_snapshot_store(struct snap_ctx* ctx,
                                        struct snap_store* store_ctx,
                                        unsigned long long length);

int snap_create_file_snapshot_store(struct snap_ctx* ctx,
                                    struct snap_store* store_ctx,
                                    struct ioctl_range_s* ranges,
                                    unsigned int ranges_count);

unsigned long long snap_create_snapshot(struct snap_ctx* ctx,
                                        struct ioctl_dev_id_s devId);

int snap_destroy_snapshot(struct snap_ctx* ctx,
                          unsigned long long snapshot_id);

int snap_snapshot_get_errno(struct snap_ctx* ctx, struct ioctl_dev_id_s devId);

int snap_mark_dirty_blocks(struct snap_ctx* ctx,
                           struct ioctl_dev_id_s devId,
                           struct block_range_s* dirty_blocks,
                           unsigned int count);

//@todo make with this something
int snap_poll(struct snap_ctx* ctx, int timeout);
ssize_t snap_read(struct snap_ctx* ctx, void *buf, size_t length);
int snap_write(struct snap_ctx* ctx, void *buf, size_t length);

#ifdef  __cplusplus
}
#endif

#endif //BLKSNAP_LIB_SNAPSHOT_CTL_H
