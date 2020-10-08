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
#include <poll.h>


const char* SNAPSHOT_IMAGE_NAME = "/dev/"MODULE_NAME;

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

    (*ctx)->fd = open( SNAPSHOT_IMAGE_NAME, O_RDWR );
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

int snap_store_ctx_free(struct snap_store* ctx)
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

unsigned int snap_get_tracking_block_size(struct snap_ctx* ctx)
{
    unsigned int block_size = 0;

    if (ioctl(ctx->fd, IOCTL_TRACKING_BLOCK_SIZE, &block_size ))
        return 0;

    return block_size;
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


struct snap_store* snap_create_snapshot_store(struct snap_ctx* ctx,
                                              struct ioctl_dev_id_s store_dev,
                                              struct ioctl_dev_id_s snap_dev)
{
    struct ioctl_snapstore_create_s param;
    if (generate_random(param.id, SNAP_ID_LENGTH) != SNAP_ID_LENGTH)
        return NULL;

    param.snapstore_dev_id.minor = store_dev.minor;
    param.snapstore_dev_id.major = store_dev.major;

    param.count =1;
    param.p_dev_id = &snap_dev;

    struct snap_store* snap_store_ctx = malloc(sizeof(struct snap_store));
    if (snap_store_ctx == NULL)
        return NULL;

    int result = ioctl( ctx->fd, IOCTL_SNAPSTORE_CREATE, &param);
    if(result != 0)
    {
        snap_store_ctx_free(snap_store_ctx);
        return NULL;
    }

    memcpy(snap_store_ctx->id, param.id, SNAP_ID_LENGTH);
    return snap_store_ctx;
}


int snap_snapshot_store_cleanup(struct snap_ctx* ctx, struct snap_store* store_ctx)
{
    struct ioctl_snapstore_cleanup_s param = { 0 };
    memcpy(param.id, store_ctx->id, SNAP_ID_LENGTH);

    return ioctl(ctx->fd, IOCTL_SNAPSTORE_CLEANUP, &param);
}

int snap_create_inmemory_snapshot_store(struct snap_ctx* ctx,
                                        struct snap_store* store_ctx,
                                        unsigned long long length)
{
    struct ioctl_snapstore_memory_limit_s param;
    memcpy(param.id, store_ctx->id, SNAP_ID_LENGTH);
    param.size = length;

    return ioctl(ctx->fd, IOCTL_SNAPSTORE_MEMORY, &param);
}

int snap_create_file_snapshot_store(struct snap_ctx* ctx,
                                    struct snap_store* store_ctx,
                                    struct ioctl_range_s* ranges,
                                    unsigned int ranges_count)
{
    struct ioctl_snapstore_file_add_s param;
    memcpy(param.id, store_ctx->id, SNAP_ID_LENGTH);
    param.range_count = ranges_count;
    param.ranges = ranges;

    return ioctl(ctx->fd, IOCTL_SNAPSTORE_FILE, &param);
}

unsigned long long snap_create_snapshot(struct snap_ctx* ctx,
                                        struct ioctl_dev_id_s devId)
{
    struct ioctl_snapshot_create_s create_snapshot = { 0 };
    create_snapshot.snapshot_id = 0;
    create_snapshot.count = 1;
    create_snapshot.p_dev_id = &devId;

    if (ioctl(ctx->fd, IOCTL_SNAPSHOT_CREATE, &create_snapshot) != 0)
        return 0;

    return create_snapshot.snapshot_id;
}

int snap_destroy_snapshot(struct snap_ctx* ctx, unsigned long long snapshot_id)
{
    return ioctl(ctx->fd, IOCTL_SNAPSHOT_DESTROY, &snapshot_id );
}

int snap_snapshot_get_errno(struct snap_ctx* ctx, struct ioctl_dev_id_s devId)
{
    struct ioctl_snapshot_errno_s param;
    param.dev_id.major = devId.major;
    param.dev_id.minor = devId.minor;
    param.err_code = 0;

    if (ioctl(ctx->fd, IOCTL_SNAPSHOT_ERRNO, &param))
        return -1;

    return param.err_code;
}

int snap_poll(struct snap_ctx* ctx, int timeout)
{
    struct pollfd fds;

    fds.fd = ctx->fd;
    fds.events = POLLIN;
    fds.revents = 0;

    return poll( &fds, 1, timeout );
}

ssize_t snap_read(struct snap_ctx* ctx, void *buf, size_t length)
{
    return read(ctx->fd, buf, length);
}

int snap_write(struct snap_ctx* ctx, void *buf, size_t length)
{
    return write(ctx->fd, buf, length);
}

int snap_mark_dirty_blocks(struct snap_ctx* ctx,
                           struct ioctl_dev_id_s devId,
                           struct block_range_s* dirty_blocks,
                           unsigned int count)
{
    struct ioctl_tracking_mark_dirty_blocks_s param;
    param.image_dev_id.major = devId.major;
    param.image_dev_id.minor = devId.minor;

    param.count = count;
    param.p_dirty_blocks = dirty_blocks;

    return ioctl(ctx->fd, IOCTL_TRACKING_MARK_DIRTY_BLOCKS, &param);
}

int snap_collect_snapshot_images(struct snap_ctx* ctx, struct image_info_s* images_info, size_t* images_length)
{
    struct ioctl_collect_snapshot_images_s param;
    param.count = *images_length;
    param.p_image_info = images_info;

    int result = ioctl(ctx->fd, IOCTL_COLLECT_SNAPSHOT_IMAGES, &param);
    *images_length = param.count;

    return result;
}

//@todo: delete this func
void set_snapshot_image_name(const char* image_name)
{
    SNAPSHOT_IMAGE_NAME = image_name;
}
