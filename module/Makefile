# SPDX-License-Identifier: GPL-2.0
KERNEL_MODULE_NAME := blk-snap

$(KERNEL_MODULE_NAME)-y += big_buffer.o
$(KERNEL_MODULE_NAME)-y += cbt_map.o
$(KERNEL_MODULE_NAME)-y += chunk.o
$(KERNEL_MODULE_NAME)-y += ctrl.o
$(KERNEL_MODULE_NAME)-y += diff_area.o
$(KERNEL_MODULE_NAME)-y += diff_storage.o
$(KERNEL_MODULE_NAME)-y += event_queue.o
$(KERNEL_MODULE_NAME)-y += main.o
# $(KERNEL_MODULE_NAME)-y += snapimage.o
# $(KERNEL_MODULE_NAME)-y += snapshot.o
$(KERNEL_MODULE_NAME)-y += sysfs.o
$(KERNEL_MODULE_NAME)-y += tracker.o

# for upstream, we don't need any addition makefiles,
# but for a standalone module the configuration is necessary
include ${M}/Makefile-*

obj-m	 += $(KERNEL_MODULE_NAME).o
