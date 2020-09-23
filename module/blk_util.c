#include "common.h"
#include "blk_util.h"

int blk_dev_open( dev_t dev_id, struct block_device** p_blk_dev )
{
	int result = SUCCESS;
	struct block_device* blk_dev;
	int refCount;

	blk_dev = bdget( dev_id );
	if (NULL == blk_dev){
		pr_err( "Unable to open device [%d:%d]: bdget return NULL\n", MAJOR( dev_id ), MINOR( dev_id ) );
		return -ENODEV;
	}

	refCount = blkdev_get( blk_dev, FMODE_READ | FMODE_WRITE, NULL );
	if (refCount < 0){
		pr_err( "Unable to open device [%d:%d]: blkdev_get return error code %d\n", MAJOR( dev_id ), MINOR( dev_id ), 0 - refCount );
		result = refCount;
	}

	if (result == SUCCESS)
		*p_blk_dev = blk_dev;
	return result;
}

void blk_dev_close( struct block_device* blk_dev )
{
	blkdev_put( blk_dev, FMODE_READ );
}
