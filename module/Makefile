# SPDX-License-Identifier: GPL-2.0
# for upstream, we don't need any addition makefiles,
# but for a standalone module the configuration is necessary
include ${M}/Makefile-*

blksnap-y := 		\
	cbt_map.o	\
	chunk.o		\
	diff_io.o	\
	diff_area.o	\
	diff_buffer.o	\
	diff_storage.o	\
	event_queue.o	\
	main.o		\
	snapimage.o	\
	snapshot.o	\
	tracker.o

obj-$(CONFIG_BLK_SNAP)	 += blksnap.o
