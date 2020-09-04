#include "stdafx.h"

#ifdef PERSISTENT_CBT

#include "cbt_checkfs.h"
#include "ext4_check.h"
#include "blk_util.h"

#define SECTION "cbt_chkfs "
#include "log_format.h"


#define CHECK_PARAMETERS_SIZE_EXT4  0x10 // size ordered by 16 byte
#define CHECK_PARAMETERS_SIZE_XFS   0x10

static int _check_offline_changes(struct block_device* blk_dev, uint32_t check_parameters_sz, void* check_parameters)
{

    // check EXT4 fs
    if (check_parameters_sz >= CHECK_PARAMETERS_SIZE_EXT4){
        uint32_t previous_sb_crc = le32_to_cpu(*(__le32*)(check_parameters + 0));
        int res = ext4_check_offline_changes(blk_dev, previous_sb_crc);
        if (res != ENOENT)
            return res;
    }

    // check XFS fs
    // to do
    //if (check_parameters_sz >= CHECK_PARAMETERS_SIZE_XFS){
    //
    //}
    return -ENOENT;
}

static int _check_unmount_status(struct block_device* blk_dev, uint32_t* p_check_parameters_sz, void** p_check_parameters)
{
    uint32_t sb_crc = 0;
    int res;

    // check EXT4 fs
    res = ext4_check_unmount_status(blk_dev, &sb_crc);

    if (res == SUCCESS){
        void* check_parameters = kmalloc(CHECK_PARAMETERS_SIZE_EXT4, GFP_KERNEL);
        if (check_parameters == NULL)
            return -ENOMEM;

        *(uint32_t*)(check_parameters + 0) = cpu_to_le32(sb_crc);

        *p_check_parameters = check_parameters;
        *p_check_parameters_sz = CHECK_PARAMETERS_SIZE_EXT4;
        return SUCCESS;
    }
    if (res != ENOENT)
        return res;

    // check XFS fs
    // to do

    return res;
}

static int _check_is_mounted(struct block_device* blk_dev, bool* p_is_mounted)
{
    struct super_block* sb = get_super(blk_dev);
    if (sb == NULL){
        *p_is_mounted = false;
        return SUCCESS;
    }

    if (sb->s_type && sb->s_type->name)
        log_tr_s("Type of mounted fs: ", sb->s_type->name);

    log_tr_uuid("fs uuid: ", (veeam_uuid_t*)&sb->s_uuid);

    drop_super(sb);
    *p_is_mounted = true;
    return SUCCESS;
}

void cbt_checkfs_status_set(cbt_checkfs_status_t* checkfs_status, dev_t dev_id, int errcode, char* message)
{
    int len = strlen(message);
    checkfs_status->dev_id = dev_id;
    checkfs_status->errcode = errcode;
     
    memcpy(checkfs_status->message_text, message, len);
    checkfs_status->message_text[len] = '\0';
    ++len;

    checkfs_status->message_length = len;
}

void cbt_checkfs_status_log(cbt_checkfs_status_t* checkfs_status)
{
    log_err_dev_t("Persistent CBT register was not stored for device ", checkfs_status->dev_id);
    log_err_d("error code: ", checkfs_status->errcode);
    log_err(checkfs_status->message_text);
}

int cbt_checkfs_start_tracker_available(dev_t dev_id, uint32_t check_parameters_sz, void* check_parameters)
{
    int res = ENODEV;
    struct block_device* blk_dev = NULL;

    log_tr_dev_t("Check tracking configuration for device ", dev_id);

    if ((check_parameters == NULL) || (check_parameters_sz == 0)){
        log_err("Invalid check fs parameters");
        return -EINVAL;
    }

    res = blk_dev_open(dev_id, &blk_dev);
    if (res != SUCCESS){
        log_err_d("Failed to open device. errcode=", res);
        return res;
    }
    do {
        bool is_mounted = false;

        log_tr("Check if the device is mounted");
        res = _check_is_mounted(blk_dev, &is_mounted);
        if (res == SUCCESS){
            if (is_mounted){
                log_err_format("Device [%d:%d] is already mounted", MAJOR(dev_id), MINOR(dev_id));
                res = EPERM;
                break;
            }
            else
                log_tr_format("Device [%d:%d] is not mounted", MAJOR(dev_id), MINOR(dev_id));
        }
        else{
            log_err_d("Failed to get fs mount state", res);
            break;
        }

        log_tr("Check the device for offline changes");
        res = _check_offline_changes(blk_dev, check_parameters_sz, check_parameters);
        if (res == SUCCESS){
            log_tr_format("Device [%d:%d] does not have offline changes", MAJOR(dev_id), MINOR(dev_id));
        }
        else{
            log_err_format("Device [%d:%d] has offline changes", MAJOR(dev_id), MINOR(dev_id));
            break;
        }
    } while (false);
    blk_dev_close(blk_dev);

    if (res != SUCCESS)
        res = EPERM;
    return res;
}

int cbt_checkfs_store_available(dev_t dev_id, uint32_t* p_check_parameters_sz, void** p_check_parameters, cbt_checkfs_status_t* error_status)
{
    int res = SUCCESS;
    struct block_device* blk_dev = NULL;

    log_tr_dev_t("Check tracking configuration for device ", dev_id);

    res = blk_dev_open(dev_id, &blk_dev);
    if (res != SUCCESS){
        cbt_checkfs_status_set(error_status, dev_id, res, "Failed to open device.");
        log_err_d("Failed to open device. errcode=", res);
        return res;
    }
    do {
        bool is_mounted = false;

        log_tr("Checking if the device is unmounted");
        res = _check_is_mounted(blk_dev, &is_mounted);
        if (res == SUCCESS){
            if (is_mounted){
                log_err_format("Device [%d:%d] is still mounted.", MAJOR(dev_id), MINOR(dev_id));
                res = EPERM;
                cbt_checkfs_status_set(error_status, dev_id, res, "Device is still mounted.");
                break;
            }else
                log_tr_format("Device [%d:%d] is already unmounted", MAJOR(dev_id), MINOR(dev_id));
        }else{
            cbt_checkfs_status_set(error_status, dev_id, res, "Failed to get fs mount state.");
            log_err_d("Failed to get fs mount state", res);
            break;
        }

        log_tr("Unmount operation result check");
        res = _check_unmount_status(blk_dev, p_check_parameters_sz, p_check_parameters);
        if (res == SUCCESS){
            log_tr_format("Device [%d:%d] has been unmounted successfully", MAJOR(dev_id), MINOR(dev_id));
        }else{
            cbt_checkfs_status_set(error_status, dev_id, res, "Failed to unmount device");
            log_err_format("Failed to unmount device [%d:%d]", MAJOR(dev_id), MINOR(dev_id));
            break;
        }
    } while (false);
    blk_dev_close(blk_dev);

    if (res != SUCCESS)
        res = EPERM;
    return res;
}

#endif //PERSISTENT_CBT