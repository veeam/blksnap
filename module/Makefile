# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2023 Veeam Software Group GmbH

# EXTRA_CFLAGS += "-D DEBUG"
# EXTRA_CFLAGS += "-D CONFIG_BLKSNAP_CHUNK_DBG"
# EXTRA_CFLAGS += "-D CONFIG_BLKSNAP_COW_SCHEDULE"
EXTRA_CFLAGS += "-D BLKSNAP_STANDALONE"
EXTRA_CFLAGS += "-D BLKSNAP_MODIFICATION"
EXTRA_CFLAGS += "-D BLKSNAP_MEMSTAT"
EXTRA_CFLAGS += "-D BLKSNAP_FILELOG"
EXTRA_CFLAGS += "-D BLKSNAP_HISTOGRAM"

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

veeamblksnap-y += log.o
veeamblksnap-y += log_histogram.o
veeamblksnap-y += memstat.o

obj-m	 += veeamblksnap.o
