/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#define MODULE_NAME "blk_snap"
#define SNAP_IMAGE_NAME "blk_snap_image"
#define BLK_SNAP 'V'

enum blk_snap_compat_flags {
	BLK_SNAP_COMPAT_FLAG_LIMIT=64
};

struct blk_snap_version {
	__u16 major;
	__u16 minor;
	__u16 revision;
	__u16 build;
	__u64 compatibility_flags;
	__u8 modification_name[32];
};
/**
 * Linking the product behavior to the version code does not seem to me a very
 * good idea. However, such an ioctl is good for checking that the module has
 * loaded and is responding to requests. 
 *
 * But compatibility flags allows to safely extend the functionality of the
 * module. When the blk_snap kernel module receives new ioctl it will be
 * enough to add a bit.
 * 
 * The name of the modification can be used by the authors of forks and branches
 * of the original module. The module in upstream is considered original and
 * therefore contains an empty string here.
 */
#define IOCTL_BLK_SNAP_VERSION \
	_IOW(BLK_SNAP, 0, struct blk_snap_version)

/**
 * The main functionality of the module is change block tracking (CBT).
 * Next, a number of ioctls will describe the interface for the CBT mechanism.
 */

struct blk_snap_tracker_add {
	__u32 dev_id;	
};
/**
 * This ioctl adds a device for tracking. From now on, all write bio requests
 * will be registered in the CBT table for this device.
 */
#define IOCTL_BLK_SNAP_TRACKER_ADD \
	_IOW(BLK_SNAP, 1, struct blk_snap_tracker_add)

struct blk_snap_tracker_remove {
	__u32 dev_id;	
};
/**
 * Removes the device from tracking changes.
 */
#define IOCTL_BLK_SNAP_TRACKER_REMOVE \
	_IOW(BLK_SNAP, 2, struct blk_snap_tracker_remove)

struct blk_snap_cbt_info {
	__u32 dev_id;
	__u32 block_size;
	__u64 device_capacity;
	__u32 cbt_map_size;
	__u8 generationId[16];
	__u8 snap_number;
};
struct blk_snap_tracker_collect {
	__u32 count;
	struct blk_snap_cbt_info *cbt_info_array;
};
/**
 * Getting information about of all devices under tracking.
 * This ioctl duplicates displays the same information that the module outputs
 * to sysfs for each device under tracking.
 */
#define IOCTL_BLK_SNAP_TRACKER_COLLECT \
	_IOW(BLK_SNAP, 3, struct blk_snap_tracker_collect)

struct blk_snap_tracker_read_cbt_bitmap {
	__u32 dev_id;
	__u32 offset;
	__u32 length;
	__u8 *buff;
};
/**
 * This ioctl allows to read the table of changes. Sysfs also has a file that
 * allows to read this table.
 */
#define IOCTL_BLK_SNAP_TRACKER_READ_CBT_BITMAP \
	_IOR(BLK_SNAP, 4, struct blk_snap_tracker_read_cbt_bitmap)

struct blk_snap_block_range {
	__u64 sector_offset;
	__u64 sector_count;
};

struct blk_snap_tracker_mark_dirty_blocks {
	__u32 dev_id;
	__u32 count;
	struct blk_snap_block_range *dirty_blocks_array;
};
/**
 * There are cases when some blocks need to be marked as changed.
 * This ioctl allows to do this.
 */
#define IOCTL_BLK_SNAP_TRACKER_MARK_DIRTY_BLOCKS \
	_IOR(BLK_SNAP, 5, struct blk_snap_tracker_mark_dirty_blocks)

/**
 * Next, there will be a description of the interface for working with snapshots.
 */

struct blk_snap_snapshot_create {
	__u32 count;		/* in */
	__u32 *dev_id_array;	/* in */
	__u8 id[16];		/* output */
};
/**
 * This ioctl creates a snapshot structure in memory and allocates an identifier
 * for it. Further interaction with the snapshot is possible by this identifier.
 * Several snapshots can be created at the same time, but with the condition
 * that one block device can only be included in one snapshot.
 */
#define IOCTL_BLK_SNAP_SNAPSHOT_CREATE \
	_IOW(BLK_SNAP, 6, struct blk_snap_snapshot_create)

struct blk_snap_snapshot_destroy {
	__u8 id[16];
};
/**
 * Destroys all snapshot structures and releases all its allocated resources.
 */
#define IOCTL_BLK_SNAP_SNAPSHOT_DESTROY \
	_IOR(BLK_SNAP, 7, struct blk_snap_snapshot_destroy)

struct blk_snap_snapshot_append_storage {
	__u8 id[16];
	__u32 dev_id;
	__u32 range_count;
	struct blk_snap_block_range *ranges;

}
/**
 * The snapshot difference storage can be set before creating the snapshot
 * images themselves, and after. This allows to dynamically expand the
 * difference storage while holding the snapshot.
 */
#define IOCTL_BLK_SNAP_SNAPSHOT_APPEND_STORAGE \
	_IOW(BLK_SNAP, 8, struct blk_snap_snapshot_append_storage)

struct blk_snap_snapshot_take {
	__u8 id[16];
};
/**
 * This ioctl creates snapshot images of block devices and switches CBT tables.
 * Before this call, the snapshot must be created, and the areas of block
 * devices should be added to the difference storage.
 */
#define IOCTL_BLK_SNAP_SNAPSHOT_TAKE \
	_IOR(BLK_SNAP, 9, struct blk_snap_snapshot_take)

struct blk_snap_snapshot_event {
	__u8 id[16];		/* in */
	__u32 timeout_ms;	/* in */
	__u32 time_label;	/* other fields for output */
	__u32 code;
	__u8  data[0];		/* up to PAGE_SIZE - sizeof(struct blk_snap_snapshot_event) */
};
/**
 * While holding the snapshot, the kernel module can transmit information about
 * changes in its state in the form of events to the user level.
 * It is very important to receive these events as quickly as possible, so the
 * user's thread is in a state of interruptable sleep.
 */
#define IOCTL_BLK_SNAP_SNAPSHOT_WAIT_EVENT \
	_IOW(BLK_SNAP, 10, PAGE_SIZE)

struct blk_snap_event_low_free_space {
	__u64 sectors_left;
};
/**
 * If the free space in the difference storage is reduced to the specified
 * limit, the module generates an event about this.
 */
#define BLK_SNAP_EVENT_LOW_FREE_SPACE 0x41

struct blk_snap_event_overflow {
	__u32 image_dev_id;
};
/**
 * If a chunk could not be allocated when trying to save data to difference
 * storage, this event is generated.
 * However, this does not mean that the backup process was interrupted with an
 * error. If the snapshot image has been read to the end by this time, the
 * backup process is considered successful.
 */
#define BLK_SNAP_EVENT_OVERFLOW 0x42
/**
 * If the event reader thread is still waiting for an event when the snapshot
 * is destroyed, it will receive this event.
 * Subsequent ioctl to read the event will end with the error code -EINVAL,
 * since the snapshot is destroyed.
 */
#define BLK_SNAP_EVENT_TERMINATE 0x43

struct blk_snap_image_info {
	
	__u32 original_dev_id;
	__u32  snapshot_dev_id;
};
struct blk_snap_snapshot_collect_images {
	__u8 id[16];
	__u32 count;
	struct blk_snap_image_info *image_info_array;

};
/**
 * While holding the snapshot, this ioctl allows you to get a table of
 * correspondences of the original devices and their snapshot images.
 * This information can also be obtained from files from sysfs.
 */
#define IOCTL_BLK_SNAP_SNAPSHOT_COLLECT_IMAGES \
	_IOW(BLK_SNAP, 11, struct blk_snap_snapshot_collect_images)
