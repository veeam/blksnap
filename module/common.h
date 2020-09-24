/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#ifndef BLK_SNAP_SECTION
#define BLK_SNAP_SECTION ""
#endif
#define pr_fmt(fmt) KBUILD_MODNAME BLK_SNAP_SECTION ": " fmt

#include <linux/version.h> /*rudiment - needed for using KERNEL_VERSION */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <asm/atomic.h>
#include <linux/blkdev.h>

#define from_sectors(_sectors) (_sectors << SECTOR_SHIFT)
#define to_sectors(_byte_size) (_byte_size >> SECTOR_SHIFT)

struct blk_range {
	sector_t ofs;
	blkcnt_t cnt;
};

#ifndef SUCCESS
#define SUCCESS 0
#endif
