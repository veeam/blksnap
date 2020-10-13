/* SPDX-License-Identifier: GPL-2.0 */
#pragma once

struct snapshot {
	struct list_head link;
	unsigned long long id;

	dev_t *dev_id_set; //array of assigned devices
	int dev_id_set_size;
};

void snapshot_done(void);

int snapshot_create(dev_t *dev_id_set, unsigned int dev_id_set_size,
		    unsigned long long *p_snapshot_id);

int snapshot_destroy(unsigned long long snapshot_id);
