#ifndef BLKSNAP_LIB_SNAPSHOT_CTL_H
#define BLKSNAP_LIB_SNAPSHOT_CTL_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <sys/types.h>


struct snap_ctx;

//@todo: [TBD] little documentation
//return 0 - success
int snap_ctx_create(struct snap_ctx** ctx);
int snap_ctx_destroy(struct snap_ctx* ctx);

//@todo: [TBD] little documentation
int snap_add_to_tracking(struct snap_ctx* ctx, dev_t dev);
int snap_remove_from_tracking(struct snap_ctx* ctx, dev_t dev);

//void RemoveFromTracking(const DeviceId_t
//
//& devId );


#ifdef  __cplusplus
}
#endif

#endif //BLKSNAP_LIB_SNAPSHOT_CTL_H
