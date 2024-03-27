/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023 Veeam Software Group GmbH */
#ifndef __BLKSNAP_LOG_HISTOGRAM_H
#define __BLKSNAP_LOG_HISTOGRAM_H

#ifdef BLKSNAP_HISTOGRAM
struct log_histogram {
	atomic64_t cnt[10];
	unsigned long min_value;
};

void log_histogram_init(struct log_histogram *hg, unsigned long min_value);
void log_histogram_enable(int state);
void log_histogram_add(struct log_histogram *hg, unsigned long value);
void log_histogram_show(struct log_histogram *hg);
#endif /* BLKSNAP_HISTOGRAM */
#endif /* __BLK_SNAP_LOG_HISTOGRAM_H */
