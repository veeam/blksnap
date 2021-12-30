// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/slab.h>
#include <linux/uaccess.h>
#ifdef CONFIG_DEBUG_MEMORY_LEAK
#include "memory_checker.h"
#endif
#include "big_buffer.h"

#ifdef CONFIG_DEBUGLOG
#undef pr_debug
#define pr_debug(fmt, ...) \
	printk(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)
#endif

static inline
size_t page_count_calc(size_t buffer_size)
{
	size_t page_count = buffer_size / PAGE_SIZE;

	if (buffer_size & (PAGE_SIZE - 1))
		page_count += 1;
	return page_count;
}

struct big_buffer *big_buffer_alloc(size_t buffer_size, int gfp_opt)
{
	int res = 0;
	struct big_buffer *bbuff;
	size_t count;
	size_t inx;

	count = page_count_calc(buffer_size);

	bbuff = kzalloc(sizeof(struct big_buffer) + count * sizeof(void *), gfp_opt);
	if (bbuff == NULL)
		return NULL;
#ifdef CONFIG_DEBUG_MEMORY_LEAK
	memory_object_inc(memory_object_big_buffer);
#endif
	bbuff->pg_cnt = count;
	for (inx = 0; inx < bbuff->pg_cnt; ++inx) {
		struct page *pg = alloc_page(gfp_opt);

		if (!pg) {
			res = -ENOMEM;
			break;
		}
#ifdef CONFIG_DEBUG_MEMORY_LEAK
		memory_object_inc(memory_object_page);
#endif
		bbuff->pg[inx] = page_address(pg);
	}

	if (res) {
		big_buffer_free(bbuff);
		return NULL;
	}

	return bbuff;
}

void big_buffer_free(struct big_buffer *bbuff)
{
	size_t inx;
	size_t count = bbuff->pg_cnt;

	if (bbuff == NULL)
		return;

	for (inx = 0; inx < count; ++inx)
		if (bbuff->pg[inx] != NULL) {
			free_page((unsigned long)bbuff->pg[inx]);
#ifdef CONFIG_DEBUG_MEMORY_LEAK
			memory_object_dec(memory_object_page);
#endif
		}

	kfree(bbuff);
#ifdef CONFIG_DEBUG_MEMORY_LEAK
	memory_object_dec(memory_object_big_buffer);
#endif
}

size_t big_buffer_copy_to_user(char __user *dst_user, size_t offset,
			       struct big_buffer *bbuff, size_t length)
{
	size_t left_data_length;
	int page_inx = offset / PAGE_SIZE;
	size_t processed_len = 0;
	size_t unordered = offset & (PAGE_SIZE - 1);

	if (unordered) { //first
		size_t page_len = min_t(size_t, (PAGE_SIZE - unordered), length);

		left_data_length = copy_to_user(dst_user + processed_len,
						bbuff->pg[page_inx] + unordered, page_len);
		if (left_data_length) {
			pr_err("Failed to copy data from big_buffer to user buffer\n");
			return processed_len;
		}

		++page_inx;
		processed_len += page_len;
	}

	while ((processed_len < length) && (page_inx < bbuff->pg_cnt)) {
		size_t page_len = min_t(size_t, PAGE_SIZE, (length - processed_len));

		left_data_length =
			copy_to_user(dst_user + processed_len, bbuff->pg[page_inx], page_len);
		if (left_data_length) {
			pr_err("Failed to copy data from big_buffer to user buffer\n");
			break;
		}

		++page_inx;
		processed_len += page_len;
	}

	return processed_len;
}

size_t big_buffer_copy_from_user(const char __user *src_user, size_t offset,
				 struct big_buffer *bbuff, size_t length)
{
	size_t left_data_length;
	int page_inx = offset / PAGE_SIZE;
	size_t processed_len = 0;
	size_t unordered = offset & (PAGE_SIZE - 1);

	if (unordered) { //first
		size_t page_len = min_t(size_t, (PAGE_SIZE - unordered), length);

		left_data_length = copy_from_user(bbuff->pg[page_inx] + unordered,
						  src_user + processed_len, page_len);
		if (left_data_length) {
			pr_err("Failed to copy data from user buffer to big_buffer\n");
			return processed_len;
		}

		++page_inx;
		processed_len += page_len;
	}

	while ((processed_len < length) && (page_inx < bbuff->pg_cnt)) {
		size_t page_len = min_t(size_t, PAGE_SIZE, (length - processed_len));

		left_data_length =
			copy_from_user(bbuff->pg[page_inx], src_user + processed_len, page_len);
		if (left_data_length) {
			pr_err("Failed to copy data from user buffer to big_buffer\n");
			break;
		}

		++page_inx;
		processed_len += page_len;
	}

	return processed_len;
}

void *big_buffer_get_element(struct big_buffer *bbuff, size_t index, size_t sizeof_element)
{
	size_t elements_in_page = PAGE_SIZE / sizeof_element;
	size_t pg_inx = index / elements_in_page;
	size_t pg_ofs = (index - (pg_inx * elements_in_page)) * sizeof_element;

	if (pg_inx >= bbuff->pg_cnt)
		return NULL;

	return bbuff->pg[pg_inx] + pg_ofs;
}

void big_buffer_memset(struct big_buffer *bbuff, int value)
{
	size_t inx;

	for (inx = 0; inx < bbuff->pg_cnt; ++inx)
		memset(bbuff->pg[inx], value, PAGE_SIZE);
}

void big_buffer_memcpy(struct big_buffer *dst, struct big_buffer *src)
{
	size_t inx;
	size_t count = min_t(size_t, dst->pg_cnt, src->pg_cnt);

	for (inx = 0; inx < count; ++inx)
		memcpy(dst->pg[inx], src->pg[inx], PAGE_SIZE);
}

int big_buffer_byte_get(struct big_buffer *bbuff, size_t inx, u8 *value)
{
	size_t page_inx = inx >> PAGE_SHIFT;
	size_t byte_pos = inx & (PAGE_SIZE - 1);

	if (page_inx >= bbuff->pg_cnt)
		return -ENODATA;

	*value = bbuff->pg[page_inx][byte_pos];

	return 0;
}

int big_buffer_byte_set(struct big_buffer *bbuff, size_t inx, u8 value)
{
	size_t page_inx = inx >> PAGE_SHIFT;
	size_t byte_pos = inx & (PAGE_SIZE - 1);

	if (page_inx >= bbuff->pg_cnt)
		return -ENODATA;

	bbuff->pg[page_inx][byte_pos] = value;

	return 0;
}
