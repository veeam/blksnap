# SPDX-License-Identifier: GPL-2.0
KERNEL_MODULE_NAME := blk-snap-lp

$(KERNEL_MODULE_NAME)-y += blk_deferred.o
$(KERNEL_MODULE_NAME)-y += blk_descr_file.o
$(KERNEL_MODULE_NAME)-y += blk_descr_mem.o
$(KERNEL_MODULE_NAME)-y += blk_descr_multidev.o
$(KERNEL_MODULE_NAME)-y += blk_descr_pool.o
$(KERNEL_MODULE_NAME)-y += blk_redirect.o
$(KERNEL_MODULE_NAME)-y += blk_util.o
$(KERNEL_MODULE_NAME)-y += cbt_map.o
$(KERNEL_MODULE_NAME)-y += ctrl_fops.o
$(KERNEL_MODULE_NAME)-y += ctrl_pipe.o
$(KERNEL_MODULE_NAME)-y += ctrl_sysfs.o
$(KERNEL_MODULE_NAME)-y += defer_io.o
$(KERNEL_MODULE_NAME)-y += main.o
$(KERNEL_MODULE_NAME)-y += params.o
$(KERNEL_MODULE_NAME)-y += big_buffer.o
$(KERNEL_MODULE_NAME)-y += rangevector.o
$(KERNEL_MODULE_NAME)-y += snapimage.o
$(KERNEL_MODULE_NAME)-y += snapshot.o
$(KERNEL_MODULE_NAME)-y += snapstore.o
$(KERNEL_MODULE_NAME)-y += snapstore_device.o
$(KERNEL_MODULE_NAME)-y += snapstore_file.o
$(KERNEL_MODULE_NAME)-y += snapstore_mem.o
$(KERNEL_MODULE_NAME)-y += snapstore_multidev.o
$(KERNEL_MODULE_NAME)-y += tracker.o
$(KERNEL_MODULE_NAME)-y += tracking.o

# for upstream, we don't need any configurations
#obj-$(CONFIG_BLK_SNAP)	 += $(KERNEL_MODULE_NAME).o

# but for a standalone module, the configuration is necessary
EXTRA_CFLAGS += $(shell 							\
	grep -qw "submit_bio_noacct" $(srctree)/include/linux/blkdev.h	&&	\
		echo -DHAVE_SUBMIT_BIO_NOACCT;					\
	)
EXTRA_CFLAGS += $(shell 							\
	grep -qw "*bi_bdev;" $(srctree)/include/linux/blk_types.h &&		\
		echo -DHAVE_BI_BDEV						\
	)
EXTRA_CFLAGS += $(shell 							\
	grep -qw "*bi_disk;" $(srctree)/include/linux/blk_types.h &&		\
		echo -DHAVE_BI_BDISK						\
	)

obj-m	 += $(KERNEL_MODULE_NAME).o
