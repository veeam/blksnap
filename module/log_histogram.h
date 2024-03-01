/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __BLK_SNAP_LOG_HISTOGRAM_H
#define __BLK_SNAP_LOG_HISTOGRAM_H

struct log_histogram {
	atomic_t cnt[10];
	unsigned long min_value;
};

void log_histogram_init(struct log_histogram *hg, unsigned long min_value);
void log_histogram_add(struct log_histogram *hg, unsigned long value);
void log_histogram_show(struct log_histogram *hg);

#endif /* __BLK_SNAP_LOG_HISTOGRAM_H */
