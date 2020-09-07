#ifndef BLKSNAP_LIB_SNAPSHOT_CTL_H
#define BLKSNAP_LIB_SNAPSHOT_CTL_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include "types.h"


struct snap_ctx;
struct snap_store_ctx;

//@todo: [TBD] little documentation
//return 0 - success
int snap_ctx_create(struct snap_ctx** ctx);
int snap_ctx_destroy(struct snap_ctx* ctx);

int snap_store_ctx_free(struct snap_store_ctx* ctx);

//@todo: [TBD] little documentation
int snap_add_to_tracking(struct snap_ctx* ctx, dev_t dev);
int snap_remove_from_tracking(struct snap_ctx* ctx, dev_t dev);
int snap_get_tracking(struct snap_ctx* ctx, struct cbt_info_s* cbtInfos, unsigned int* count);
int snap_read_cbt(struct snap_ctx* ctx, dev_t dev, unsigned int offset, int length, unsigned char* buffer);

struct snap_store_ctx* snap_create_snapshot_store(struct snap_ctx* ctx,
                                                  struct ioctl_dev_id_s store_dev,
                                                  struct ioctl_dev_id_s snap_dev);

int snap_create_inmemory_snapshot_store(struct snap_ctx* ctx,
                                        struct snap_store_ctx* store_ctx,
                                        unsigned long long length);

unsigned long long snap_create_snapshot(struct snap_ctx* ctx,
                                               struct ioctl_dev_id_s devId);

#ifdef  __cplusplus
}
#endif

#endif //BLKSNAP_LIB_SNAPSHOT_CTL_H
