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
#ifndef _UAPI_LINUX_BLK_FILTER_H
#define _UAPI_LINUX_BLK_FILTER_H

#include <linux/types.h>
#include <linux/fs.h>

#ifndef BLKFILTER_ATTACH

/* 130-136 are used by zoned block device ioctls (uapi/linux/blkzoned.h) */
/* 137-141 are used by blk-crypto ioctls (uapi/linux/blk-crypto.h) */
#define BLKFILTER_ATTACH        _IOWR(0x12, 142, struct blkfilter_attach)
#define BLKFILTER_DETACH        _IOWR(0x12, 143, struct blkfilter_detach)
#define BLKFILTER_CTL           _IOWR(0x12, 144, struct blkfilter_ctl)

#define BLKFILTER_NAME_LENGTH   32

/**
 * struct blkfilter_attach - parameter for BLKFILTER_ATTACH ioctl.
 *
 * @name:       Name of block device filter.
 * @opt:        Userspace buffer with options.
 * @optlen:     Size of data at @opt.
 */
struct blkfilter_attach {
        __u8 name[BLKFILTER_NAME_LENGTH];
        __u64 opt;
        __u32 optlen;
};

/**
 * struct blkfilter_detach - parameter for BLKFILTER_DETACH ioctl.
 *      ioctl.
 *
 * @name:       Name of block device filter.
 */
struct blkfilter_detach {
        __u8 name[BLKFILTER_NAME_LENGTH];
};

/**
 * struct blkfilter_ctl - parameter for BLKFILTER ioctl
 *
 * @name:       Name of block device filter.
 * @cmd:        Command code opcode (BLKFILTER_CMD_*)
 * @optlen:     Size of data at @opt
 * @opt:        userspace buffer with options
 */
struct blkfilter_ctl {
        __u8 name[BLKFILTER_NAME_LENGTH];
        __u32 cmd;
        __u32 optlen;
        __u64 opt;
};

#endif

#endif
