/* Copyright (C) 2023 Veeam Software Group GmbH */
#ifndef _UAPI_LINUX_BDEVFILTER_H
#define _UAPI_LINUX_BDEVFILTER_H

#include <linux/types.h>

#define BDEVFILTER "bdevfilter"
#define BDEVFILTER_NAME_LENGTH	32

/**
 * struct bdevfilter_name - parameter for BLKFILTER_ATTACH and BLKFILTER_DETACH
 *      ioctl.
 *
 * @name:       Name of block device filter.
 */
struct bdevfilter_name {
	__s32 bdev_fd;
	__u8 name[BDEVFILTER_NAME_LENGTH];
};

/**
 * struct bdevfilter_ctl - parameter for bdevfilter_ctl ioctl
 *
 * @name:	Name of block device filter.
 * @cmd:	The filter-specific operation code of the command.
 * @optlen:	Size of data at @opt.
 * @opt:	Userspace buffer with options.
 */
struct bdevfilter_ctl {
	__s32 bdev_fd;
	__u8 name[BDEVFILTER_NAME_LENGTH];
	__u32 cmd;
	__u32 optlen;
	__u64 opt;
};


#define BDEVFILTER_ATTACH	_IOWR('F', 140, struct bdevfilter_name)
#define BDEVFILTER_DETACH	_IOWR('F', 141, struct bdevfilter_name)
#define BDEVFILTER_CTL		_IOWR('F', 142, struct bdevfilter_ctl)

#endif /* _UAPI_LINUX_BDEVFILTER_H */
