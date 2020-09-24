/* SPDX-License-Identifier: GPL-2.0 */
#pragma once

#include "blk_deferred.h"

struct snapstore_file {
	dev_t blk_dev_id;
	struct block_device *blk_dev;

	struct blk_descr_pool pool;
};

int snapstore_file_create(dev_t dev_id, struct snapstore_file **pfile);

void snapstore_file_destroy(struct snapstore_file *file);
