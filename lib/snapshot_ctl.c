#include "blk-snap/snapshot_ctl.h"

#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/ioctl.h>
#include <stdio.h>

#include "../module/blk-snap-ctl.h"

struct snap_ctx
{
    int fd;
    // ?
    //error num
    //error string
};

int snap_ctx_create(struct snap_ctx** ctx)
{
    int error = 0;
    *ctx = malloc(sizeof(struct snap_ctx));
    if (*ctx == NULL)
        return -1;

    (*ctx)->fd = open( "/dev/"MODULE_NAME, O_RDWR );
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

