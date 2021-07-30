// SPDX-License-Identifier: GPL-2.0
#define BLK_SNAP_SECTION "-cache"
#include <linux/blk_types.h>
#include <linux/blk.h>
#include <linux/blkdev.h>
#include "common.h"
#include "cache.h"


void diff_buffer_free(struct kref *kref)
{
	struct diff_buffer *diff_buffer = container_of(kref, struct diff_buffer, kref);
	struct page_list *curr_page = diff_buffer.pages;
	
	while(curr_page) {
		if (curr_page->page)
			free_page((unsigned long)page_address(curr_page->page));

		curr_page = curr_page->next;
	}
	kfree(diff_buffer);
}

struct diff_buffer *diff_buffer_new(size_t page_count, gfp_t gfp_mask)
{
	int ret = 0;
	struct diff_buffer *buf;
	size_t inx;
	struct page_list *prev_page = NULL;

	buf = kcalloc(page_count, sizeof(struct page_list), GFP_NOIO);
	if (!buf)
		return NULL;

	kref_init(&buf->kref);
	while(inx < CHUNK_PAGES) {
		struct page *page;

		page = alloc_page(GFP_NOIO);
		if (!page) {
			ret = -ENOMEM;
			break;
		}
		buf.pages[inx].page = page;

		if (inx)
			prev_page->next = buf.pages + inx;
		prev_page = buf.pages + inx;
		inx++;		
	}
	pr_debug("allocate %d pages\n", inx);
}

static void __chunk_free(struct chunk *chunk)
{
	if (!chunk)
		return;

	if (chunk->diff_buffer) 
		diff_buffer_put(chunk->diff_buffer);

	if (chunk->diff_store)
		diff_store_free(chunk->diff_store);

	kfree(chunk);
}

struct chunk *chunk_alloc(struct diff_area *diff_area, unsigned long number)
{
	int ret = 0;
	struct chunk *chunk;

	pr_debug("allocate chunk sz=%ld\n", sizeof(struct chunk));
	chunk = kzalloc(sizeof(struct chunk), GFP_NOIO);
	if (!chunk)
		return NULL;

	kref_init(&chunk->refcount);
	INIT_LIST_HEAD(&chunk->link);
	init_rwsem(&chunk->lock);
	chunk->diff_area = diff_area;
	chunk->number = number;

	return chunk;
}

void chunk_free(struct kref *kref)
{
	__chunk_free(container_of(kref, struct chunk, refcount));
}



LIST_HEAD(shared_chunk_cache);
DEFINE_SPINLOCK(shared_chunk_cache_lock);
sector_t shared_chunk_cache_count;

static void __flush_chunk(struct chunk *chunk)
{
	struct diff_buffer *diff_buffer = NULL;

	down_write(&chunk->buffer_lock);
	if (chunk_state_check(chunk, CHUNK_ST_DIRTY)) {
		diff_buffer = chunk->diff_buffer;
		chunk->diff_buffer = NULL;
		chunk_state_unset(chunk, CHUNK_ST_BUFFER_READY);

		if (diff_buffer)
			diff_buffer_put(diff_buffer);
	}
	up_write(&chunk->buffer_lock);
}

void chunk_cache_store(struct chunk *chunk)
{
	chunk_get(chunk);

	spin_lock(&shared_chunk_cache_lock);

	list_add_tail(&chunk->link, shared_chunk_cache);
	chunk = NULL;

	shared_chunk_cache_count++;
	if (shared_chunk_cache_length > CONFIG_BLK_SNAP_MAXIMUM_CHUNK_IN_CACHE) {
		chunk = list_first_entry(&shared_chunk_cache, struct chunk, link);
		list_del(&chunk->link);
		shared_chunk_cache_count--;
	}

	spin_unlock(&shared_chunk_cache_lock);

	if (chunk) {
		__flush_chunk(chunk);
		chunk_put(chunk);
		return;
	}
}

void chunk_cache_del(struct chunk *chunk)
{
	spin_lock(&shared_chunk_cache_lock);

	list_del(&chunk->link);
	
	spin_unlock(&shared_chunk_cache_lock);
}

void chunk_cache_touch(struct chunk *chunk)
{
	spin_lock(&shared_chunk_cache_lock);

	list_del(&chunk->link);
	list_add_tail(&chunk->link, shared_chunk_cache_lock)
	
	spin_unlock(&shared_chunk_cache_lock);
}

void chunk_cache_flush(struct chunk *chunk)
{
	struct chunk *chunk = NULL;

	while (1) {
		spin_lock(&shared_chunk_cache_lock);
		chunk = list_first_entry_or_null(&shared_chunk_cache, struct chunk, link);
		if (!chunk) {
			spin_unlock(&shared_chunk_cache_lock);
			break;
		}
		list_del(chunk->link);
		shared_chunk_cache_count--;
		spin_unlock(&shared_chunk_cache_lock);

		__flush_chunk(chunk);
		chunk_put(chunk);
	}


}
