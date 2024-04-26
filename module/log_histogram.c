// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 Veeam Software Group GmbH */
#define pr_fmt(fmt) KBUILD_MODNAME "-log: " fmt
#include <linux/module.h>
#include "log_histogram.h"
#ifdef BLKSNAP_FILELOG
#include "log.h"
#endif

#ifdef BLKSNAP_HISTOGRAM
static atomic_t log_histogram_state;

void log_histogram_init(struct log_histogram *hg, unsigned long min_value)
{
	memset(&hg->cnt, 0, sizeof(hg->cnt));
	hg->min_value = min_value;
}

void log_histogram_enable(int state)
{
	atomic_set(&log_histogram_state, state);
}

void log_histogram_add(struct log_histogram *hg, unsigned long value)
{
	int inx = 0;
	int last = sizeof(hg->cnt) / sizeof(hg->cnt[0]) - 1;
	unsigned long test_value = hg->min_value;

	if (!atomic_read(&log_histogram_state))
		return;

	while (inx <= last) {
		if (value <= test_value)
			break;

		test_value = test_value << 1;
		inx++;
	}
	atomic64_inc(&hg->cnt[inx]);
}

void log_histogram_show(struct log_histogram *hg)
{
	int inx = 0;
	int last = sizeof(hg->cnt) / sizeof(hg->cnt[0]) - 1;
	unsigned long test_value = hg->min_value;
	unsigned long prev_test_value = 0;

	if (!atomic_read(&log_histogram_state))
		return;

	while (inx <= last) {
		pr_debug("(%lu : %lu] KiB - %lld\n",
			prev_test_value / 1024,
			test_value / 1024,
			atomic64_read(&hg->cnt[inx]));

		prev_test_value = test_value;
		test_value = test_value << 1;
		inx++;
	}
}
#endif
