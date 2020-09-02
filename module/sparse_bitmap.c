#include "stdafx.h"

#include "sparse_bitmap.h"
#include "mem_alloc.h"

#define SECTION "sparse_array"
#include "log_format.h"

#define BLK_ST_EMPTY 0
#define BLK_ST_USE   1
#define BLK_ST_FULL  2

struct kmem_cache* g_sparse_block_cache = NULL;

int sparsebitmap_init( void )
{
#ifdef SPARSE_BLOCK_CACHEABLE
    g_sparse_block_cache = kmem_cache_create( "veeamsnap_sparse_bitmap", sizeof( blocks_array_t ), 0, 0, NULL );
    if (g_sparse_block_cache == NULL){
        log_tr( "Unable create kmem_cache" );
        return -ENOMEM;
    }
#endif
    return SUCCESS;
}

void  sparsebitmap_done( void )
{
#ifdef SPARSE_BLOCK_CACHEABLE
    if (g_sparse_block_cache != NULL){
        kmem_cache_destroy( g_sparse_block_cache );
        g_sparse_block_cache = NULL;
    }
#endif
}

static inline blocks_array_t* _sparse_block_array_new( int init_value )
{
    blocks_array_t* blocks_array = NULL;
#ifdef SPARSE_BLOCK_CACHEABLE
    blocks_array = kmem_cache_alloc( g_sparse_block_cache, GFP_NOIO );
#else
    blocks_array = dbg_kmalloc( sizeof( blocks_array_t ), GFP_NOIO );
#endif

    if (blocks_array == NULL)
        return NULL;

    memset( blocks_array, init_value, sizeof( blocks_array_t ) );
    return blocks_array;
}

static inline void _sparse_block_array_free( blocks_array_t* blocks_array )
{
    if (blocks_array != NULL){
#ifdef SPARSE_BLOCK_CACHEABLE
        kmem_cache_free( g_sparse_block_cache, blocks_array );
#else
        dbg_kfree( blocks_array );
#endif
    }
}


void _sparse_block_init( sparse_block_t* block, char level, void* block_state )
{
    block->level = level;

    if (block_state == BLOCK_EMPTY){
        block->fill_count = 0;
        block->cnt_full = 0;
    }
    else{
        block->fill_count = SPARSE_BITMAP_BLOCK_SIZE;
        block->cnt_full = SPARSE_BITMAP_BLOCK_SIZE;
    }

    block->blocks_array = block_state;
}

sparse_block_t* _sparse_block_create( char level, void* block_state )
{
    sparse_block_t* block = dbg_kmalloc( sizeof( sparse_block_t ), GFP_NOIO );
    if (NULL == block)
        log_err( "Failed to allocate memory for sparse bitmap block" );
    else
        _sparse_block_init( block, level, block_state );
    return block;
}

void _sparse_block_destroy( sparse_block_t* block )
{
    if (NULL != block)
        dbg_kfree( block );
}

void _sparse_block_free( sparse_block_t* block )
{
    if (block->level == 0){
        block->bit_block = 0;
        block->fill_count = 0;
    }
    else{
        if ((block->blocks_array != BLOCK_EMPTY) && (block->blocks_array != BLOCK_FULL)){
            int inx;
            for (inx = 0; inx < SPARSE_BITMAP_BLOCK_SIZE; inx++){

                if ((block->blocks_array->blk[inx] != BLOCK_FULL) && (block->blocks_array->blk[inx] != BLOCK_EMPTY)){
                    _sparse_block_free( block->blocks_array->blk[inx] );

                    _sparse_block_destroy( block->blocks_array->blk[inx] );
                    block->blocks_array->blk[inx] = NULL;
                }
            }

            _sparse_block_array_free( block->blocks_array );
            block->blocks_array = NULL;

            block->fill_count = 0;
            block->cnt_full = 0;
        }
    }
}

int _sparse_block_clear(sparse_block_t* block, stream_size_t index, char* p_blk_st);

int _sparse_block_clear_leaf(sparse_block_t* block, stream_size_t index, char* p_blk_st)
{
    char blk_st = BLK_ST_USE;
    int res = SUCCESS;

    size_t inx = (size_t)(index  & SPARSE_BITMAP_BLOCK_SIZE_MASK);
    size_t bit_mask = ((size_t)(1) << inx);

    if (block->bit_block & bit_mask){//is set
        block->bit_block &= ~bit_mask;
        --block->fill_count;
    }
    else
        res = -EALREADY;

    if (block->fill_count == 0)
        blk_st = BLK_ST_EMPTY;

    *p_blk_st = blk_st;
    return res;
}

int _sparse_block_clear_branch(sparse_block_t* block, stream_size_t index, char* p_blk_st)
{
    char blk_st = BLK_ST_USE;
    int res = SUCCESS;

    do{
        size_t inx = (size_t)(index >> (SPARSE_BITMAP_BLOCK_SIZE_DEGREE * block->level)) & SPARSE_BITMAP_BLOCK_SIZE_MASK;

        if (block->blocks_array == BLOCK_EMPTY){
            blk_st = BLK_ST_EMPTY;
            break;
        }

        if (block->blocks_array == BLOCK_FULL){
            blocks_array_t* blocks_array = _sparse_block_array_new( 0xFF );//all blocks is full
            if (blocks_array)
                block->blocks_array = blocks_array;
            else{
                res = -ENOMEM;
                break;
            }

        }

        if (block->blocks_array->blk[inx] == BLOCK_EMPTY)
            break; //already empty

        if (block->blocks_array->blk[inx] == BLOCK_FULL){
            block->blocks_array->blk[inx] = _sparse_block_create( block->level - 1, BLOCK_FULL );
            if (block->blocks_array->blk[inx] == NULL){
                res = -ENOMEM;
                break;
            }
            --block->cnt_full;
        }

        {
            char sub_blk_state;
            res = _sparse_block_clear( block->blocks_array->blk[inx], index, &sub_blk_state );
            if (res != SUCCESS)
                break;

            if (sub_blk_state == BLK_ST_EMPTY){
                _sparse_block_destroy( block->blocks_array->blk[inx] );
                block->blocks_array->blk[inx] = BLOCK_EMPTY;
                --block->fill_count;

                if (block->fill_count == 0){
                    blk_st = BLK_ST_EMPTY;

                    _sparse_block_array_free( block->blocks_array );
                    block->blocks_array = BLOCK_EMPTY;
                }
            }
        }

    } while (false);

    *p_blk_st = blk_st;
    return res;
}

int _sparse_block_clear( sparse_block_t* block, stream_size_t index, char* p_blk_st )
{
    if (block->level == 0)
        return _sparse_block_clear_leaf(block, index, p_blk_st);
    else
        return _sparse_block_clear_branch(block, index, p_blk_st);
}

int _sparse_block_set(sparse_block_t* block, stream_size_t index, char* p_blk_st);

int _sparse_block_set_leaf(sparse_block_t* block, stream_size_t index, char* p_blk_st)
{
    char blk_st = BLK_ST_USE;
    int res = SUCCESS;

    size_t inx = (size_t)(index & SPARSE_BITMAP_BLOCK_SIZE_MASK);
    size_t bit_mask = ((size_t)(1) << inx);

    if ((block->bit_block & bit_mask) == 0){//is non set
        block->bit_block |= bit_mask;
        ++block->fill_count;
    }
    else
        res = -EALREADY;


    if (block->fill_count == SPARSE_BITMAP_BLOCK_SIZE)
        blk_st = BLK_ST_FULL;

    *p_blk_st = blk_st;
    return res;
}

int _sparse_block_set_branch(sparse_block_t* block, stream_size_t index, char* p_blk_st)
{
    char blk_st = BLK_ST_USE;
    int res = SUCCESS;

    do{
        size_t inx = (size_t)(index >> (stream_size_t)(SPARSE_BITMAP_BLOCK_SIZE_DEGREE * block->level)) & SPARSE_BITMAP_BLOCK_SIZE_MASK;

        if (block->blocks_array == BLOCK_FULL){
            res = -EALREADY;
            break;
        }

        if (block->blocks_array == BLOCK_EMPTY){
            blocks_array_t* blocks_array = _sparse_block_array_new(0x00); //all blocks is empty
            if (blocks_array)
                block->blocks_array = blocks_array;
            else{
                res = -ENOMEM;
                break;
            }
        }
        if (block->blocks_array->blk[inx] == BLOCK_FULL){
            res = -EALREADY;
            break; //already full
        }

        if (block->blocks_array->blk[inx] == BLOCK_EMPTY){
            block->blocks_array->blk[inx] = _sparse_block_create(block->level - 1, BLOCK_EMPTY);
            if (block->blocks_array->blk[inx] == NULL){
                res = -ENOMEM;
                break;
            }
            ++block->fill_count;
        }

        {
            char sub_blk_st;
            res = _sparse_block_set(block->blocks_array->blk[inx], index, &sub_blk_st);
            if (res != SUCCESS)
                break;

            if (sub_blk_st == BLK_ST_FULL){
                //log_err_llx( "block full. index=", index );

                _sparse_block_destroy(block->blocks_array->blk[inx]);
                block->blocks_array->blk[inx] = BLOCK_FULL;
                ++block->cnt_full;

                if (block->cnt_full == SPARSE_BITMAP_BLOCK_SIZE){
                    //log_err_llx( "block array full. index=", index );

                    blk_st = BLK_ST_FULL;

                    _sparse_block_array_free(block->blocks_array);
                    block->blocks_array = BLOCK_FULL;
                }
            }
        }

    } while (false);

    *p_blk_st = blk_st;

    return res;
}

int _sparse_block_set( sparse_block_t* block, stream_size_t index, char* p_blk_st )
{
    if (block->level == 0)
        return _sparse_block_set_leaf(block, index, p_blk_st);
    else
        return _sparse_block_set_branch(block, index, p_blk_st);
}

bool _sparse_block_get( sparse_block_t* block, stream_size_t index )
{
    size_t inx;

    if (block->level == 0){
        size_t inx = (size_t)(index  & SPARSE_BITMAP_BLOCK_SIZE_MASK);
        size_t bit_mask = ((size_t)(1) << inx);

        return ((block->bit_block & bit_mask) != 0);
    }

    if (block->blocks_array == BLOCK_FULL)
        return true;

    if (block->blocks_array == BLOCK_EMPTY)
        return false;

    inx = (size_t)(index >> (SPARSE_BITMAP_BLOCK_SIZE_DEGREE * block->level)) & SPARSE_BITMAP_BLOCK_SIZE_MASK;
    if (block->blocks_array->blk[inx] == BLOCK_FULL)
        return true;

    if (block->blocks_array->blk[inx] == BLOCK_EMPTY)
        return false;

    return _sparse_block_get( block->blocks_array->blk[inx], index );
    }

char _calc_level( stream_size_t ull )
{
    char level = 0;
    while (ull > SPARSE_BITMAP_BLOCK_SIZE){
        ull = ull >> SPARSE_BITMAP_BLOCK_SIZE_DEGREE;
        level++;
    }
    return level;
}


void sparsebitmap_create( sparse_bitmap_t* bitmap, stream_size_t min_index, stream_size_t length )
{
    char level = _calc_level( length );
    bitmap->start_index = min_index;
    bitmap->length = length;

    log_tr_format( "Create sparse bitmap. Start index %lld, length %lld, levels %d", bitmap->start_index, bitmap->length, level );

    _sparse_block_init( &(bitmap->sparse_block), level, BLOCK_EMPTY );
}

void sparsebitmap_destroy( sparse_bitmap_t* bitmap )
{
    sparsebitmap_Clean( bitmap );
}


int sparsebitmap_Set( sparse_bitmap_t* bitmap, stream_size_t index, bool state )
{
    char blk_st;

    if ((index < bitmap->start_index) || (index >= (bitmap->start_index + bitmap->length))){
        log_err_lld( "Unable to set sparse bitmap bit: invalid index ", index );
        return -EINVAL;
    }
    index = index - bitmap->start_index;

    if (state)
        return _sparse_block_set( &bitmap->sparse_block, index, &blk_st );
    else
        return _sparse_block_clear( &bitmap->sparse_block, index, &blk_st );
}

int sparsebitmap_Get( sparse_bitmap_t* bitmap, stream_size_t index, bool* p_state )
{
    if ((index < bitmap->start_index) || (index >= (bitmap->start_index + bitmap->length)))
        return -EINVAL;
    index = index - bitmap->start_index;

    *p_state = _sparse_block_get( &bitmap->sparse_block, index );
    return SUCCESS;
}

void sparsebitmap_Clean( sparse_bitmap_t* bitmap )
{
    _sparse_block_free( &bitmap->sparse_block );
    bitmap->length = 0;
    bitmap->start_index = 0;
}

int _sparse_block_get_ranges_leaf( sparse_block_t* block, rangelist_t* rangelist, sector_t* index, range_t* rg )
{
    int res = SUCCESS;
    size_t inx;

    for (inx = 0; inx < SPARSE_BITMAP_BLOCK_SIZE; ++inx){
        size_t bit_mask = ((size_t)(1) << inx);
        if ((block->bit_block & bit_mask) != 0){
            if (range_is_empty( rg ))
                rg->ofs = *index;
            ++rg->cnt;
        }
        else{
            if (!range_is_empty( rg )){
                res = rangelist_add( rangelist, rg );
                *rg = range_empty( );
            }
        }

        ++*index;
    }
    return res;
}

void _sparse_block_get_ranges_full( sparse_block_t* block, sector_t* index, range_t* rg )
{
    sector_t block_size = 1ull << (SPARSE_BITMAP_BLOCK_SIZE_DEGREE * block->level);

    if (range_is_empty( rg ))
        rg->ofs = *index;
    rg->cnt += block_size;

    *index += block_size;
}

int _sparse_block_get_ranges_empty( sparse_block_t* block, sector_t* index, range_t* rg, rangelist_t* rangelist )
{
    int res = SUCCESS;
    sector_t block_size = 1ull << (SPARSE_BITMAP_BLOCK_SIZE_DEGREE * block->level);

    if (!range_is_empty( rg )){
        res = rangelist_add( rangelist, rg );
        *rg = range_empty( );
    }

    *index += block_size;

    return res;
}

int _sparse_block_get_ranges( sparse_block_t* block, rangelist_t* rangelist, sector_t* index, range_t* rg );

int _sparse_block_get_ranges_block(sparse_block_t* block, rangelist_t* rangelist, sector_t* index, range_t* rg)
{
    int res = SUCCESS;

    if (block->blocks_array == BLOCK_FULL)
        _sparse_block_get_ranges_full( block, index, rg );
    else if (block->blocks_array == BLOCK_EMPTY)
        res = _sparse_block_get_ranges_empty( block, index, rg, rangelist );
    else {
        size_t inx;

        for (inx = 0; inx < SPARSE_BITMAP_BLOCK_SIZE; ++inx){
            void* blk = block->blocks_array->blk[inx];

            if (blk == BLOCK_FULL)
                _sparse_block_get_ranges_full( block, index, rg );
            else if (blk == BLOCK_EMPTY)
                res = _sparse_block_get_ranges_empty( block, index, rg, rangelist );
            else
                res = _sparse_block_get_ranges( blk, rangelist, index, rg );

            if (res != SUCCESS)
                break;
        }
    }
    return res;
}

int _sparse_block_get_ranges( sparse_block_t* block, rangelist_t* rangelist, sector_t* index, range_t* rg )
{
    if (block->level == 0)
        return _sparse_block_get_ranges_leaf(block, rangelist, index, rg);
    else
        return _sparse_block_get_ranges_block(block, rangelist, index, rg);
}

int sparsebitmap_convert2rangelist( sparse_bitmap_t* bitmap, rangelist_t* rangelist, sector_t start_index )
{
    int res = SUCCESS;
    range_t rg = range_empty( );

    res = _sparse_block_get_ranges( &bitmap->sparse_block, rangelist, &start_index, &rg );
    if (res != SUCCESS)
        return res;

    if (!range_is_empty( &rg ))
        res = rangelist_add( rangelist, &rg );
    return res;
}
