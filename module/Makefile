# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2023 Veeam Software Group GmbH

include ${M}/Makefile-config
include ${M}/Makefile-bdevfilter

veeamblksnap-y :=	\
	cbt_map.o	\
	chunk.o		\
	diff_area.o	\
	diff_buffer.o	\
	diff_storage.o	\
	event_queue.o	\
	main.o		\
	snapimage.o	\
	snapshot.o	\
	tracker.o

include ${M}/Makefile-standalone

obj-m	 += veeamblksnap.o
