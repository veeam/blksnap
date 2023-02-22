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
#ifndef _UAPI_LINUX_BLKSNAP_H
#define _UAPI_LINUX_BLKSNAP_H

#include <linux/types.h>
#include <linux/fs.h>

#define BLKSNAP_CTL "blksnap-control"
#define BLKSNAP_IMAGE_NAME "blksnap-image"
#define BLKSNAP 'V'

#ifdef BLKSNAP_MODIFICATION
#define IOCTL_MOD 32
#endif

#ifndef BLKFILTER
/*
 * Should be in fs.h
 */

/**
 *
 *
 */
enum {
	BLKFILTER_CMD_ATTACH,
	BLKFILTER_CMD_DETACH,
	BLKFILTER_CMD_CTL,
};

#define BLKFILTER_NAME_LENGTH   32

/**
 *
 *
 */
struct blkfilter_ctl {
	__s8 name[BLKFILTER_NAME_LENGTH];
	__u32 cmd;
	__u32 optlen;
	void *opt;
};

/**
 * BLKFILTER -
 */
#define BLKFILTER _IOWR(0x12, 129, struct blkfilter_ctl)
#endif

/**
 * DOC: Block filter interface.
 *
 * Control commands that are transmitted through the block device filter interface.
 * See: include/uapi/fs.h#BLKFILTER
 */

enum blkfilter_ctl_blksnap {
	blkfilter_ctl_blksnap_cbtinfo,
	blkfilter_ctl_blksnap_cbtmap,
	blkfilter_ctl_blksnap_cbtdirty,
	blkfilter_ctl_blksnap_snapshotadd,
	blkfilter_ctl_blksnap_snapshotinfo,
};

/**
 * blkfilter_ctl_blksnap_cbtinfo - Get CBT information
 */

/**
 * blkfilter_ctl_blksnap_cbtmap - Read the CBT map.
 *
 * Allows to read the table of changes.
 *
 * The size of the table can be quite large. Thus, the table is read in a loop,
 * in each cycle of which the next offset is set to
 * &blksnap_tracker_read_cbt_bitmap.offset.
 *
 * Return: a count of bytes read if succeeded, negative errno otherwise.
 */

/**
 * blkfilter_ctl_blksnap_cbtdirty - Set dirty blocks in the CBT map.
 *
 * There are cases when some blocks need to be marked as changed.
 * This ioctl allows to do this.
 *
 * Return: 0 if succeeded, negative errno otherwise.
 */

/**
 * blkfilter_ctl_blksnap_snapshotinfo -
 *
 */

#ifndef UUID_SIZE
#define UUID_SIZE 16
#endif

/**
 * struct blksnap_uuid - Unique 16-byte identifier.
 * @b:
 *	An array of 16 bytes.
 */
struct blksnap_uuid {
	__u8 b[UUID_SIZE];
};

/**
 * struct blksnap_cbt_info - Information about change tracking for a block
 *	device.
 * @dev_id:
 *	Device ID.
 * @block_size:
 *	Block size in bytes.
 * @device_capacity:
 *	Device capacity in bytes.
 * @block_count:
 *	Number of blocks.
 * @generation_id:
 *	Unique identifier of change tracking generation.
 * @changes_number:
 *	Current changes number.
 */
struct blksnap_cbtinfo {
	__u64 device_capacity; // ? see BLKGETSIZE64
	__u32 block_size;
	__u32 block_count;
	struct blksnap_uuid generation_id;
	__u8 changes_number;
};

/**
 * struct blksnap_tracker_read_cbt_bitmap - Argument for the
 *	&IOCTL_BLKSNAP_TRACKER_READ_CBT_MAP control.
 * @dev_id:
 *	Device ID.
 * @offset:
 *	Offset from the beginning of the CBT bitmap in bytes.
 * @length:
 *	Size of @buff in bytes.
 * @buffer:
 *	Pointer to the buffer for output.
 */
struct blksnap_cbtmap {
	__u32 offset;
	__u32 length;
	__u8 *buffer;
};

/**
 * struct blksnap_block_range - Element of array for
 *	&struct blksnap_tracker_mark_dirty_blocks.
 * @sector_offset:
 *	Offset from the beginning of the disk in sectors.
 * @sector_count:
 *	Number of sectors.
 */
struct blksnap_sectors {
	__u64 offset;
	__u64 count;
};

/**
 * struct blksnap_tracker_mark_dirty_blocks - Argument for the
 *	&IOCTL_BLKSNAP_TRACKER_MARK_DIRTY_BLOCKS control.
 * @count:
 *	Size of @dirty_blocks_array in the number of
 *	&struct blksnap_block_range.
 * @dirty_blocks_array:
 *	Pointer to the array of &struct blksnap_block_range.
 */
struct blksnap_cbtdirty {
	__u32 count;
	struct blksnap_sectors *dirty_sectors;
};

/**
 * struct blksnap_snapshotadd - Argument for the command
 *	blkfilter_ctl_blksnap_snapshotadd. Allow adding a device to the
 *	snapshot.
 * @id:
 *	ID of the snapshot to which the block device should be added.
 */
struct blksnap_snapshotadd {
	struct blksnap_uuid id;
};

#define IMAGE_DISK_NAME_LEN 32

/**
 * struct blksnap_snapshotinfo - Argument for the command
 *	blkfilter_ctl_blksnap_snapshotinfo. Allow to get a status of the block
 *	device snapshot and image.
 * @error_code:
 *	Zero if there were no errors while holding the snapshot.
 *	The error code -ENOSPC means that while holding the snapshot, a snapshot
 *	overflow situation has occurred. Other error codes mean other reasons
 *	for failure.
 *	The error code is reset when the device is added to a new snapshot.
 * @image:
 *	If the snapshot was taken, it stores the block device name of the
 *	image, or empty string.
 */
struct blksnap_snapshotinfo {
	__s32 error_code;
	__s8 image[IMAGE_DISK_NAME_LEN];
};

/**
 * DOC: Interface for managing snapshots
 *
 * Control commands that are transmitted through the blksnap module interface.
 */
enum blksnap_ioctl {
	blksnap_ioctl_version,
	blksnap_ioctl_snapshot_create,
	blksnap_ioctl_snapshot_destroy,
	blksnap_ioctl_snapshot_append_storage,
	blksnap_ioctl_snapshot_take,
	blksnap_ioctl_snapshot_collect,
	blksnap_ioctl_snapshot_wait_event,
#ifdef BLKSNAP_MODIFICATION
	/*
	 * Additional controls for any standalone modification
	 */
	blksnap_ioctl_mod = IOCTL_MOD,
	blksnap_ioctl_setlog,
	blksnap_ioctl_get_sector_state,
	blksnap_ioctl_end_mod
#endif
};

/**
 * struct blksnap_version - Module version.
 * @major:
 *	Version major part.
 * @minor:
 *	Version minor part.
 * @revision:
 *	Revision number.
 * @build:
 *	Build number. Should be zero.
 */
struct blksnap_version {
	__u16 major;
	__u16 minor;
	__u16 revision;
	__u16 build;
};

/**
 * define IOCTL_BLKSNAP_VERSION - Get module version.
 *
 * The version may increase when the API changes. But linking the user space
 * behavior to the version code does not seem to be a good idea.
 * To ensure backward compatibility, API changes should be made by adding new
 * ioctl without changing the behavior of existing ones. The version should be
 * used for logs.
 *
 * Return: 0 if succeeded, negative errno otherwise.
 */
#define IOCTL_BLKSNAP_VERSION							\
	_IOW(BLKSNAP, blksnap_ioctl_version, struct blksnap_version)


/**
 * define IOCTL_BLKSNAP_SNAPSHOT_CREATE - Create snapshot.
 *
 * Creates a snapshot structure in the memory and allocates an identifier for
 * it. Further interaction with the snapshot is possible by this identifier.
 * A snapshot is created for several block devices at once.
 * Several snapshots can be created at the same time, but with the condition
 * that one block device can only be included in one snapshot.
 *
 * Return: 0 if succeeded, negative errno otherwise.
 */
#define IOCTL_BLKSNAP_SNAPSHOT_CREATE						\
	_IOW(BLKSNAP, blksnap_ioctl_snapshot_create, struct blksnap_uuid)

/**
 * define IOCTL_BLKSNAP_SNAPSHOT_DESTROY - Release and destroy the snapshot.
 *
 * Destroys snapshot with &blksnap_uuid. This leads to the
 * deletion of all block device images of the snapshot. The difference storage
 * is being released. But the change tracker keeps tracking.
 *
 * Return: 0 if succeeded, negative errno otherwise.
 */
#define IOCTL_BLKSNAP_SNAPSHOT_DESTROY						\
	_IOR(BLKSNAP, blksnap_ioctl_snapshot_destroy, struct blksnap_uuid)

/**
 * struct blksnap_snapshot_append_storage - Argument for the
 *	&IOCTL_BLKSNAP_SNAPSHOT_APPEND_STORAGE control.
 * @id:
 *	Snapshot ID.
 * @dev_id:
 *	Device ID.
 * @count:
 *	Size of @ranges in the number of &struct blksnap_sectors.
 * @ranges:
 *	Pointer to the array of &struct blksnap_sectors.
 */
struct blksnap_snapshot_append_storage {
	struct blksnap_uuid id;
	__s8 *bdev_path;
	__u32 bdev_path_size;
	__u32 count;
	struct blksnap_sectors *ranges;
};

/**
 * define IOCTL_BLKSNAP_SNAPSHOT_APPEND_STORAGE - Append storage to the
 *	difference storage of the snapshot.
 *
 * The snapshot difference storage can be set either before or after creating
 * the snapshot images. This allows to dynamically expand the difference
 * storage while holding the snapshot.
 *
 * Return: 0 if succeeded, negative errno otherwise.
 */
#define IOCTL_BLKSNAP_SNAPSHOT_APPEND_STORAGE					\
	_IOW(BLKSNAP, blksnap_ioctl_snapshot_append_storage,			\
	     struct blksnap_snapshot_append_storage)

/**
 * define IOCTL_BLKSNAP_SNAPSHOT_TAKE - Take snapshot.
 *
 * Creates snapshot images of block devices and switches change trackers tables.
 * The snapshot must be created before this call, and the areas of block
 * devices should be added to the difference storage.
 *
 * Return: 0 if succeeded, negative errno otherwise.
 */
#define IOCTL_BLKSNAP_SNAPSHOT_TAKE						\
	_IOR(BLKSNAP, blksnap_ioctl_snapshot_take,				\
	     struct blksnap_uuid)

/**
 * struct blksnap_snapshot_collect - Argument for the
 *	&IOCTL_BLKSNAP_SNAPSHOT_COLLECT control.
 * @count:
 *	Size of &blksnap_snapshot_collect.ids in the number of 16-byte UUID.
 * @ids:
 *	Pointer to the array with the snapshot ID for output.
 */
struct blksnap_snapshot_collect {
	__u32 count;
	struct blksnap_uuid *ids;
};

/**
 * define IOCTL_BLKSNAP_SNAPSHOT_COLLECT - Get collection of created snapshots.
 *
 * Multiple snapshots can be created at the same time. This allows for one
 * system to create backups for different data with a independent schedules.
 *
 * If in &blksnap_snapshot_collect.count is less than required to store the
 * &blksnap_snapshot_collect.ids, the array is not filled, and the ioctl
 * returns the required count for &blksnap_snapshot_collect.ids.
 *
 * So, it is recommended to call the ioctl twice. The first call with an null
 * pointer &blksnap_snapshot_collect.ids and a zero value in
 * &blksnap_snapshot_collect.count. It will set the required array size in
 * &blksnap_snapshot_collect.count. The second call with a pointer
 * &blksnap_snapshot_collect.ids to an array of the required size will allow to
 * get collection of active snapshots.
 *
 * Return: 0 if succeeded, -ENODATA if there is not enough space in the array
 * to store collection of active snapshots, or negative errno otherwise.
 */
#define IOCTL_BLKSNAP_SNAPSHOT_COLLECT						\
	_IOW(BLKSNAP, blksnap_ioctl_snapshot_collect,				\
	     struct blksnap_snapshot_collect)

/**
 * enum blksnap_event_codes - Variants of event codes.
 *
 * @blksnap_event_code_low_free_space:
 *	Low free space in difference storage event.
 *	If the free space in the difference storage is reduced to the specified
 *	limit, the module generates this event.
 * @blksnap_event_code_corrupted:
 *	Snapshot image is corrupted event.
 *	If a chunk could not be allocated when trying to save data to the
 *	difference storage, this event is generated. However, this does not mean
 *	that the backup process was interrupted with an error. If the snapshot
 *	image has been read to the end by this time, the backup process is
 *	considered successful.
 */
enum blksnap_event_codes {
	blksnap_event_code_low_free_space,
	blksnap_event_code_corrupted,
};

/**
 * struct blksnap_snapshot_event - Argument for the
 *	&IOCTL_BLKSNAP_SNAPSHOT_WAIT_EVENT control.
 * @id:
 *	Snapshot ID.
 * @timeout_ms:
 *	Timeout for waiting in milliseconds.
 * @time_label:
 *	Timestamp of the received event.
 * @code:
 *	Code of the received event &enum blksnap_event_codes.
 * @data:
 *	The received event body.
 */
struct blksnap_snapshot_event {
	struct blksnap_uuid id;
	__u32 timeout_ms;
	__u32 code;
	__s64 time_label;
	__s8 data[4096 - 32];
};
static_assert(sizeof(struct blksnap_snapshot_event) == 4096,
	"The size struct blksnap_snapshot_event should be equal to the size of the page.");

/**
 * define IOCTL_BLKSNAP_SNAPSHOT_WAIT_EVENT - Wait and get the event from the
 *	snapshot.
 *
 * While holding the snapshot, the kernel module can transmit information about
 * changes in its state in the form of events to the user level.
 * It is very important to receive these events as quickly as possible, so the
 * user's thread is in the state of interruptable sleep.
 *
 * Return: 0 if succeeded, negative errno otherwise.
 */
#define IOCTL_BLKSNAP_SNAPSHOT_WAIT_EVENT					\
	_IOW(BLKSNAP, blksnap_ioctl_snapshot_wait_event,			\
	     struct blksnap_snapshot_event)

/**
 * struct blksnap_event_low_free_space - Data for the
 *	&blksnap_event_code_low_free_space event.
 * @requested_nr_sect:
 *	The required number of sectors.
 */
struct blksnap_event_low_free_space {
	__u64 requested_nr_sect;
};

/**
 * struct blksnap_event_corrupted - Data for the
 *      &blksnap_event_code_corrupted event.
 *
 * @dev_id_mj:
 *      Major part of original device ID.
 * @dev_id_mn:
 *      Minor part of original device ID.
 * @err_code:
 *      Error code.
 */
struct blksnap_event_corrupted {
        __u32 dev_id_mj;
        __u32 dev_id_mn;
        __s32 err_code;
};


#ifdef BLKSNAP_MODIFICATION
/**
 * @tz_minuteswest:
 *	Time zone offset in minutes.
 *	The system time is in UTC. In order for the module to write local time
 *	to the log, its offset should be specified.
 * @level:
 *	0 - disable logging to file
 *	3 - only error messages
 *	4 - log warnings
 *	6 - log info messages
 *	7 - log debug messages
 * @filepath_size:
 *	Count of bytes in &filepath.
 * @filename:
 *	Full path for log file.
 */
struct blksnap_setlog {
	__s32 tz_minuteswest;
	__u32 level;
	__u32 filepath_size;
	__s8 *filepath;
};

/**
 *
 */
#define IOCTL_BLKSNAP_SETLOG							\
	_IOW(BLKSNAP, blksnap_ioctl_setlog, struct blksnap_setlog)

/**
 *
 */
struct blksnap_sector_state {
	__u8 snap_number_prev;
	__u8 snap_number_curr;
	__u32 chunk_state;
};

struct blksnap_get_sector_state {
	struct blksnap_dev image_dev_id;
	__u64 sector;
	struct blksnap_sector_state state;
};

/**
 *
 */
#define IOCTL_BLKSNAP_GET_SECTOR_STATE						\
	_IOW(BLKSNAP, blksnap_ioctl_get_sector_state,				\
	     struct blksnap_get_sector_state)

#endif /* BLKSNAP_MODIFICATION */

#endif /* _UAPI_LINUX_BLKSNAP_H */
