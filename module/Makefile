# SPDX-License-Identifier: GPL-2.0
# for upstream, we don't need any addition makefiles,
# but for a standalone module the configuration is necessary
include ${M}/Makefile-*

blksnap-$(CONFIG_BLK_SNAP) += cbt_map.o
blksnap-$(CONFIG_BLK_SNAP) += chunk.o
blksnap-$(CONFIG_BLK_SNAP) += ctrl.o
blksnap-$(CONFIG_BLK_SNAP) += diff_io.o
blksnap-$(CONFIG_BLK_SNAP) += diff_area.o
blksnap-$(CONFIG_BLK_SNAP) += diff_buffer.o
blksnap-$(CONFIG_BLK_SNAP) += diff_storage.o
blksnap-$(CONFIG_BLK_SNAP) += event_queue.o
blksnap-$(CONFIG_BLK_SNAP) += main.o
blksnap-$(CONFIG_BLK_SNAP) += snapimage.o
blksnap-$(CONFIG_BLK_SNAP) += snapshot.o
blksnap-$(CONFIG_BLK_SNAP) += sysfs.o
blksnap-$(CONFIG_BLK_SNAP) += tracker.o

obj-$(CONFIG_BLK_SNAP)	 += blksnap.o
