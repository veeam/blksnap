/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <linux/types.h>
#include <linux/uuid.h>

#define MODULE_NAME "blk_snap"
#define SNAPIMAGE_NAME "blk_snap_image"
#define BLK_SNAP 'V'

enum blk_snap_compat_flags {
	BLK_SNAP_COMPAT_FLAG_LIMIT=64
};

/**
 * struct blk_snap_version - Result for &IOCTL_BLK_SNAP_VERSION control.
 *
 * @major:
 * 	?
 */
struct blk_snap_version {
	__u16 major;			/* output */
	__u16 minor;			/* output */
	__u16 revision;			/* output */
	__u16 build;			/* output */
	__u64 compatibility_flags;	/* output */
	__u8 mod_name[32];		/* output */
};
/**
 * IOCTL_BLK_SNAP_VERSION - Get version and compatibility flags.
 *
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

/*
 * The main functionality of the module is change block tracking (CBT).
 * Next, a number of ioctls will describe the interface for the CBT mechanism.
 */

/**
 * struct blk_snap_tracker_remove - Input argument for
 * 	&IOCTL_BLK_SNAP_TRACKER_REMOVE control.
 * @dev_id:
 * 	Device ID in dev_t format.
 */
struct blk_snap_tracker_remove {
	dev_t dev_id;		/* input */
};
/**
 * IOCTL_BLK_SNAP_TRACKER_REMOVE - Remove device from tracking.
 *
 * Removes the device from tracking changes.
 * Adding a device for tracking is performed when creating a snapshot
 * that includes this device.
 */
#define IOCTL_BLK_SNAP_TRACKER_REMOVE \
	_IOW(BLK_SNAP, 1, struct blk_snap_tracker_remove)

/**
 * struct blk_snap_cbt_info - Information about change tracking for a block
 * 	device.
 */
struct blk_snap_cbt_info {
	dev_t dev_id;
	__u32 blk_size;
	__u64 device_capacity;
	__u32 blk_count;
	__u8 generationId[16];
	__u8 snap_number;
};
/**
 * struct blk_snap_tracker_collect - Argument for
 * 	&IOCTL_BLK_SNAP_TRACKER_COLLECT control.
 */
struct blk_snap_tracker_collect {
	__u32 count;					/* input/output */
	struct blk_snap_cbt_info *cbt_info_array;	/* allocated buffer for output */
};
/**
 * IOCTL_BLK_SNAP_TRACKER_COLLECT - Collect all tracked device.
 *
 * Getting information about of all devices under tracking.
 * This ioctl returns the same information that the module outputs
 * to sysfs for each device under tracking.
 */
#define IOCTL_BLK_SNAP_TRACKER_COLLECT \
	_IOW(BLK_SNAP, 2, struct blk_snap_tracker_collect)

/**
 * struct blk_snap_tracker_read_cbt_bitmap - Argument for
 * 	&IOCTL_BLK_SNAP_TRACKER_READ_CBT_MAP control.
 */
struct blk_snap_tracker_read_cbt_bitmap {
	dev_t dev_id;		/* input */
	__u32 offset;		/* input */
	__u32 length;		/* input */
	__u8 *buff;		/* allocated buffer for output */
};
/**
 * IOCTL_BLK_SNAP_TRACKER_READ_CBT_MAP - Read CBT map.
 *
 * This ioctl allows to read the table of changes. Sysfs also has a file that
 * allows to read this table.
 */
#define IOCTL_BLK_SNAP_TRACKER_READ_CBT_MAP \
	_IOR(BLK_SNAP, 3, struct blk_snap_tracker_read_cbt_bitmap)

/**
 * struct blk_snap_block_range - Element of array for
 * 	&struct blk_snap_tracker_mark_dirty_blocks.
 *
 */
struct blk_snap_block_range {
	__u64 sector_offset;
	__u64 sector_count;
};
/**
 * struct blk_snap_tracker_mark_dirty_blocks - Argument for
 * 	&IOCTL_BLK_SNAP_TRACKER_MARK_DIRTY_BLOCKS control.
 */
struct blk_snap_tracker_mark_dirty_blocks {
	dev_t dev_id;						/* input */
	__u32 count;						/* input */
	struct blk_snap_block_range *dirty_blocks_array;	/* input */
};
/**
 * IOCTL_BLK_SNAP_TRACKER_MARK_DIRTY_BLOCKS - Set dirty blocks in CBT map.
 *
 * There are cases when some blocks need to be marked as changed.
 * This ioctl allows to do this.
 */
#define IOCTL_BLK_SNAP_TRACKER_MARK_DIRTY_BLOCKS \
	_IOR(BLK_SNAP, 4, struct blk_snap_tracker_mark_dirty_blocks)

/*
 * Next, there will be a description of the interface for working with snapshots.
 */

/**
 * struct blk_snap_snapshot_create - Argument for
 * 	&IOCTL_BLK_SNAP_SNAPSHOT_CREATE control.
 */
struct blk_snap_snapshot_create {
	__u32 count;		/* input */
	dev_t *dev_id_array;	/* input */
	uuid_t id;		/* output */
};
/**
 * This ioctl creates a snapshot structure in memory and allocates an identifier
 * for it. Further interaction with the snapshot is possible by this identifier.
 * Several snapshots can be created at the same time, but with the condition
 * that one block device can only be included in one snapshot.
 */
#define IOCTL_BLK_SNAP_SNAPSHOT_CREATE \
	_IOW(BLK_SNAP, 5, struct blk_snap_snapshot_create)

/**
 * struct blk_snap_snapshot_destroy - Argument for
 * 	&IOCTL_BLK_SNAP_SNAPSHOT_DESTROY control.
 */
struct blk_snap_snapshot_destroy {
	uuid_t id;	/* input */
};
/**
 * IOCTL_BLK_SNAP_SNAPSHOT_DESTROY - Release and destroy snapshot.
 *
 * Destroys all snapshot structures and releases all its allocated resources.
 */
#define IOCTL_BLK_SNAP_SNAPSHOT_DESTROY \
	_IOR(BLK_SNAP, 6, struct blk_snap_snapshot_destroy)

/**
 * struct blk_snap_snapshot_append_storage - Argument for
 * 	&IOCTL_BLK_SNAP_SNAPSHOT_APPEND_STORAGE control.
 */
struct blk_snap_snapshot_append_storage {
	uuid_t id;				/* input */
	dev_t dev_id;				/* input */
	__u32 count;				/* input */
	struct blk_snap_block_range *ranges;	/* input */

};
/**
 * IOCTL_BLK_SNAP_SNAPSHOT_APPEND_STORAGE - Append storage to difference
 * 	storage of snapshot.
 *
 * The snapshot difference storage can be set before creating the snapshot
 * images themselves, and after. This allows to dynamically expand the
 * difference storage while holding the snapshot.
 */
#define IOCTL_BLK_SNAP_SNAPSHOT_APPEND_STORAGE \
	_IOW(BLK_SNAP, 7, struct blk_snap_snapshot_append_storage)

/**
 * struct blk_snap_snapshot_take - Argument for
 * 	&IOCTL_BLK_SNAP_SNAPSHOT_TAKE control.
 */
struct blk_snap_snapshot_take {
	uuid_t id;	/* input */
};
/**
 * IOCTL_BLK_SNAP_SNAPSHOT_TAKE - Take snapshot.
 *
 * This ioctl creates snapshot images of block devices and switches CBT tables.
 * The snapshot must be created before this call, and the areas of block
 * devices should be added to the difference storage.
 */
#define IOCTL_BLK_SNAP_SNAPSHOT_TAKE \
	_IOR(BLK_SNAP, 8, struct blk_snap_snapshot_take)

 /**
  * struct blk_snap_image_info - Associates the original device in the snapshot
  * 	and the corresponding snapshot image.
  *
  */
struct blk_snap_image_info {
	dev_t orig_dev_id;
	dev_t image_dev_id;
};
/**
 * struct blk_snap_snapshot_collect_images - Argument for
 * 	&IOCTL_BLK_SNAP_SNAPSHOT_COLLECT_IMAGES control.
 */
struct blk_snap_snapshot_collect_images {
	uuid_t id;					/* input */
	__u32 count;					/* input/output */
	struct blk_snap_image_info* image_info_array;	/* allocated buffer for output */

};
/**
 * IOCTL_BLK_SNAP_SNAPSHOT_COLLECT_IMAGES - Get collection of devices and their
 * 	snapshot images.
 *
 * While holding the snapshot, this ioctl allows you to get a table of
 * correspondences of the original devices and their snapshot images.
 * This information can also be obtained from files from sysfs.
 */
#define IOCTL_BLK_SNAP_SNAPSHOT_COLLECT_IMAGES \
	_IOW(BLK_SNAP, 10, struct blk_snap_snapshot_collect_images)

/**
 * struct blk_snap_snapshot_event - Argument for
 * 	&IOCTL_BLK_SNAP_SNAPSHOT_WAIT_EVENT control.
 */
struct blk_snap_snapshot_event {
	uuid_t id;		/* input */
	__u32 timeout_ms;	/* input */
	__u32 time_label;	/* output */
	__u32 code;		/* output */
	__u8  data[4096 - 28];	/* output, up to PAGE_SIZE */
};
/**
 * IOCTL_BLK_SNAP_SNAPSHOT_WAIT_EVENT - Wait and get event from snapshot.
 *
 * While holding the snapshot, the kernel module can transmit information about
 * changes in its state in the form of events to the user level.
 * It is very important to receive these events as quickly as possible, so the
 * user's thread is in a state of interruptable sleep.
 */
#define IOCTL_BLK_SNAP_SNAPSHOT_WAIT_EVENT \
	_IOW(BLK_SNAP, 9, struct blk_snap_snapshot_event)

/**
 * BLK_SNAP_EVENT_MSG_ERROR - Unused
 */
//#define BLK_SNAP_EVENT_MSG_ERROR 0x30

/**
 * Unused.
 */
//#define BLK_SNAP_EVENT_MSG_WARNING 0x31

/**
 * struct blk_snap_event_low_free_space - Data for
 * 	&BLK_SNAP_EVENT_LOW_FREE_SPACE event.
 */
struct blk_snap_event_low_free_space {
	__u64 requested_nr_sect;
};
/**
 * BLK_SNAP_EVENT_LOW_FREE_SPACE - Low free space in difference storage event.
 *
 * If the free space in the difference storage is reduced to the specified
 * limit, the module generates this event.
 */
#define BLK_SNAP_EVENT_LOW_FREE_SPACE 0x41

/**
 * struct blk_snap_event_corrupted - Data for &BLK_SNAP_EVENT_CORRUPTED event.
 *
 */
struct blk_snap_event_corrupted {
	dev_t orig_dev_id;
	__s32 errno;
};
/**
 * BLK_SNAP_EVENT_CORRUPTED - Snapshot image is corrupted event.
 *
 * If a chunk could not be allocated when trying to save data to difference
 * storage, this event is generated.
 * However, this does not mean that the backup process was interrupted with an
 * error. If the snapshot image has been read to the end by this time, the
 * backup process is considered successful.
 */
#define BLK_SNAP_EVENT_CORRUPTED 0x42

/**
 * BLK_SNAP_EVENT_TERMINATE - Snapshot terminated event.
 *
 * If the event reader thread is still waiting for an event when the snapshot
 * is destroyed, it will receive this event.
 * Subsequent ioctl to read the event will end with the error code -EINVAL,
 * since the snapshot is destroyed.
 */
#define BLK_SNAP_EVENT_TERMINATE 0x43
