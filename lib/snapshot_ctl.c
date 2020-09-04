#include "blk-snap/snapshot_ctl.h"

#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <string.h>
#include "utils.h"

//@todo: move declaration of id to common space
#define ID_LENGTH 16

struct snap_ctx
{
    int fd;
    // ?
    //error num
    //error string
};

struct snap_store_ctx
{
    unsigned char id[16];
};


int snap_ctx_create(struct snap_ctx** ctx)
{
    int error = 0;
    *ctx = malloc(sizeof(struct snap_ctx));
    if (*ctx == NULL)
        return -1;

    (*ctx)->fd = open( "/dev/"MODULE_NAME, O_RDWR );
//    (*ctx)->fd = open( "/dev/veeamsnap", O_RDWR );
    if ((*ctx)->fd == -1)
    {
        error = errno;
        free(*ctx);
    }

    if (error == 0)
        return 0;

    errno = error;
    return -1;
}

int snap_ctx_destroy(struct snap_ctx* ctx)
{
    int error = 0;
    if (close(ctx->fd) != 0)
        error = errno;

    free(ctx);

    if (error == 0)
        return 0;

    errno = error;
    return -1;
}

int snap_store_ctx_free(struct snap_store_ctx* ctx)
{
    free(ctx);
    return 0;
}

int snap_add_to_tracking(struct snap_ctx* ctx, dev_t dev)
{
    struct ioctl_dev_id_s dev_id;
    dev_id.major = major(dev);
    dev_id.minor = minor(dev);

    return ioctl(ctx->fd, IOCTL_TRACKING_ADD, &dev_id);
}

int snap_remove_from_tracking(struct snap_ctx* ctx, dev_t dev)
{
    struct ioctl_dev_id_s dev_id;
    dev_id.major = major(dev);
    dev_id.minor = minor(dev);

    return ioctl(ctx->fd, IOCTL_TRACKING_REMOVE, &dev_id);
}

int snap_get_tracking(struct snap_ctx* ctx, struct cbt_info_s* cbtInfos, unsigned int* count)
{
    struct ioctl_tracking_collect_s get;
    get.count = *count;
    get.p_cbt_info = cbtInfos;

    if (ioctl(ctx->fd, IOCTL_TRACKING_COLLECT, &get))
        return -1;

    *count = get.count;
    return 0;
}

int snap_read_cbt(struct snap_ctx* ctx, dev_t dev, unsigned int offset, int length, unsigned char* buffer)
{
    struct ioctl_tracking_read_cbt_bitmap_s bitmap;
    bitmap.dev_id.major = major(dev);
    bitmap.dev_id.minor = minor(dev);
    bitmap.offset = offset;
    bitmap.length = length;

    bitmap.ull_buff = 0ULL;
    bitmap.buff   = buffer;

    return ioctl(ctx->fd, IOCTL_TRACKING_READ_CBT_BITMAP, &bitmap);
}


struct snap_store_ctx* snap_create_snapshot_store(struct snap_ctx* ctx,
                                                  struct ioctl_dev_id_s store_dev,
                                                  struct ioctl_dev_id_s snap_dev)
{
    struct ioctl_snapstore_create_s param;
    if (generate_random(param.id, ID_LENGTH) != ID_LENGTH)
        return NULL;

    param.count =1;
    param.snapstore_dev_id.minor = store_dev.minor;
    param.snapstore_dev_id.major = store_dev.major;

    param.p_dev_id = &snap_dev;

    struct snap_store_ctx* snap_store_ctx = malloc(sizeof(struct snap_store_ctx));
    if (snap_store_ctx == NULL)
        return NULL;

    int result = ioctl( ctx->fd, IOCTL_SNAPSTORE_CREATE, &param);
    if(result != 0)
    {
        snap_store_ctx_free(snap_store_ctx);
        return NULL;
    }

    memcpy(snap_store_ctx->id, param.id, ID_LENGTH);
    return snap_store_ctx;
}

int snap_create_inmemory_snapshot_store(struct snap_ctx* ctx,
                                        struct snap_store_ctx* store_ctx,
                                        unsigned long long length)
{
    struct ioctl_snapstore_memory_limit_s param;
    memcpy(param.id, store_ctx->id, ID_LENGTH);
    param.size = length;

    return ioctl(ctx->fd, IOCTL_SNAPSTORE_MEMORY, &param);
}
