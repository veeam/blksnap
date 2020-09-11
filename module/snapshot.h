#pragma once

typedef struct snapshot_s
{
	struct list_head link;
	unsigned long long id;

	dev_t* dev_id_set;	//array
	int dev_id_set_size;
}snapshot_t;

void snapshot_Done( void );

int snapshot_Create( dev_t* dev_id_set, unsigned int dev_id_set_size, unsigned int cbt_block_size_degree, unsigned long long* psnapshot_id );

int snapshot_Destroy( unsigned long long snapshot_id );


