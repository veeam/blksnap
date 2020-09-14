#pragma once
#include <linux/blkdev.h>

#define from_sectors(_sectors) \
	(_sectors << SECTOR_SHIFT)
#define to_sectors(_byte_size) \
	(_byte_size >> SECTOR_SHIFT)
