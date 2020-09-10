#include <blk-snap/stretch_snapshot_ctrl.h>
#include <malloc.h>
#include "stretch_snapshot_ctx.h"
#include "stretch_snapshot_impl.h"
#include <errno.h>

int stretch_store_ctx_create(struct snap_stretch_store_ctx** stretch_ctx)
{
    *stretch_ctx = calloc(1, sizeof(struct snap_stretch_store_ctx));
    if (*stretch_ctx == NULL)
        return -1;

    if (snap_ctx_create(&(*stretch_ctx)->ctx) != 0)
    {
        free(*stretch_ctx);
        return -1;
    }

    return 0;
}

int stretch_store_ctx_init(struct snap_stretch_store_ctx* stretch_ctx, uint64_t limit,
                           struct ioctl_dev_id_s* store_dev,
                           struct ioctl_dev_id_s* snap_dev,
                           unsigned int snap_dev_count)
{
    stretch_ctx->snap_store = stretch_create_snap_store(stretch_ctx, limit, store_dev, snap_dev, snap_dev_count);
    if (!stretch_ctx->snap_store)
        return -1;

    return 0;
}

int stretch_store_ctx_add_first_space(struct snap_stretch_store_ctx* stretch_ctx, struct snap_ranges_space* space)
{
    if (stretch_ctx->space != 0)
    {
        //@todo: mb we should allow user set it second time
        stretch_ctx->error = EALREADY;
        //@todo: error message
        //stretch_ctx->error_str
        return -1;
    }

    return stretch_add_space(stretch_ctx, space);
}

int stretch_store_ctx_destroy(struct snap_stretch_store_ctx* stretch_ctx)
{
    snap_ctx_destroy(stretch_ctx->ctx);

    //@todo: maybe we should stop maintenance
    //@todo: destroy snap_store
    if (stretch_ctx->snap_store)
        free(stretch_ctx->snap_store);

    free(stretch_ctx);

    return 0;
}

int stretch_store_ctx_get_errno(struct snap_stretch_store_ctx* stretch_ctx)
{
    return stretch_ctx->error;
}

const char* stretch_store_ctx_get_error_str(struct snap_stretch_store_ctx* stretch_ctx)
{
    return stretch_ctx->error_str;
}

void* stretch_store_ctx_get_user_data(struct snap_stretch_store_ctx* stretch_ctx)
{
    return stretch_ctx->client_data;
}

void stretch_store_ctx_set_user_data(struct snap_stretch_store_ctx* stretch_ctx, void* user_data)
{
    stretch_ctx->client_data = user_data;
}

//void stretch_store_ctx_set_callbacks(struct snap_stretch_store_ctx* stretch_ctx, struct stretch_callbacks* callbacks)
//{
//    stretch_ctx->callbacks = callbacks;
//}

struct stretch_callbacks* stretch_store_ctx_get_callbacks(struct snap_stretch_store_ctx* stretch_ctx)
{
    return stretch_ctx->callbacks;
}

struct snap_store* stretch_store_ctx_get_snap_store(struct snap_stretch_store_ctx* stretch_ctx)
{
    return stretch_ctx->snap_store;
}

int stretch_store_maintenance_loop(struct snap_stretch_store_ctx* stretch_ctx, struct stretch_callbacks* callbacks)
{
    //@todo: check callbacks
    stretch_ctx->callbacks = callbacks;

    //@todo: if no space added => call     stretch_ctx->callbacks->require_snapstore_space
    stretch_run_maintenance_loop(stretch_ctx);
}
