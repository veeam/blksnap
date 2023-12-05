/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* Copyright (C) 2023 Veeam Software Group GmbH */
#ifndef _UAPI_LINUX_VEEAMBLKSNAP_H
#define _UAPI_LINUX_VEEAMBLKSNAP_H

#include <linux/types.h>

#define BLKSNAP_CTL "veeamblksnap"
#define BLKSNAP_IMAGE_NAME "veeamblksnapimg"
#define BLKSNAP 'V'

/**
 * DOC: Block device filter interface.
 *
 * Control commands that are transmitted through the block device filter
 * interface.
 */

/**
 * enum blkfilter_ctl_blksnap - List of commands for BLKFILTER_CTL ioctl
 *
 * @blkfilter_ctl_blksnap_cbtinfo:
 *	Get CBT information.
 *	The result of executing the command is a &struct blksnap_cbtinfo.
 *	Return 0 if succeeded, negative errno otherwise.
 * @blkfilter_ctl_blksnap_cbtmap:
 *	Read the CBT map.
 *	The option passes the &struct blksnap_cbtmap.
 *	The size of the table can be quite large. Thus, the table is read in
 *	a loop, in each cycle of which the next offset is set to
 *	&blksnap_tracker_read_cbt_bitmap.offset.
 *	Return a count of bytes read if succeeded, negative errno otherwise.
 * @blkfilter_ctl_blksnap_cbtdirty:
 *	Set dirty blocks in the CBT map.
 *	The option passes the &struct blksnap_cbtdirty.
 *	There are cases when some blocks need to be marked as changed.
 *	This ioctl allows to do this.
 *	Return 0 if succeeded, negative errno otherwise.
 * @blkfilter_ctl_blksnap_snapshotadd:
 *	Add device to snapshot.
 *	The option passes the &struct blksnap_snapshotadd.
 *	Return 0 if succeeded, negative errno otherwise.
 * @blkfilter_ctl_blksnap_snapshotinfo:
 *	Get information about snapshot.
 *	The result of executing the command is a &struct blksnap_snapshotinfo.
 *	Return 0 if succeeded, negative errno otherwise.
 */
enum blkfilter_ctl_blksnap {
	blkfilter_ctl_blksnap_cbtinfo,
	blkfilter_ctl_blksnap_cbtmap,
	blkfilter_ctl_blksnap_cbtdirty,
	blkfilter_ctl_blksnap_snapshotadd,
	blkfilter_ctl_blksnap_snapshotinfo,
};

#ifndef UUID_SIZE
#define UUID_SIZE 16
#endif

/**
 * struct blksnap_uuid - Unique 16-byte identifier.
 *
 * @b:
 *	An array of 16 bytes.
 */
struct blksnap_uuid {
	__u8 b[UUID_SIZE];
};

/**
 * struct blksnap_cbtinfo - Result for the command
 *	&blkfilter_ctl_blksnap.blkfilter_ctl_blksnap_cbtinfo.
 *
 * @device_capacity:
 *	Device capacity in bytes.
 * @block_size:
 *	Block size in bytes.
 * @block_count:
 *	Number of blocks.
 * @generation_id:
 *	Unique identifier of change tracking generation.
 * @changes_number:
 *	Current changes number.
 */
struct blksnap_cbtinfo {
	__u64 device_capacity;
	__u32 block_size;
	__u32 block_count;
	struct blksnap_uuid generation_id;
	__u8 changes_number;
};

/**
 * struct blksnap_cbtmap - Option for the command
 *	&blkfilter_ctl_blksnap.blkfilter_ctl_blksnap_cbtmap.
 *
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
	__u64 buffer;
};

/**
 * struct blksnap_sectors - Description of the block device region.
 *
 * @offset:
 *	Offset from the beginning of the disk in sectors.
 * @count:
 *	Count of sectors.
 */
struct blksnap_sectors {
	__u64 offset;
	__u64 count;
};

/**
 * struct blksnap_cbtdirty - Option for the command
 *	&blkfilter_ctl_blksnap.blkfilter_ctl_blksnap_cbtdirty.
 *
 * @count:
 *	Count of elements in the @dirty_sectors.
 * @dirty_sectors:
 *	Pointer to the array of &struct blksnap_sectors.
 */
struct blksnap_cbtdirty {
	__u32 count;
	__u64 dirty_sectors;
};

/**
 * struct blksnap_snapshotadd - Option for the command
 *	&blkfilter_ctl_blksnap.blkfilter_ctl_blksnap_snapshotadd.
 *
 * @id:
 *	ID of the snapshot to which the block device should be added.
 */
struct blksnap_snapshotadd {
	struct blksnap_uuid id;
};

#define IMAGE_DISK_NAME_LEN 32

/**
 * struct blksnap_snapshotinfo - Result for the command
 *	&blkfilter_ctl_blksnap.blkfilter_ctl_blksnap_snapshotinfo.
 *
 * @error_code:
 *	Zero if there were no errors while holding the snapshot.
 *	The error code -ENOSPC means that while holding the snapshot, a snapshot
 *	overflow situation has occurred. Other error codes mean other reasons
 *	for failure.
 *	The error code is reset when the device is added to a new snapshot.
 * @image:
 *	If the snapshot was taken, it stores the block device name of the
 *	image, or empty string otherwise.
 */
struct blksnap_snapshotinfo {
	__s32 error_code;
	__u8 image[IMAGE_DISK_NAME_LEN];
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
	blksnap_ioctl_snapshot_take,
	blksnap_ioctl_snapshot_collect,
	blksnap_ioctl_snapshot_wait_event,
#ifdef BLKSNAP_MODIFICATION
	blksnap_ioctl_mod = 32,
	blksnap_ioctl_setlog,
#endif
};

/**
 * struct blksnap_version - Module version.
 *
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
	_IOR(BLKSNAP, blksnap_ioctl_version, struct blksnap_version)

/**
 * struct blksnap_snapshot_create - Argument for the
 *	&IOCTL_BLKSNAP_SNAPSHOT_CREATE control.
 *
 * @diff_storage_limit_sect:
 *	The maximum allowed difference storage size in sectors.
 * @diff_storage_fd:
 *	The difference storage file descriptor.
 * @id:
 *	Generated new snapshot ID.
 */
struct blksnap_snapshot_create {
	__u64 diff_storage_limit_sect;
	__u32 diff_storage_fd;
	struct blksnap_uuid id;
};

/**
 * define IOCTL_BLKSNAP_SNAPSHOT_CREATE - Create snapshot.
 *
 * Creates a snapshot structure and initializes the difference storage.
 * A snapshot is created for several block devices at once. Several snapshots
 * can be created at the same time, but with the condition that one block
 * device can only be included in one snapshot.
 *
 * The difference storage can be dynamically increase as it fills up.
 * The file is increased in portions, the size of which is determined by the
 * module parameter &diff_storage_minimum. Each time the amount of free space
 * in the difference storage is reduced to the half of &diff_storage_minimum,
 * the file is expanded by a portion, until it reaches the allowable limit
 * &diff_storage_limit_sect.
 *
 * Return: 0 if succeeded, negative errno otherwise.
 */
#define IOCTL_BLKSNAP_SNAPSHOT_CREATE						\
	_IOWR(BLKSNAP, blksnap_ioctl_snapshot_create,				\
	     struct blksnap_snapshot_create)

/**
 * define IOCTL_BLKSNAP_SNAPSHOT_DESTROY - Release and destroy the snapshot.
 *
 * Destroys snapshot with &blksnap_snapshot_destroy.id. This leads to the
 * deletion of all block device images of the snapshot. The difference storage
 * is being released. But the change tracker keeps tracking.
 *
 * Return: 0 if succeeded, negative errno otherwise.
 */
#define IOCTL_BLKSNAP_SNAPSHOT_DESTROY						\
	_IOW(BLKSNAP, blksnap_ioctl_snapshot_destroy,				\
	     struct blksnap_uuid)

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
	_IOW(BLKSNAP, blksnap_ioctl_snapshot_take,				\
	     struct blksnap_uuid)

/**
 * struct blksnap_snapshot_collect - Argument for the
 *	&IOCTL_BLKSNAP_SNAPSHOT_COLLECT control.
 *
 * @count:
 *	Size of &blksnap_snapshot_collect.ids in the number of 16-byte UUID.
 * @ids:
 *	Pointer to the array of struct blksnap_uuid for output.
 */
struct blksnap_snapshot_collect {
	__u32 count;
	__u64 ids;
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
	_IOR(BLKSNAP, blksnap_ioctl_snapshot_collect,				\
	     struct blksnap_snapshot_collect)

/**
 * enum blksnap_event_codes - Variants of event codes.
 *
 * @blksnap_event_code_corrupted:
 *	Snapshot image is corrupted event.
 *	If a chunk could not be allocated when trying to save data to the
 *	difference storage, this event is generated. However, this does not mean
 *	that the backup process was interrupted with an error. If the snapshot
 *	image has been read to the end by this time, the backup process is
 *	considered successful.
 */
enum blksnap_event_codes {
	blksnap_event_code_corrupted,
};

/**
 * struct blksnap_snapshot_event - Argument for the
 *	&IOCTL_BLKSNAP_SNAPSHOT_WAIT_EVENT control.
 *
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
	__u8 data[4096 - 32];
};

/**
 * define IOCTL_BLKSNAP_SNAPSHOT_WAIT_EVENT - Wait and get the event from the
 *	snapshot.
 *
 * While holding the snapshot, the kernel module can transmit information about
 * changes in its state in the form of events to the user level.
 * It is very important to receive these events as quickly as possible, so the
 * user's thread is in the state of interruptible sleep.
 *
 * Return: 0 if succeeded, negative errno otherwise.
 */
#define IOCTL_BLKSNAP_SNAPSHOT_WAIT_EVENT					\
	_IOR(BLKSNAP, blksnap_ioctl_snapshot_wait_event,			\
	     struct blksnap_snapshot_event)

/**
 * struct blksnap_event_corrupted - Data for the
 *	&blksnap_event_code_corrupted event.
 *
 * @dev_id_mj:
 *	Major part of original device ID.
 * @dev_id_mn:
 *	Minor part of original device ID.
 * @err_code:
 *	Error code.
 */
struct blksnap_event_corrupted {
	__u32 dev_id_mj;
	__u32 dev_id_mn;
	__s32 err_code;
};


#ifdef BLKSNAP_MODIFICATION
enum blksnap_compat_flags {
	blksnap_compat_flag_debug_sector_state,
	blksnap_compat_flag_setlog,
	/*
	 * Reserved for new features
	 */
	blksnap_compat_flags_end
};
static_assert(blksnap_compat_flags_end <= 64,
	      "There are too many compatibility flags.");

#define BLKSNAP_MOD_NAME_LIMIT 32

/**
 * struct blksnap_modification - Result for &IOCTL_BLKSNAP_VERSION control.
 *
 * @compatibility_flags:
 *	Reserved for new modification specific features.
 * @name:
 *	Name of modification of the module (fork name, for example).
 *	It's should be empty string for upstream module.
 */
struct blksnap_mod {
	__u64 compatibility_flags;
	__u8 name[BLKSNAP_MOD_NAME_LIMIT];
};

/**
 * IOCTL_BLKSNAP_MOD - Get modification name and compatibility flags.
 *
 * Linking the product behavior to the version code does not seem to me a very
 * good idea. However, such an ioctl is good for checking that the module has
 * loaded and is responding to requests.
 *
 * The compatibility flags allows to safely extend the functionality of the
 * module. When the blksnap kernel module receives new ioctl it will be
 * enough to add a bit.
 *
 * The name of the modification can be used by the authors of forks and branches
 * of the original module. The module in upstream have not any modifications.
 */
#define IOCTL_BLKSNAP_MOD                                                      \
	_IOR(BLKSNAP, blksnap_ioctl_mod, struct blksnap_mod)

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
	__u8 *filepath;
};

/**
 * IOCTL_BLKSNAP_SETLOG - Configure private log file
 */
#define IOCTL_BLKSNAP_SETLOG                                                   \
	_IOW(BLKSNAP, blksnap_ioctl_setlog, struct blksnap_setlog)

#endif /* BLKSNAP_MODIFICATION */

#endif /* _UAPI_LINUX_VEEAMBLKSNAP_H */
