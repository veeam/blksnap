/*
 * Copyright (C) 2022 Veeam Software Group GmbH <https://www.veeam.com/contacts.html>
 *
 * This file is part of libblksnap
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Lesser Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once
#include <linux/types.h>
#include <linux/uuid.h>

#define BLK_SNAP_MODULE_NAME "blksnap"
#define BLK_SNAP_IMAGE_NAME "blksnap-image"
#define BLK_SNAP 'V'

#ifdef BLK_SNAP_MODIFICATION
#define IOCTL_MOD 32
#endif

enum blk_snap_ioctl {
	/*
	 * Service controls
	 */
	blk_snap_ioctl_version,
	/*
	 * contols for tracking
	 */
	blk_snap_ioctl_tracker_remove,
	blk_snap_ioctl_tracker_collect,
	blk_snap_ioctl_tracker_read_cbt_map,
	blk_snap_ioctl_tracker_mark_dirty_blocks,
	/*
	 * Snapshot contols
	 */
	blk_snap_ioctl_snapshot_create,
	blk_snap_ioctl_snapshot_destroy,
	blk_snap_ioctl_snapshot_append_storage,
	blk_snap_ioctl_snapshot_take,
	blk_snap_ioctl_snapshot_collect,
	blk_snap_ioctl_snapshot_collect_images,
	blk_snap_ioctl_snapshot_wait_event,
	blk_snap_ioctl_end,
	/*
	 * Additional controls for any standalone modification
	 */
#ifdef BLK_SNAP_MODIFICATION
	blk_snap_ioctl_mod = IOCTL_MOD,
#ifdef BLK_SNAP_DEBUG_SECTOR_STATE
	blk_snap_ioctl_get_sector_state,
#endif
	blk_snap_ioctl_end_mod
#endif
};

/**
 * struct blk_snap_version - Result for &IOCTL_BLK_SNAP_VERSION control.
 * @major:
 *	Version major part.
 * @minor:
 *	Version minor part.
 * @revision:
 *	Revision number.
 * @build:
 *	Build number.
 */
struct blk_snap_version {
	__u16 major;
	__u16 minor;
	__u16 revision;
	__u16 build;
};
/**
 * IOCTL_BLK_SNAP_VERSION - Get version and compatibility flags.
 *
 * Linking the product behavior to the version code does not seem to me a very
 * good idea. Version is only for logs.
 */
#define IOCTL_BLK_SNAP_VERSION                                                 \
	_IOW(BLK_SNAP, blk_snap_ioctl_version, struct blk_snap_version)

#ifdef BLK_SNAP_MODIFICATION

enum blk_snap_compat_flags {
#ifdef BLK_SNAP_DEBUG_SECTOR_STATE
	blk_snap_compat_flag_debug_sector_state,
#endif
	/*
	 * Reserved for new features
	 */
	blk_snap_compat_flags_end
};
static_assert(blk_snap_compat_flags_end <= 64,
	      "There are too many compatibility flags.");

#define BLK_SNAP_MOD_NAME_LIMIT 31

/**
 * struct blk_snap_modification - Result for &IOCTL_BLK_SNAP_VERSION control.
 *
 * @compatibility_flags:
 *      [TBD] Reserved for new modification specific features.
 * @name:
 *      Name of modification of the module blksnap (fork name, for example).
 *      It's should be empty string for upstream module.
 */
struct blk_snap_mod {
	__u64 compatibility_flags;
	__u8 name[BLK_SNAP_MOD_NAME_LIMIT + 1];
};

/**
 * IOCTL_BLK_SNAP_MOD - Get modification name and compatibility flags.
 *
 * Linking the product behavior to the version code does not seem to me a very
 * good idea. However, such an ioctl is good for checking that the module has
 * loaded and is responding to requests.
 *
 * The compatibility flags allows to safely extend the functionality of the
 * module. When the blk_snap kernel module receives new ioctl it will be
 * enough to add a bit.
 *
 * The name of the modification can be used by the authors of forks and branches
 * of the original module. The module in upstream have not any modifications.
 */
#define IOCTL_BLK_SNAP_MOD                                                     \
	_IOW(BLK_SNAP, blk_snap_ioctl_mod, struct blk_snap_mod)
#endif

/*
 * The main functionality of the module is change block tracking (CBT).
 * Next, a number of ioctls will describe the interface for the CBT mechanism.
 */

/**
 * struct blk_snap_dev_t - Block device ID.
 * @mj:
 *      Device ID major part.
 * @mn:
 *      Device ID minor part.
 *
 * In user-space and in kernel-space, block devices are encoded differently.
 * We have to enter own type to guarantee the correct transmission of the major
 * and minor parts.
 */
struct blk_snap_dev_t {
	__u32 mj;
	__u32 mn;
};

/**
 * struct blk_snap_tracker_remove - Input argument for
 *      &IOCTL_BLK_SNAP_TRACKER_REMOVE control.
 * @dev_id:
 *      Device ID.
 */
struct blk_snap_tracker_remove {
	struct blk_snap_dev_t dev_id;
};
/**
 * IOCTL_BLK_SNAP_TRACKER_REMOVE - Remove device from tracking.
 *
 * Removes the device from tracking changes.
 * Adding a device for tracking is performed when creating a snapshot
 * that includes this device.
 */
#define IOCTL_BLK_SNAP_TRACKER_REMOVE                                          \
	_IOW(BLK_SNAP, blk_snap_ioctl_tracker_remove,                          \
	     struct blk_snap_tracker_remove)

/**
 * struct blk_snap_cbt_info - Information about change tracking for a block
 *      device.
 * @dev_id:
 *      Device ID.
 * @blk_size:
 *      Block size in bytes.
 * @device_capacity:
 *      Device capacity in bytes.
 * @blk_count:
 *      Number of blocks.
 * @generation_id:
 *      Change tracking generation unique identification.
 * @snap_number:
 *      Current changes number.
 */
struct blk_snap_cbt_info {
	struct blk_snap_dev_t dev_id;
	__u32 blk_size;
	__u64 device_capacity;
	__u32 blk_count;
	uuid_t generation_id;
	__u8 snap_number;
};
/**
 * struct blk_snap_tracker_collect - Argument for
 *      &IOCTL_BLK_SNAP_TRACKER_COLLECT control.
 * @count:
 *      Size of @cbt_info_array in the number of &struct blk_snap_cbt_info.
 *      If @cbt_info_array has not enough space, it will contain the required size of the array.
 * @cbt_info_array:
 *      Pointer to the array for output.
 */
struct blk_snap_tracker_collect {
	__u32 count;
	struct blk_snap_cbt_info *cbt_info_array;
};
/**
 * IOCTL_BLK_SNAP_TRACKER_COLLECT - Collect all tracked device.
 *
 * Getting information about of all devices under tracking.
 * This ioctl returns the same information that the module outputs
 * to sysfs for each device under tracking.
 */
#define IOCTL_BLK_SNAP_TRACKER_COLLECT                                         \
	_IOW(BLK_SNAP, blk_snap_ioctl_tracker_collect,                         \
	     struct blk_snap_tracker_collect)

/**
 * struct blk_snap_tracker_read_cbt_bitmap - Argument for
 *      &IOCTL_BLK_SNAP_TRACKER_READ_CBT_MAP control.
 * @dev_id:
 *      Device ID.
 * @offset:
 *      Offset from the beginning of CBT bitmap in bytes.
 * @length:
 *      Size of @buff in bytes.
 * @buff:
 *      Pointer to the buffer for output.
 */
struct blk_snap_tracker_read_cbt_bitmap {
	struct blk_snap_dev_t dev_id;
	__u32 offset;
	__u32 length;
	__u8 *buff;
};
/**
 * IOCTL_BLK_SNAP_TRACKER_READ_CBT_MAP - Read CBT map.
 *
 * This ioctl allows to read the table of changes. Sysfs also has a file that
 * allows to read this table.
 */
#define IOCTL_BLK_SNAP_TRACKER_READ_CBT_MAP                                    \
	_IOR(BLK_SNAP, blk_snap_ioctl_tracker_read_cbt_map,                    \
	     struct blk_snap_tracker_read_cbt_bitmap)

/**
 * struct blk_snap_block_range - Element of array for
 *      &struct blk_snap_tracker_mark_dirty_blocks.
 * @sector_offset:
 *      Offset from the beginning of the disk in sectors.
 * @sector_count:
 *      Number of sectors.
 */
struct blk_snap_block_range {
	__u64 sector_offset;
	__u64 sector_count;
};
/**
 * struct blk_snap_tracker_mark_dirty_blocks - Argument for
 *      &IOCTL_BLK_SNAP_TRACKER_MARK_DIRTY_BLOCKS control.
 * @dev_id:
 *      Device ID.
 * @count:
 *      Size of @dirty_blocks_array in the number of &struct blk_snap_block_range.
 * @dirty_blocks_array:
 *      Pointer to the array of &struct blk_snap_block_range.
 */
struct blk_snap_tracker_mark_dirty_blocks {
	struct blk_snap_dev_t dev_id;
	__u32 count;
	struct blk_snap_block_range *dirty_blocks_array;
};
/**
 * IOCTL_BLK_SNAP_TRACKER_MARK_DIRTY_BLOCKS - Set dirty blocks in CBT map.
 *
 * There are cases when some blocks need to be marked as changed.
 * This ioctl allows to do this.
 */
#define IOCTL_BLK_SNAP_TRACKER_MARK_DIRTY_BLOCKS                               \
	_IOR(BLK_SNAP, blk_snap_ioctl_tracker_mark_dirty_blocks,               \
	     struct blk_snap_tracker_mark_dirty_blocks)

/*
 * Next, there will be a description of the interface for working with snapshots.
 */

/**
 * struct blk_snap_snapshot_create - Argument for
 *      &IOCTL_BLK_SNAP_SNAPSHOT_CREATE control.
 * @count:
 *      Size of @dev_id_array in the number of &struct blk_snap_dev_t.
 * @dev_id_array:
 *      Pointer to the array of &struct blk_snap_dev_t.
 * @id:
 *      Return ID of the created snapshot.
 */
struct blk_snap_snapshot_create {
	__u32 count;
	struct blk_snap_dev_t *dev_id_array;
	uuid_t id;
};
/**
 * This ioctl creates a snapshot structure in memory and allocates an identifier
 * for it. Further interaction with the snapshot is possible by this identifier.
 * Several snapshots can be created at the same time, but with the condition
 * that one block device can only be included in one snapshot.
 */
#define IOCTL_BLK_SNAP_SNAPSHOT_CREATE                                         \
	_IOW(BLK_SNAP, blk_snap_ioctl_snapshot_create,                         \
	     struct blk_snap_snapshot_create)

/**
 * struct blk_snap_snapshot_destroy - Argument for
 *      &IOCTL_BLK_SNAP_SNAPSHOT_DESTROY control.
 * @id:
 *      Snapshot ID.
 */
struct blk_snap_snapshot_destroy {
	uuid_t id;
};
/**
 * IOCTL_BLK_SNAP_SNAPSHOT_DESTROY - Release and destroy snapshot.
 *
 * Destroys all snapshot structures and releases all its allocated resources.
 */
#define IOCTL_BLK_SNAP_SNAPSHOT_DESTROY                                        \
	_IOR(BLK_SNAP, blk_snap_ioctl_snapshot_destroy,                        \
	     struct blk_snap_snapshot_destroy)

/**
 * struct blk_snap_snapshot_append_storage - Argument for
 *      &IOCTL_BLK_SNAP_SNAPSHOT_APPEND_STORAGE control.
 * @id:
 *      Snapshot ID.
 * @dev_id:
 *      Device ID.
 * @count:
 *      Size of @ranges in the number of &struct blk_snap_block_range.
 * @ranges:
 *      Pointer to the array of &struct blk_snap_block_range.
 */
struct blk_snap_snapshot_append_storage {
	uuid_t id;
	struct blk_snap_dev_t dev_id;
	__u32 count;
	struct blk_snap_block_range *ranges;
};
/**
 * IOCTL_BLK_SNAP_SNAPSHOT_APPEND_STORAGE - Append storage to difference
 *      storage of snapshot.
 *
 * The snapshot difference storage can be set before creating the snapshot
 * images themselves, and after. This allows to dynamically expand the
 * difference storage while holding the snapshot.
 */
#define IOCTL_BLK_SNAP_SNAPSHOT_APPEND_STORAGE                                 \
	_IOW(BLK_SNAP, blk_snap_ioctl_snapshot_append_storage,                 \
	     struct blk_snap_snapshot_append_storage)

/**
 * struct blk_snap_snapshot_take - Argument for
 *      &IOCTL_BLK_SNAP_SNAPSHOT_TAKE control.
 * @id:
 *      Snapshot ID.
 */
struct blk_snap_snapshot_take {
	uuid_t id;
};
/**
 * IOCTL_BLK_SNAP_SNAPSHOT_TAKE - Take snapshot.
 *
 * This ioctl creates snapshot images of block devices and switches CBT tables.
 * The snapshot must be created before this call, and the areas of block
 * devices should be added to the difference storage.
 */
#define IOCTL_BLK_SNAP_SNAPSHOT_TAKE                                           \
	_IOR(BLK_SNAP, blk_snap_ioctl_snapshot_take,                           \
	     struct blk_snap_snapshot_take)

/**
 * struct blk_snap_snapshot_collect - Argument for
 *      &IOCTL_BLK_SNAP_SNAPSHOT_COLLECT control.
 * @count:
 *      Size of @ids in the number of &uuid_t.
 *      If @ids has not enough space, it will contain the required
 *      size of the array.
 * @ids:
 *      Pointer to the array with snapshot ID for output. If the pointer is
 *      zero, the ioctl simply returns the number of active snapshots in &count.
 *
 */
struct blk_snap_snapshot_collect {
	__u32 count;
	uuid_t *ids;
};
/**
 * IOCTL_BLK_SNAP_SNAPSHOT_COLLECT - Get collection of created snapshots.
 *
 * This information can also be obtained from files from sysfs.
 */
#define IOCTL_BLK_SNAP_SNAPSHOT_COLLECT                                        \
	_IOW(BLK_SNAP, blk_snap_ioctl_snapshot_collect,                        \
	     struct blk_snap_snapshot_collect)
/**
 * struct blk_snap_image_info - Associates the original device in the snapshot
 *      and the corresponding snapshot image.
 * @orig_dev_id:
 *      Device ID.
 * @image_dev_id:
 *      Image ID.
 */
struct blk_snap_image_info {
	struct blk_snap_dev_t orig_dev_id;
	struct blk_snap_dev_t image_dev_id;
};
/**
 * struct blk_snap_snapshot_collect_images - Argument for
 *      &IOCTL_BLK_SNAP_SNAPSHOT_COLLECT_IMAGES control.
 * @id:
 *      Snapshot ID.
 * @count:
 *      Size of @image_info_array in the number of &struct blk_snap_image_info.
 *      If @image_info_array has not enough space, it will contain the required
 *      size of the array.
 * @image_info_array:
 *      Pointer to the array for output.
 */
struct blk_snap_snapshot_collect_images {
	uuid_t id;
	__u32 count;
	struct blk_snap_image_info *image_info_array;
};
/**
 * IOCTL_BLK_SNAP_SNAPSHOT_COLLECT_IMAGES - Get collection of devices and his
 *      snapshot images.
 *
 * While holding the snapshot, this ioctl allows you to get a table of
 * correspondences of the original devices and their snapshot images.
 * This information can also be obtained from files from sysfs.
 */
#define IOCTL_BLK_SNAP_SNAPSHOT_COLLECT_IMAGES                                 \
	_IOW(BLK_SNAP, blk_snap_ioctl_snapshot_collect_images,                 \
	     struct blk_snap_snapshot_collect_images)

enum blk_snap_event_codes {
	/**
	 * Low free space in difference storage event.
	 *
	 * If the free space in the difference storage is reduced to the
	 * specified limit, the module generates this event.
	 */
	blk_snap_event_code_low_free_space,
	/**
	 * Snapshot image is corrupted event.
	 *
	 * If a chunk could not be allocated when trying to save data to
	 * difference storage, this event is generated.
	 * However, this does not mean that the backup process was interrupted
	 * with an error. If the snapshot image has been read to the end by this
	 * time, the backup process is considered successful.
	 */
	blk_snap_event_code_corrupted,
};

/**
 * struct blk_snap_snapshot_event - Argument for
 *      &IOCTL_BLK_SNAP_SNAPSHOT_WAIT_EVENT control.
 * @id:
 *      Snapshot ID.
 * @timeout_ms:
 *      Timeout for waiting in milliseconds.
 * @time_label:
 *      Timestamp of the received event.
 * @code:
 *      Code of the received event.
 * @data:
 *      The received event body.
 */
struct blk_snap_snapshot_event {
	uuid_t id;
	__u32 timeout_ms;
	__u32 code;
	__s64 time_label;
	__u8 data[4096 - 32];
};
static_assert(
	sizeof(struct blk_snap_snapshot_event) == 4096,
	"The size struct blk_snap_snapshot_event should be equal to the size of the page.");

/**
 * IOCTL_BLK_SNAP_SNAPSHOT_WAIT_EVENT - Wait and get event from snapshot.
 *
 * While holding the snapshot, the kernel module can transmit information about
 * changes in its state in the form of events to the user level.
 * It is very important to receive these events as quickly as possible, so the
 * user's thread is in a state of interruptable sleep.
 */
#define IOCTL_BLK_SNAP_SNAPSHOT_WAIT_EVENT                                     \
	_IOW(BLK_SNAP, blk_snap_ioctl_snapshot_wait_event,                     \
	     struct blk_snap_snapshot_event)

/**
 * struct blk_snap_event_low_free_space - Data for
 *      &blk_snap_event_code_low_free_space event.
 * @requested_nr_sect:
 *      The required number of sectors.
 */
struct blk_snap_event_low_free_space {
	__u64 requested_nr_sect;
};

/**
 * struct blk_snap_event_corrupted - Data for &blk_snap_event_code_corrupted event.
 * @orig_dev_id:
 *      Device ID.
 * @err_code:
 *      Error code.
 */
struct blk_snap_event_corrupted {
	struct blk_snap_dev_t orig_dev_id;
	__s32 err_code;
};

#ifdef BLK_SNAP_DEBUG_SECTOR_STATE

/**
 *
 */
struct blk_snap_sector_state {
	__u8 snap_number_prev;
	__u8 snap_number_curr;
	__u32 chunk_state;
};

struct blk_snap_get_sector_state {
	struct blk_snap_dev_t image_dev_id;
	__u64 sector;
	struct blk_snap_sector_state state;
};

/**
 *
 */
#define IOCTL_BLK_SNAP_GET_SECTOR_STATE                                        \
	_IOW(BLK_SNAP, blk_snap_ioctl_get_sector_state,                        \
	     struct blk_snap_get_sector_state)

#endif
