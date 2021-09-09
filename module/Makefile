# SPDX-License-Identifier: GPL-2.0
KERNEL_MODULE_NAME := blk-snap-lp

$(KERNEL_MODULE_NAME)-y += big_buffer.o
$(KERNEL_MODULE_NAME)-y += cbt_map.o
$(KERNEL_MODULE_NAME)-y += chunk.o
$(KERNEL_MODULE_NAME)-y += ctrl.o
$(KERNEL_MODULE_NAME)-y += diff_area.o
$(KERNEL_MODULE_NAME)-y += diff_storage.o
$(KERNEL_MODULE_NAME)-y += event_queue.o
$(KERNEL_MODULE_NAME)-y += main.o
$(KERNEL_MODULE_NAME)-y += snapimage.o
$(KERNEL_MODULE_NAME)-y += snapshot.o
$(KERNEL_MODULE_NAME)-y += sysfs.o
$(KERNEL_MODULE_NAME)-y += tracker.o
$(KERNEL_MODULE_NAME)-y += lp_filter.o
# only for standalone module

# for upstream, we don't need any configurations
# but for a standalone module, the configuration is necessary
EXTRA_CFLAGS += $(shell 							\
	grep -qw "submit_bio_noacct" $(srctree)/include/linux/blkdev.h	&&	\
		echo -DHAVE_SUBMIT_BIO_NOACCT)
EXTRA_CFLAGS += $(shell 							\
	grep -qw "struct super_block *freeze_bdev;"				\
		$(srctree)/include/linux/blkdev.h &&				\
		echo -DHAVE_SUPER_BLOCK_FREEZE)
EXTRA_CFLAGS += $(shell 							\
	grep -qw "*bi_bdev;" $(srctree)/include/linux/blk_types.h &&		\
		echo -DHAVE_BI_BDEV)
EXTRA_CFLAGS += $(shell 							\
	grep -qw "*bi_disk;" $(srctree)/include/linux/blk_types.h &&		\
		echo -DHAVE_BI_BDISK)
EXTRA_CFLAGS += $(shell 							\
	grep -qw "*bdev_nr_sectors;" $(srctree)/include/linux/genhd.h &&	\
		echo -DHAVE_BDEV_NR_SECTORS)

# The power of 2 for minimum trackings block size.
# The minimum tracking block size by default is 64 KB (shift 16)
# It's looks good for block device 128 GB or lower.
# In this case, the block device is divided into 2097152 blocks.
EXTRA_CFLAGS += "-DCONFIG_BLK_SNAP_TRACKING_BLOCK_MINIMUM_SHIFT=16"

# The limit of the maximum number of trackings blocks.
# As the size of the block device grows, the size of the tracking block
# size should also grow. For this purpose, the limit of the maximum
# number of block size is set.
EXTRA_CFLAGS += "-DCONFIG_BLK_SNAP_TRACKING_BLOCK_MAXIMUM_COUNT=2097152"

# The minimum chunk size is 256 KB (shift 18).
# It's looks good for block device 128 GB or lower.
# In this case, the block device is divided into 524288 chunks.
EXTRA_CFLAGS += "-DCONFIG_BLK_SNAP_CHUNK_MINIMUM_SHIFT=18"

# As the size of the block device grows, the size of the chunk should also grow.
# For this purpose, the limit of the maximum number of chunks is set.
EXTRA_CFLAGS += "-DCONFIG_BLK_SNAP_CHUNK_MAXIMUM_COUNT=2097152"

# Since reading and writing to snapshots is performed in large chunks,
# a cache is implemented to optimize reading small portions of data
# from the snapshot image. As the number of chunks in the cache
# increases, memory consumption also increases.
# The minimum recommended value is four.
EXTRA_CFLAGS += "-DCONFIG_BLK_SNAP_CHUNK_MAXIMUM_IN_CACHE=128"

# The minimum allowable size of the difference storage in sectors.
# When reached, an event is generated about the lack of free space.
EXTRA_CFLAGS += "-DCONFIG_BLK_SNAP_DIFF_STORAGE_MINIMUM=2097152"

obj-m	 += $(KERNEL_MODULE_NAME).o
