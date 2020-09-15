#pragma once
#include <linux/blkdev.h>

#define from_sectors(_sectors) \
	(_sectors << SECTOR_SHIFT)
#define to_sectors(_byte_size) \
	(_byte_size >> SECTOR_SHIFT)

struct blk_range
{
	sector_t ofs;
	blkcnt_t cnt;
};
