#pragma once

struct snapshot {
	struct list_head link;
	unsigned long long id;

	dev_t *dev_id_set; //array of assigned devices
	int dev_id_set_size;
};

void snapshot_Done(void);

int snapshot_Create(dev_t *dev_id_set, unsigned int dev_id_set_size,
		    unsigned int cbt_block_size_degree, unsigned long long *psnapshot_id);

int snapshot_Destroy(unsigned long long snapshot_id);
