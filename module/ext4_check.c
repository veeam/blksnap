#include "stdafx.h"

#ifdef PERSISTENT_CBT

#include "ext4_check.h"
#include "blk_util.h"
#include <linux/crc32.h>
#include <linux/buffer_head.h>
#include "../../../fs/ext4/ext4.h"

#define SECTION "ext4_chkfs"
#include "log_format.h"


#define EXT4_SUPER_MAGIC	0xEF53
#define EXT4_MIN_BLOCK_SIZE		1024

static int _ext4_get_sb(struct block_device* bdev, struct ext4_super_block **p_sb)
{
    int res;
    struct buffer_head *bh = NULL;
    struct ext4_super_block *es = NULL;
    unsigned long long sb_block;
    unsigned long offset;
    unsigned blocksize = blk_dev_get_block_size(bdev);

    sb_block = EXT4_MIN_BLOCK_SIZE / blocksize;
    offset = EXT4_MIN_BLOCK_SIZE % blocksize;

    bh = __bread(bdev, sb_block, blocksize);
    if (bh == NULL) {
        log_err("Failed to read superblock");
        return -ENOMEM;
    }
    es = (struct ext4_super_block *)(bh->b_data + offset);
    do{
        struct ext4_super_block* sb = kmalloc(sizeof(struct ext4_super_block), GFP_KERNEL);
        if (sb == NULL){
            res = -ENOMEM;
            break;
        }
        if (le16_to_cpu(es->s_magic) != EXT4_SUPER_MAGIC){
            log_tr("Ext fs not found");
            kfree(sb);
            res = ENOENT;
            break;
        }

        memcpy(sb, es, sizeof(struct ext4_super_block));
        *p_sb = sb;
        res = SUCCESS;
    } while (false);
    brelse(bh);

    return res;
}



int ext4_check_offline_changes(struct block_device* bdev, uint32_t previous_crc)
{
    struct ext4_super_block* sb = NULL;
    int res = _ext4_get_sb(bdev, &sb);
    if (res == ENOENT)
        return res;

    if (res != SUCCESS){
        log_err("Failed to read superblock");
        return res;
    }

    //EXT file system found!
    log_tr_uuid("128-bit uuid for volume: ", (veeam_uuid_t*)&sb->s_uuid);
    log_tr_s("volume name: ", sb->s_volume_name);
    log_tr_d("File system state: ", le16_to_cpu(sb->s_state));

    log_tr_s_sec("Mount time: ", le32_to_cpu(sb->s_mtime));
    log_tr_s_sec("Write time: ", le32_to_cpu(sb->s_wtime));
    log_tr_s_sec("Time of last check: ", le32_to_cpu(sb->s_lastcheck));
    {
        uint32_t crc = crc32(~0, (void*)(sb), sizeof(struct ext4_super_block));

        if (crc == previous_crc){
            log_tr("Superblock crc match.");
            res = SUCCESS;
        }else{
            log_tr("Superblock crc mismatch.");
            log_tr_lx("Previous crc: ", previous_crc);
            log_tr_lx("Current crc: ", crc);
            res = ENOENT;
        }
    }
    kfree(sb);

    return res;
}

int ext4_check_unmount_status(struct block_device* bdev, uint32_t* p_sb_crc)
{
    struct ext4_super_block* sb = NULL;
    int res = _ext4_get_sb(bdev, &sb);
    if (res == ENOENT)
        return res;

    if (res != SUCCESS){
        log_err("Failed to read superblock");
        return res;
    }

    //EXT file system found!
    log_tr_uuid("128-bit uuid for volume: ", (veeam_uuid_t*)&sb->s_uuid );
    log_tr_s("volume name: ", sb->s_volume_name);
    log_tr_d("File system state: ", le16_to_cpu(sb->s_state));

    log_tr_s_sec("Mount time: ", le32_to_cpu(sb->s_mtime));
    log_tr_s_sec("Write time: ", le32_to_cpu(sb->s_wtime));
    log_tr_s_sec("Time of last check: ", le32_to_cpu(sb->s_lastcheck));
    {
        uint32_t crc = crc32(~0, (void*)(sb), sizeof(struct ext4_super_block));
        *p_sb_crc = crc;
        res = SUCCESS;
    }
    kfree(sb);

    return res;
}

#endif //PERSISTENT_CBT