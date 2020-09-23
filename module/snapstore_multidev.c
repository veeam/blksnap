#include "common.h"
#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV

#include "snapstore_multidev.h"
#include "blk_util.h"

int snapstore_multidev_create( snapstore_multidev_t** p_multidev )
{
	int res = SUCCESS;
	snapstore_multidev_t* multidev;

	pr_info( "Multidevice file snapstore create\n" );

	multidev = kzalloc( sizeof( snapstore_multidev_t ), GFP_KERNEL );
	if (multidev == NULL)
		return -ENOMEM;

	INIT_LIST_HEAD( &multidev->devicelist );
	spin_lock_init( &multidev->devicelist_lock );

	blk_descr_multidev_pool_init( &multidev->pool );

	*p_multidev = multidev;
	return res;
}

void snapstore_multidev_destroy( snapstore_multidev_t* multidev )
{
	multidev_el_t* el;

	//BUG_ON(NULL == multidev);
	blk_descr_multidev_pool_done( &multidev->pool );

	do {
		el = NULL;
		spin_lock( &multidev->devicelist_lock );
		if (!list_empty( &multidev->devicelist )){
			el = list_entry( multidev->devicelist.next, multidev_el_t, link );

			list_del( &el->link );
		}
		spin_unlock( &multidev->devicelist_lock );

		if (el) {
			blk_dev_close( el->blk_dev );

			pr_info( "Close device for multidevice snapstore [%d:%d]\n",
				MAJOR(el->dev_id), MINOR(el->dev_id));

			kfree( el );
		}
	} while(el);

	kfree( multidev );
}

multidev_el_t* snapstore_multidev_find(snapstore_multidev_t* multidev, dev_t dev_id)
{
	multidev_el_t* el = NULL;

	spin_lock( &multidev->devicelist_lock );
	if (!list_empty( &multidev->devicelist )){
		struct list_head* _head;

		list_for_each( _head, &multidev->devicelist ) {
			multidev_el_t* _el = list_entry( _head, multidev_el_t, link );

			if (_el->dev_id == dev_id) {
				el = _el;
				break;
			}
		}
	}
	spin_unlock( &multidev->devicelist_lock );

	return el;
}

struct block_device* snapstore_multidev_get_device( snapstore_multidev_t* multidev, dev_t dev_id )
{
	int res;
	struct block_device* blk_dev = NULL;
	multidev_el_t* el = snapstore_multidev_find(multidev, dev_id);

	if (el)
		return el->blk_dev;

	res = blk_dev_open( dev_id, &blk_dev );
	if (res != SUCCESS){
		pr_err("Unable to add device to snapstore multidevice file\n");
		pr_err("Failed to open [%d:%d]. errno=%d", MAJOR( dev_id ), MINOR( dev_id ), res);
		return NULL;
	}

	el = kzalloc(sizeof(multidev_el_t), GFP_KERNEL);
	INIT_LIST_HEAD( &el->link );

	el->blk_dev = blk_dev;
	el->dev_id = dev_id;

	spin_lock( &multidev->devicelist_lock );
	list_add_tail( &el->link, &multidev->devicelist );
	spin_unlock( &multidev->devicelist_lock );

	return el->blk_dev;
}

#endif
