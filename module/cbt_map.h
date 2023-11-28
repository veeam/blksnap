/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023 Veeam Software Group GmbH */
#ifndef __BLKSNAP_CBT_MAP_H
#define __BLKSNAP_CBT_MAP_H

#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/uuid.h>
#include <linux/spinlock.h>
#include <linux/blkdev.h>

struct blksnap_sectors;

/**
 * struct cbt_map - The table of changes for a block device.
 *
 * @locker:
 *	Locking for atomic modification of structure members.
 * @blk_size_shift:
 *	The power of 2 used to specify the change tracking block size.
 * @blk_count:
 *	The number of change tracking blocks.
 * @device_capacity:
 *	The actual capacity of the device.
 * @read_map:
 *	A table of changes available for reading. This is the table that can
 *	be read after taking a snapshot.
 * @write_map:
 *	The current table for tracking changes.
 * @snap_number_active:
 *	The current sequential number of changes. This is the number that is
 *	written to the current table when the block data changes.
 * @snap_number_previous:
 *	The previous sequential number of changes. This number is used to
 *	identify the blocks that were changed between the penultimate snapshot
 *	and the last snapshot.
 * @generation_id:
 *	UUID of the generation of changes.
 * @is_corrupted:
 *	A flag that the change tracking data is no longer reliable.
 *
 * The change block tracking map is a byte table. Each byte stores the
 * sequential number of changes for one block. To determine which blocks have
 * changed since the previous snapshot with the change number 4, it is enough
 * to find all bytes with the number more than 4.
 *
 * Since one byte is allocated to track changes in one block, the change table
 * is created again at the 255th snapshot. At the same time, a new unique
 * generation identifier is generated. Tracking changes is possible only for
 * tables of the same generation.
 *
 * There are two tables on the change block tracking map. One is available for
 * reading, and the other is available for writing. At the moment of taking
 * a snapshot, the tables are synchronized. The user's process, when calling
 * the corresponding ioctl, can read the readable table. At the same time, the
 * change tracking mechanism continues to work with the writable table.
 *
 * To provide the ability to mount a snapshot image as writeable, it is
 * possible to make changes to both of these tables simultaneously.
 *
 */
struct cbt_map {
	spinlock_t locker;

	size_t blk_size_shift;
	size_t blk_count;
	sector_t device_capacity;

	unsigned char *read_map;
	unsigned char *write_map;

	unsigned long snap_number_active;
	unsigned long snap_number_previous;
	uuid_t generation_id;

	bool is_corrupted;
};

struct cbt_map *cbt_map_create(struct block_device *bdev);
int cbt_map_reset(struct cbt_map *cbt_map, sector_t device_capacity);

void cbt_map_destroy(struct cbt_map *cbt_map);

void cbt_map_switch(struct cbt_map *cbt_map);
int cbt_map_set(struct cbt_map *cbt_map, sector_t sector_start,
		sector_t sector_cnt);
int cbt_map_set_both(struct cbt_map *cbt_map, sector_t sector_start,
		     sector_t sector_cnt);

#endif /* __BLKSNAP_CBT_MAP_H */
