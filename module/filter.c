// SPDX-License-Identifier: GPL-2.0
#include "common.h"
#include "filter.h"
#include "tracking.h"

#include <linux/blk-filter.h>

#define BLK_SNAP_DEFAULT_ALTITUDE BLK_FILTER_ALTITUDE_MIN

static void filter_disk_add(struct gendisk *disk)
{
	pr_info("new disk [%s] in system\n", disk->disk_name);
}
static void filter_disk_del(struct gendisk *disk)
{
	pr_info("del disk [%s] from system\n", disk->disk_name);
}
static void filter_disk_release(struct gendisk *disk)
{
	pr_info("release disk [%s] from system\n", disk->disk_name);
}

static blk_qc_t filter_submit_bio(struct bio *bio)
{
	blk_qc_t result;

	if (tracking_submit_bio(bio, &result))
		return result;

	return filter_submit_original_bio(bio);
}

const struct blk_filter_ops filter_ops = { .disk_add = filter_disk_add,
					   .disk_del = filter_disk_del,
					   .disk_release = filter_disk_release,
					   .submit_bio = filter_submit_bio };

struct blk_filter filter = { .name = MODULE_NAME,
			     .ops = &filter_ops,
			     .altitude = BLK_SNAP_DEFAULT_ALTITUDE,
			     .blk_filter_ctx = NULL };

blk_qc_t filter_submit_original_bio(struct bio *bio)
{
	return blk_filter_submit_bio_next(&filter, bio);
}

int filter_init(void)
{
	const char *exist_filter;
	int result;

	result = blk_filter_register(&filter);
	if (result != SUCCESS) {
		pr_err("Failed to register block io layer filter\n");

		exist_filter = blk_filter_check_altitude(filter.altitude);
		if (exist_filter)
			pr_err("Block io layer filter [%s] already exist on altitude [%ld]\n",
			       exist_filter, filter.altitude);
	}

	return result;
}

void filter_done(void)
{
	int result = blk_filter_unregister(&filter);

	BUG_ON(result != SUCCESS);
}
