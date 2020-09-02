#pragma once

#ifdef PERSISTENT_CBT
int ext4_check_offline_changes(struct block_device* bdev, uint32_t previous_crc);
int ext4_check_unmount_status(struct block_device* bdev, uint32_t* p_sb_crc);
#endif //PERSISTENT_CBT