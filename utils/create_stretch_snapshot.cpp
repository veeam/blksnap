#include <iostream>
#include <system_error>
#include <blk-snap/snapshot_ctl.h>
#include <vector>
#include <cstring>
#include "helper.h"

std::string g_dirPath;
size_t g_currentPortion = 0;

#pragma pack(push,1)

struct initSnapStoreParams
{
    uint32_t cmd;
    unsigned char id[SNAP_ID_LENGTH];
    uint64_t limits;
    ioctl_dev_id_s snapStoreDevId;
    uint32_t DevsSize;
    ioctl_dev_id_s DevsId[0];
};

struct Range{
    uint64_t left;
    uint64_t right;
};

struct NextPortion
{
    uint32_t cmd;
    unsigned char id[SNAP_ID_LENGTH];
    uint32_t rangesSize;
    Range ranges[0];
};

struct Response
{
    uint32_t cmd;
    uint8_t buffer[1020];
};

struct AcknowledgeResponse
{
    uint32_t status;
};

struct HalfFillResponse
{
    uint64_t FilledStatus;
};

struct OverflowResponse
{
    uint32_t errorCode;
    uint64_t filledStatus;
};

struct TerminateResponse
{
    uint64_t filledStatus;
};

#pragma pack(pop)

int read_response(snap_ctx* snapCtx, int timeout = 5000)
{
    int pollRes = snap_poll(snapCtx, timeout);
    if (pollRes == -1)
        throw std::system_error(errno, std::generic_category(), "Failed to poll");
    else if (pollRes == 0)
        return 0;

    Response response;
    if (snap_read(snapCtx, &response, sizeof(response)) == -1)
        throw std::system_error(errno, std::generic_category(), "Failed to read response cmd");

    if (response.cmd == VEEAMSNAP_CHARCMD_ACKNOWLEDGE)
    {
        AcknowledgeResponse* ack = (AcknowledgeResponse*)response.buffer;
        if (ack->status != 0)
            throw std::system_error(ack->status, std::generic_category(), "Bad VEEAMSNAP_CHARCMD_ACKNOWLEDGE status");

        return VEEAMSNAP_CHARCMD_ACKNOWLEDGE;
    }
    else if (response.cmd == VEEAMSNAP_CHARCMD_HALFFILL)
    {
        HalfFillResponse* halfFill = (HalfFillResponse*)response.buffer;
        std::cout << "Snapstore already filled " <<  std::to_string( halfFill->FilledStatus >> 20 ) << "MiB" << std::endl;
        return VEEAMSNAP_CHARCMD_HALFFILL;
    }
    else if (response.cmd == VEEAMSNAP_CHARCMD_OVERFLOW)
    {
        OverflowResponse* overflow = (OverflowResponse*)response.buffer;
        std::cout << "Overflow filled " <<  std::to_string( overflow->filledStatus >> 20 ) << "MiB" << std::endl;
        throw std::system_error(overflow->errorCode, std::generic_category(), "VEEAMSNAP_CHARCMD_OVERFLOW");
    }
    else if (response.cmd == VEEAMSNAP_CHARCMD_TERMINATE)
    {
        TerminateResponse* terminate = (TerminateResponse*)response.buffer;
        std::cout << "VEEAMSNAP_CHARCMD_TERMINATE filled " <<  std::to_string( terminate->filledStatus >> 20 ) << "MiB" << std::endl;
        throw std::runtime_error("VEEAMSNAP_CHARCMD_TERMINATE");
    }
    else
    {
        std::string error = std::string ("Unknown cmd: ") + std::to_string(response.cmd);
        throw std::runtime_error(error);
    }
}

snap_store* init_snap_store(snap_ctx* snapCtx, uint64_t limit, ioctl_dev_id_s snapStoreId, ioctl_dev_id_s snapDevId)
{
    size_t structSize = sizeof(initSnapStoreParams) + sizeof(ioctl_dev_id_s);
    initSnapStoreParams* params = (initSnapStoreParams*)malloc(structSize);
    params->cmd = VEEAMSNAP_CHARCMD_INITIATE;

    generate_random(params->id, SNAP_ID_LENGTH);
    params->limits = limit;
    params->snapStoreDevId.major = snapDevId.major;
    params->snapStoreDevId.minor = snapDevId.minor;

    params->DevsSize = 1;
    params->DevsId[0].major = snapStoreId.major;
    params->DevsId[0].minor = snapStoreId.minor;

    int res = snap_write(snapCtx, params, structSize);
    if (res != structSize)
        throw std::system_error(errno, std::generic_category(), "Failed to init stretch snapshot");

    snap_store* store = (snap_store*)malloc(sizeof(snap_store));
    memcpy(store->id, params->id, SNAP_ID_LENGTH);

    free(params);
    if (read_response(snapCtx) != VEEAMSNAP_CHARCMD_ACKNOWLEDGE)
    {
        free(store);
        throw std::runtime_error("Failed to receive ack");
    }

    return store;
}

void add_snap_portion(snap_ctx* snapCtx, snap_store* snapStore)
{
    std::string snapStorePath = g_dirPath + "/#" + std::to_string(g_currentPortion++);
    std::cout << "Create storage: " << snapStorePath << std::endl;
    int fd = open(snapStorePath.c_str(), O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR );
    if (fd == -1)
        throw std::system_error(errno, std::generic_category(), "Failed to create storage");

    if (fallocate64(fd, 0, 0, 500 * 1024 * 1024))
        throw std::system_error(errno, std::generic_category(), "Failed to fallocate storage");

    std::vector<struct ioctl_range_s> ranges = EnumRanges(fd);
    size_t structSize = sizeof(NextPortion) + (sizeof(Range) * ranges.size());
    NextPortion* params = (NextPortion*)malloc(structSize);

    params->cmd = VEEAMSNAP_CHARCMD_NEXT_PORTION;

    memcpy(params->id, snapStore->id, SNAP_ID_LENGTH);

    params->rangesSize = ranges.size();
    for (size_t i = 0; i < ranges.size(); i++)
    {
        params->ranges[i].right = ranges[i].right;
        params->ranges[i].left = ranges[i].left;
    }

    int res = snap_write(snapCtx, params, structSize);
    if (res != structSize)
        throw std::system_error(errno, std::generic_category(), "Failed to write VEEAMSNAP_CHARCMD_NEXT_PORTION");

    free(params);

}

void watch(snap_ctx* snapCtx, snap_store* snapStore)
{
    std::cout << "Start watcher" << std::endl;

    while (true)
    {
        uint32_t cmd = read_response(snapCtx, 5000);
        if (cmd == VEEAMSNAP_CHARCMD_ACKNOWLEDGE)
            std::cout << "receive ack??" << std::endl;
        else if (cmd == VEEAMSNAP_CHARCMD_HALFFILL)
        {
            std::cout << "receive VEEAMSNAP_CHARCMD_HALFFILL" << std::endl;
            add_snap_portion(snapCtx, snapStore);
        }
        else if (cmd == 0)
            std::cout << "timeout" << std::endl;
        else
        {
            std::cout << "break: " << cmd << std::endl;
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc < 3)
        throw std::runtime_error("need file path");

    struct snap_ctx* snapCtx;
    if (snap_ctx_create(&snapCtx) != 0)
        throw std::system_error(errno, std::generic_category(), "Failed to create snap context");

    struct stat snapDev;
    stat(argv[1], &snapDev);
    struct ioctl_dev_id_s snapDevId = to_dev_id(snapDev.st_rdev);

    g_dirPath = std::string(argv[2]);
    struct stat snapStoreDev;
    stat(g_dirPath.c_str(), &snapStoreDev);
    struct ioctl_dev_id_s snapStoreDevId = to_dev_id(snapStoreDev.st_dev);

    snap_store* snapStore = init_snap_store(snapCtx, (500 * 1024 * 1024)/2, snapStoreDevId, snapDevId);
    std::cout << "Create snap store: " << snap_store_to_str(snapStore) << std::endl;
    add_snap_portion(snapCtx, snapStore);

    unsigned long long snapshotId = snap_create_snapshot(snapCtx, snapDevId);
    if (snapshotId == 0)
        throw std::system_error(errno, std::generic_category(), "Failed to create snapshot");

    std::cout << "Successfully create file snapshot id: " << snapshotId << std::endl;
    watch(snapCtx, snapStore);
}



