#include "common.h"
#include "blk_deferred.h"
#include "blk_util.h"
#include "snapstore.h"
#include "snapstore_blk.h"

struct bio_set g_BlkDeferredBioset = { 0 };
#define BlkDeferredBioset &g_BlkDeferredBioset

typedef struct dio_bio_complete_s{
	blk_deferred_request_t* dio_req;
	sector_t bio_sect_len;
}dio_bio_complete_t;

typedef struct dio_deadlocked_list_s
{
	struct list_head link;

	blk_deferred_request_t* dio_req;
}dio_deadlocked_list_t;


LIST_HEAD(dio_deadlocked_list);
DEFINE_RWLOCK(dio_deadlocked_list_lock);

atomic64_t dio_alloc_count = ATOMIC64_INIT(0);
atomic64_t dio_free_count = ATOMIC64_INIT(0);


void blk_deferred_done( void )
{
	dio_deadlocked_list_t* dio_locked;

	do {
		dio_locked = NULL;

		write_lock( &dio_deadlocked_list_lock );
		if (!list_empty( &dio_deadlocked_list )){
			dio_locked = list_entry( dio_deadlocked_list.next, dio_deadlocked_list_t, link );

			list_del( &dio_locked->link );
		}
		write_unlock(&dio_deadlocked_list_lock);

		if (dio_locked) {
			if (dio_locked->dio_req->sect_len == atomic64_read( &dio_locked->dio_req->sect_processed ))
				blk_deferred_request_free( dio_locked->dio_req );
			else
				pr_err( "Locked defer IO is still in memory\n" );

			kfree(dio_locked);
		}
	} while(dio_locked);
}

void blk_deferred_request_deadlocked( blk_deferred_request_t* dio_req )
{
	dio_deadlocked_list_t* dio_locked = kzalloc(sizeof(dio_deadlocked_list_t), GFP_KERNEL);

	dio_locked->dio_req = dio_req;

	write_lock( &dio_deadlocked_list_lock );
	list_add_tail( &dio_locked->link, &dio_deadlocked_list);
	write_unlock(&dio_deadlocked_list_lock);

	pr_warn( "Deadlock with defer IO\n" );
}

void blk_deferred_free( blk_deferred_t* dio )
{
	size_t inx = 0;

	if (dio->page_array != NULL) {
		while (NULL != dio->page_array[inx] ) {
			__free_page(dio->page_array[inx]);
			dio->page_array[inx] = NULL;

			++inx;
		}

		kfree( dio->page_array );
		dio->page_array = NULL;
	}
	kfree( dio );
}

blk_deferred_t* blk_deferred_alloc( unsigned long block_index, union blk_descr_unify blk_descr )
{
	size_t inx;
	size_t page_count;

	blk_deferred_t* dio = kmalloc( sizeof( blk_deferred_t ), GFP_NOIO );
	if (dio == NULL)
		return NULL;

	INIT_LIST_HEAD( &dio->link );

	dio->blk_descr = blk_descr;
	dio->blk_index = block_index;

	dio->sect.ofs = block_index << snapstore_block_shift();
	dio->sect.cnt = snapstore_block_size();

	page_count = snapstore_block_size() / (PAGE_SIZE / SECTOR_SIZE);
	dio->page_array = kzalloc((page_count + 1) * sizeof(struct page *), GFP_NOIO); //empty pointer on the end
	if (NULL == dio->page_array){
		pr_err( "Failed to allocate defer IO block [%ld]\n", block_index );

		blk_deferred_free( dio );
		return NULL;
	}

	for (inx=0; inx < page_count; inx++ ) {
		dio->page_array[inx] = alloc_page(GFP_NOIO);
		if (NULL == dio->page_array[inx]) {
			pr_err( "Failed to allocate page\n" );
			blk_deferred_free( dio );
			return NULL;
		}
	}

	return dio;
}


int blk_deferred_bioset_create( void )
{
	return bioset_init(BlkDeferredBioset, 64, sizeof(dio_bio_complete_t), BIOSET_NEED_BVECS | BIOSET_NEED_RESCUER);
}

void blk_deferred_bioset_free( void )
{
	bioset_exit(BlkDeferredBioset);
}

struct bio* _blk_deferred_bio_alloc( int nr_iovecs )
{
	struct bio* new_bio = bio_alloc_bioset( GFP_NOIO, nr_iovecs, BlkDeferredBioset );
	if (new_bio){
		new_bio->bi_end_io = blk_deferred_bio_endio;
		new_bio->bi_private = ((void*)new_bio) - sizeof( dio_bio_complete_t );
	}
	return new_bio;
}



void blk_deferred_complete( blk_deferred_request_t* dio_req, sector_t portion_sect_cnt, int result )
{
	atomic64_add( portion_sect_cnt, &dio_req->sect_processed );

	if (dio_req->sect_len == atomic64_read( &dio_req->sect_processed )){
		complete( &dio_req->complete );
	}

	if (result != SUCCESS){
		dio_req->result = result;
		pr_err( "Failed to process defer IO request. errno=%d\n", result );
	}
}

void blk_deferred_bio_endio( struct bio *bio )
{
	int local_err;
	dio_bio_complete_t* complete_param = (dio_bio_complete_t*)bio->bi_private;

	if (complete_param == NULL){
//		WARN( true, "bio already end." );
	}else{
		if (bio->bi_status != BLK_STS_OK)
			local_err = -EIO;
		else
			local_err = SUCCESS;

		blk_deferred_complete( complete_param->dio_req, complete_param->bio_sect_len, local_err );
		bio->bi_private = NULL;
	}

	bio_put( bio );
}

static inline
size_t _page_count_calculate( sector_t size_sector )
{
	size_t page_count = size_sector / (PAGE_SIZE / SECTOR_SIZE);

	if (unlikely( size_sector & ((PAGE_SIZE / SECTOR_SIZE) - 1) ))
		page_count += 1;
	return page_count;
}

sector_t _blk_deferred_submit_pages(
	struct block_device* blk_dev,
	blk_deferred_request_t* dio_req,
	int direction,
	sector_t arr_ofs,
	struct page **page_array,
	sector_t ofs_sector,
	sector_t size_sector
){

	struct bio *bio = NULL;
	int nr_iovecs;
	int page_inx = arr_ofs >> (PAGE_SHIFT - SECTOR_SHIFT);
	sector_t process_sect = 0;

	nr_iovecs = _page_count_calculate( size_sector );

	while (NULL == (bio = _blk_deferred_bio_alloc( nr_iovecs ))) {
		size_sector = (size_sector >> 1) & ~((PAGE_SIZE / SECTOR_SIZE) - 1);
		if (size_sector == 0)
			return 0;

		nr_iovecs = _page_count_calculate( size_sector );
	}

	bio_set_dev(bio, blk_dev);

	if (direction == READ)
		bio_set_op_attrs( bio, REQ_OP_READ, 0 );
	else
		bio_set_op_attrs( bio, REQ_OP_WRITE, 0 );

	bio->bi_iter.bi_sector = ofs_sector;

	{//add first
		sector_t unordered = arr_ofs & ((PAGE_SIZE / SECTOR_SIZE) - 1);
		sector_t bvec_len_sect = min_t( sector_t, ((PAGE_SIZE / SECTOR_SIZE) - unordered), size_sector );
		struct page* page = page_array[page_inx];

		if (0 == bio_add_page( bio, page, (unsigned int)from_sectors( bvec_len_sect ), (unsigned int)from_sectors( unordered ) )){
			bio_put( bio );
			return 0;
		}
		++page_inx;
		process_sect += bvec_len_sect;
	}

	while (process_sect < size_sector)
	{
		sector_t bvec_len_sect = min_t( sector_t, (PAGE_SIZE / SECTOR_SIZE), (size_sector - process_sect) );
		struct page* page = page_array[page_inx];

		BUG_ON(NULL == page);
		if (0 == bio_add_page( bio, page, (unsigned int)from_sectors( bvec_len_sect ), 0 ))
			break;

		++page_inx;
		process_sect += bvec_len_sect;
	}


	((dio_bio_complete_t*)bio->bi_private)->dio_req = dio_req;
	((dio_bio_complete_t*)bio->bi_private)->bio_sect_len = process_sect;

	submit_bio( bio );

	return process_sect;
}

sector_t blk_deferred_submit_pages(
	struct block_device* blk_dev,
	blk_deferred_request_t* dio_req,
	int direction,
	sector_t arr_ofs,
	struct page **page_array,
	sector_t ofs_sector,
	sector_t size_sector
){
	sector_t process_sect = 0;

	do {
		sector_t portion_sect = _blk_deferred_submit_pages( blk_dev, dio_req, direction, arr_ofs + process_sect, page_array, ofs_sector + process_sect, size_sector - process_sect );
		if (portion_sect == 0){
			pr_err( "Failed to submit defer IO pages. Only [%lld] sectors processed\n", process_sect );
			break;
		}
		process_sect += portion_sect;
	} while (process_sect < size_sector);

	return process_sect;
}

void blk_deferred_memcpy_read( char* databuff, blk_deferred_request_t* dio_req, struct page **page_array, sector_t arr_ofs, sector_t size_sector )
{
	size_t prev_pg_inx = 0;
	void *addr = NULL;
	sector_t sect_inx = 0;

	for ( ; sect_inx < size_sector; ++sect_inx) {
		size_t pg_inx = (arr_ofs + sect_inx) >> (PAGE_SHIFT - SECTOR_SHIFT);
		size_t pg_ofs = (size_t)from_sectors( (arr_ofs + sect_inx) & ((1 << (PAGE_SHIFT - SECTOR_SHIFT)) - 1) );

		if ( (prev_pg_inx != pg_inx) || (NULL == addr) ) {
			addr = page_address(page_array[pg_inx]);
			prev_pg_inx = pg_inx;
		}

		memcpy( addr + pg_ofs, databuff + (sect_inx<<SECTOR_SHIFT), SECTOR_SIZE );
	}
	blk_deferred_complete( dio_req, size_sector, SUCCESS );
}

blk_deferred_request_t* blk_deferred_request_new( void )
{
	blk_deferred_request_t* dio_req = NULL;

	dio_req = kzalloc( sizeof( blk_deferred_request_t ), GFP_NOIO );
	if (dio_req == NULL)
		return NULL;

	INIT_LIST_HEAD( &dio_req->dios );

	dio_req->result = SUCCESS;
	atomic64_set( &dio_req->sect_processed, 0 );
	dio_req->sect_len = 0;
	init_completion( &dio_req->complete );

	return dio_req;
}

bool blk_deferred_request_already_added( blk_deferred_request_t* dio_req, unsigned long block_index )
{
	bool result = false;
	struct list_head* _list_head;

	if (list_empty( &dio_req->dios ))
		return result;

	list_for_each( _list_head, &dio_req->dios ) {
		blk_deferred_t* dio = list_entry( _list_head, blk_deferred_t, link );

		if (dio->blk_index == block_index){
			result = true;
			break;
		}
	}

	return result;
}

int blk_deferred_request_add( blk_deferred_request_t* dio_req, blk_deferred_t* dio )
{
	list_add_tail( &dio->link, &dio_req->dios );
	dio_req->sect_len += dio->sect.cnt;

	return SUCCESS;
}

void blk_deferred_request_free( blk_deferred_request_t* dio_req )
{
	if (dio_req != NULL){
		while (!list_empty( &dio_req->dios )){
			blk_deferred_t* dio = list_entry( dio_req->dios.next, blk_deferred_t, link );

			list_del( &dio->link );

			blk_deferred_free( dio );
		}
		kfree( dio_req );
	}
}

void blk_deferred_request_waiting_skip( blk_deferred_request_t* dio_req )
{
	init_completion( &dio_req->complete );
	atomic64_set( &dio_req->sect_processed, 0 );
}

int blk_deferred_request_wait( blk_deferred_request_t* dio_req )
{
	u64 start_jiffies = get_jiffies_64( );
	u64 current_jiffies;

	while (0 == wait_for_completion_timeout( &dio_req->complete, (HZ * 1) )) {
		current_jiffies = get_jiffies_64( );
		if (jiffies_to_msecs( current_jiffies - start_jiffies ) > 60 * 1000) {
			pr_warn( "Defer IO request timeout\n" );
			return -EDEADLK;
		}
	}

	return dio_req->result;
}

int blk_deferred_request_read_original( struct block_device* original_blk_dev, blk_deferred_request_t* dio_copy_req )
{
	int res = -ENODATA;
	struct list_head* _list_head;

	blk_deferred_request_waiting_skip( dio_copy_req );

	if (list_empty( &dio_copy_req->dios ))
		return res;

	list_for_each( _list_head, &dio_copy_req->dios ) {
		blk_deferred_t* dio = list_entry( _list_head, blk_deferred_t, link );

		sector_t ofs = dio->sect.ofs;
		sector_t cnt = dio->sect.cnt;

		if (cnt != blk_deferred_submit_pages( original_blk_dev, dio_copy_req, READ, 0, dio->page_array, ofs, cnt )){
			pr_err( "Failed to submit reading defer IO request. offset=%lld\n", dio->sect.ofs );
			res = -EIO;
			break;
		}
		else
			res = SUCCESS;
	}

	if (res == SUCCESS)
		res = blk_deferred_request_wait( dio_copy_req );

	return res;
}

int blk_deferred_request_store_file( struct block_device* blk_dev, blk_deferred_request_t* dio_copy_req )
{
	int res = SUCCESS;
	struct list_head* _dio_list_head;
	struct list_head* _rangelist_head;

	blk_deferred_request_waiting_skip( dio_copy_req );

	BUG_ON(list_empty( &dio_copy_req->dios ));
	list_for_each( _dio_list_head, &dio_copy_req->dios ){
		blk_deferred_t* dio = list_entry( _dio_list_head, blk_deferred_t, link );
		sector_t page_array_ofs = 0;
		struct blk_descr_file *blk_descr = dio->blk_descr.file;

		BUG_ON(list_empty( &blk_descr->rangelist ));
		list_for_each( _rangelist_head, &blk_descr->rangelist ) {
			sector_t process_sect;
			blk_range_link_t *range_link = list_entry( _rangelist_head, blk_range_link_t, link );

			//BUG_ON( NULL == dio->page_array );
			process_sect = blk_deferred_submit_pages( blk_dev, dio_copy_req, WRITE, page_array_ofs, dio->page_array, range_link->rg.ofs, range_link->rg.cnt );
			if (range_link->rg.cnt != process_sect){
				pr_err( "Failed to submit defer IO request for storing. ofs=%lld\n", dio->sect.ofs );
				res = -EIO;
				break;
			}
			page_array_ofs += range_link->rg.cnt;
		}


		if (res != SUCCESS)
			break;
	}


	if (res != SUCCESS)
		return res;

	res = blk_deferred_request_wait( dio_copy_req );
	return res;
}

#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV
int blk_deferred_request_store_multidev( blk_deferred_request_t* dio_copy_req )
{
	int res = SUCCESS;
	struct list_head* _dio_list_head;
	struct list_head* _ranges_list_head;

	blk_deferred_request_waiting_skip( dio_copy_req );

	BUG_ON(list_empty( &dio_copy_req->dios ));
	list_for_each( _dio_list_head, &dio_copy_req->dios ){
		blk_deferred_t* dio = list_entry( _dio_list_head, blk_deferred_t, link );
		sector_t page_array_ofs = 0;
		struct blk_descr_multidev* blk_descr = dio->blk_descr.multidev;

		BUG_ON(list_empty(&blk_descr->rangelist));
		list_for_each( _ranges_list_head, &blk_descr->rangelist ) {
			sector_t process_sect;
			blk_range_link_ex_t* range_link = list_entry( _ranges_list_head, blk_range_link_ex_t, link );

			process_sect = blk_deferred_submit_pages( range_link->blk_dev, dio_copy_req,
				WRITE, page_array_ofs, dio->page_array, range_link->rg.ofs, range_link->rg.cnt );
			if (range_link->rg.cnt != process_sect){
				pr_err( "Failed to submit defer IO request for storing. ofs=%lld\n", dio->sect.ofs );
				res = -EIO;
				break;
			}
			page_array_ofs += range_link->rg.cnt;
		}

		if (res != SUCCESS)
			break;
	}


	if (res != SUCCESS)
		return res;

	res = blk_deferred_request_wait( dio_copy_req );
	return res;
}
#endif


static
size_t _store_pages( void* dst, size_t arr_ofs, struct page **page_array, size_t length )
{
	size_t page_inx = arr_ofs / PAGE_SIZE;

	size_t processed_len = 0;
	void* src;

	{//first
		size_t unordered = arr_ofs & (PAGE_SIZE - 1);
		size_t page_len = min_t( size_t, ( PAGE_SIZE - unordered ), length );

		src = page_address(page_array[page_inx]);
		memcpy( dst + processed_len, src + unordered, page_len );

		++page_inx;
		processed_len += page_len;
	}
	while ( (processed_len < length) && (NULL != page_array[page_inx]) )
	{
		size_t page_len = min_t( size_t, PAGE_SIZE, (length - processed_len) );

		src = page_address(page_array[page_inx]);
		memcpy( dst + processed_len, src, page_len );

		++page_inx;
		processed_len += page_len;
	}

	return processed_len;
}

int blk_deffered_request_store_mem( blk_deferred_request_t* dio_copy_req )
{
	int res = SUCCESS;
	sector_t processed = 0;

	if (!list_empty( &dio_copy_req->dios )){
		struct list_head* _list_head;
		list_for_each( _list_head, &dio_copy_req->dios ){
			blk_deferred_t* dio = list_entry( _list_head, blk_deferred_t, link );
			struct blk_descr_mem *blk_descr = dio->blk_descr.mem;

			size_t portion = _store_pages( blk_descr->buff, 0, dio->page_array, (snapstore_block_size() * SECTOR_SIZE) );
			if (unlikely( portion != (snapstore_block_size() * SECTOR_SIZE) )){
				res = -EIO;
				break;
			}
			processed += (sector_t)to_sectors( portion );
		}
	}

	blk_deferred_complete( dio_copy_req, processed, res );
	return res;
}
