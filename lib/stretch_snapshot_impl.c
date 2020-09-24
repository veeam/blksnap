#include "stretch_snapshot_impl.h"
#include <malloc.h>
#include <string.h>
#include <alloca.h>
#include <errno.h>
#include <stdbool.h>
#include "utils.h"

#pragma pack(push, 1)

struct initSnapStoreParams
{
    uint32_t cmd;
    unsigned char id[SNAP_ID_LENGTH];
    uint64_t limits;
    struct ioctl_dev_id_s snapStoreDevId;
    uint32_t DevsCount;
    struct ioctl_dev_id_s DevsId[0];
};

struct acknowledgeResponse
{
    uint32_t status;
};

#pragma pack(pop)

struct snap_store *stretch_create_snap_store(struct snap_stretch_store_ctx* stretch_ctx, uint64_t limit, struct ioctl_dev_id_s* snapStoreId,
                                             struct ioctl_dev_id_s *snapDevId, uint32_t devCount)
{
    size_t structSize = sizeof(struct initSnapStoreParams) + (sizeof(struct ioctl_dev_id_s) * devCount);
    //@todo: mb use malloc
    struct initSnapStoreParams *params = (struct initSnapStoreParams *) alloca(structSize);
    if (params == NULL)
        return NULL;

    params->cmd = BLK_SNAP_CHARCMD_INITIATE;

    generate_random(params->id, SNAP_ID_LENGTH);
    params->limits = limit;
    params->snapStoreDevId.major = snapStoreId->major;
    params->snapStoreDevId.minor = snapStoreId->minor;

    params->DevsCount = devCount;
    //@todo maybe memcpy
    for (uint32_t i = 0; i < devCount; ++i)
    {
        params->DevsId[i].major = snapDevId[i].major;
        params->DevsId[i].minor = snapDevId[i].minor;
    }

    int res = snap_write(stretch_ctx->ctx, params, structSize);
    if (res != structSize)
    {
        stretch_ctx->error = errno;
        //@todo we need set error str: Failed to call BLK_SNAP_CHARCMD_INITIATE
        return NULL;
    }

    int ackStatus = read_ack(stretch_ctx->ctx, -1);
    if (ackStatus == -1)
    {
        //@todo we need set error str: Failed to read ack in init_snap_store
        stretch_ctx->error = errno; //@todo: not sure
//        return -1;
        return NULL;
    }
    else if (ackStatus != 0)
    {
        //@todo we need set error str: Bad ack in init_snap_store
        stretch_ctx->error = ackStatus;
        return NULL;
    }

    //@todo: struct stretch context *store
    struct snap_store *store = (struct snap_store *) malloc(sizeof(struct snap_store));
    memcpy(store->id, params->id, SNAP_ID_LENGTH);

    return store;
}

int stretch_add_space(struct snap_stretch_store_ctx* stretch_ctx, struct snap_ranges_space* space)
{
    size_t struct_size = sizeof(struct stretch_space_portion_params) + (sizeof(struct stretch_range) * space->count);
    //@todo use malloc?
    struct stretch_space_portion_params* params = alloca(struct_size);

    params->cmd = BLK_SNAP_CHARCMD_NEXT_PORTION;
    memcpy(params->id, stretch_ctx->snap_store->id, SNAP_ID_LENGTH);

    params->range_count = space->count;
    for (size_t i = 0; i < params->range_count; i++)
    {
        params->ranges[i].right = space->ranges[i].right;
        params->ranges[i].left = space->ranges[i].left;
    }

    int res = snap_write(stretch_ctx->ctx, params, struct_size);
    if (res != struct_size)
    {
        stretch_ctx->error = errno;
//        stretch_ctx->error_str = Failed to add space
        return -1;
    }

    stretch_ctx->space += space->count;
    //@todo: why no ack
    return 0;
}

int read_response(struct snap_ctx* snapCtx, int timeout, struct stretch_response* response)
{
    int pollRes = snap_poll(snapCtx, timeout);
    if (pollRes == -1)
        return -1;
    else if (pollRes == 0)
        return 0;

    //@todo for example we need say where error occurred: in poll or read
    if (snap_read(snapCtx, response, sizeof(struct stretch_response)) == -1)
        return -1;

    return response->cmd;
}

int read_ack(struct snap_ctx* snapCtx, int timeout)
{
    struct stretch_response response;
    if (read_response(snapCtx, timeout, &response) != BLK_SNAP_CHARCMD_ACKNOWLEDGE)
        return -1; //@todo we need set error: Failed to read ack in init_snap_store

    return ((struct acknowledgeResponse*)response.buffer)->status;
}

int stretch_run_maintenance_loop(struct snap_stretch_store_ctx* stretch_ctx)
{
    while (true)
    {
        struct stretch_response response;
        int cmd = read_response(stretch_ctx->ctx, 5000, &response); //@todo why 5000

        if (cmd == 0) // timeout
            continue;

        if (cmd == -1)
        {
            //@todo: error_str = "Failed to read response"
            stretch_ctx->error = errno;
            return -1;
        }

        int res = 0;
        if (cmd == BLK_SNAP_CHARCMD_HALFFILL)
        {
            struct halfFill_response* half_fill = (struct halfFill_response*)response.buffer;
            res = process_half_fill_response(stretch_ctx, half_fill);
        }
        else if (cmd == BLK_SNAP_CHARCMD_OVERFLOW)
        {
            struct overflow_response* overflow = (struct overflow_response*)response.buffer;
            res = process_overflow_response(stretch_ctx, overflow);
        }
        else if (cmd == BLK_SNAP_CHARCMD_TERMINATE)
        {
            struct terminate_response* terminate = (struct terminate_response*)response.buffer;
            process_terminate(stretch_ctx, terminate);
            return 0;
        }

        if (res != 0)
            return res;
    }

//    return -1; //@todo: ?
}

int process_half_fill_response(struct snap_stretch_store_ctx* stretch_ctx, struct halfFill_response* half_fill)
{
    //@todo: review this

    struct snap_ranges_space* space = stretch_ctx->callbacks->require_snapstore_space(stretch_ctx, half_fill->FilledStatus);
    if (space == NULL)
    {
        //@todo: if client want send error
        //@todo: mb it's ok
        return 0;
    }

    int result = stretch_add_space(stretch_ctx, space);
    stretch_ctx->callbacks->snap_space_added_result(stretch_ctx, space, result);
    return result;
}

int process_overflow_response(struct snap_stretch_store_ctx* stretch_ctx, struct overflow_response* overflow)
{
    stretch_ctx->error = EOVERFLOW;
    //@todo: stretch_ctx->error_str

    stretch_ctx->callbacks->snapshot_overflow(stretch_ctx, overflow->filledStatus);
    return -1;
}

int process_terminate(struct snap_stretch_store_ctx* stretch_ctx, struct terminate_response* terminate)
{
    //@todo: review this
    stretch_ctx->error = 0;
    //@todo: stretch_ctx->error_str - clean

    stretch_ctx->callbacks->snapshot_terminated(stretch_ctx, terminate->filledStatus);
    return 0;
}
