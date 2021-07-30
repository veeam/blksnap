# SPDX-License-Identifier: GPL-2.0
KERNEL_MODULE_NAME := blk-snap-lp

$(KERNEL_MODULE_NAME)-y += blk_util.o
$(KERNEL_MODULE_NAME)-y += cbt_map.o
$(KERNEL_MODULE_NAME)-y += ctrl_fops.o
$(KERNEL_MODULE_NAME)-y += ctrl_pipe.o
$(KERNEL_MODULE_NAME)-y += ctrl_sysfs.o
$(KERNEL_MODULE_NAME)-y += main.o
$(KERNEL_MODULE_NAME)-y += params.o
$(KERNEL_MODULE_NAME)-y += big_buffer.o
$(KERNEL_MODULE_NAME)-y += rangevector.o
$(KERNEL_MODULE_NAME)-y += snapimage.o
$(KERNEL_MODULE_NAME)-y += snapshot.o
$(KERNEL_MODULE_NAME)-y += tracker.o
$(KERNEL_MODULE_NAME)-y += chunkcache.o


# for upstream, we don't need any configurations
#obj-$(CONFIG_BLK_SNAP)	 += $(KERNEL_MODULE_NAME).o

# but for a standalone module, the configuration is necessary
EXTRA_CFLAGS += $(shell 							\
	grep -qw "submit_bio_noacct" $(srctree)/include/linux/blkdev.h	&&	\
		echo -DHAVE_SUBMIT_BIO_NOACCT;					\
	)
EXTRA_CFLAGS += $(shell 							\
	grep -qw "struct super_block *freeze_bdev);"				\
		$(srctree)/include/linux/blkdev.h &&				\
		echo -DHAVE_SUPER_BLOCK_FREEZE					\
	)
EXTRA_CFLAGS += $(shell 							\
	grep -qw "*bi_bdev;" $(srctree)/include/linux/blk_types.h &&		\
		echo -DHAVE_BI_BDEV						\
	)
EXTRA_CFLAGS += $(shell 							\
	grep -qw "*bi_disk;" $(srctree)/include/linux/blk_types.h &&		\
		echo -DHAVE_BI_BDISK						\
	)

# The minimum chunk size is 65535 KB (shift 16)
# It's looks good for block device 128 GB or lower
# In this case, the block device is divided into 2097152 chunks.
EXTRA_CFLAGS += "-DCONFIG_BLK_SNAP_CHUNK_MINIMUM_SHIFT=16"

# As the size of the block device grows, the size of the chunk should also grow.
# For this purpose, the limit of the maximum number of chunks is set.
EXTRA_CFLAGS += "-DCONFIG_BLK_SNAP_MAXIMUM_CHUNK_COUNT=2097152"

# Since reading and writing to snapshots is performed in large chunks,
# a cache is implemented to optimize reading small portions of data
# from the snapshot image. As the number of chunks in the cache
# increases, memory consumption also increases.
# The minimum recommended value is four.
EXTRA_CFLAGS += "-DCONFIG_BLK_SNAP_MAXIMUM_CHUNK_IN_CACHE=128"

obj-m	 += $(KERNEL_MODULE_NAME).o
