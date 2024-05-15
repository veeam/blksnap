// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 Veeam Software Group GmbH */
#define pr_fmt(fmt) KBUILD_MODNAME "-mem: " fmt
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/refcount.h>
#include "memstat.h"
#ifdef BLKSNAP_FILELOG
#include "log.h"
#endif

#ifdef BLKSNAP_MEMSTAT

struct memstat_class {
	const char *file;
	int line;
	atomic64_t count;
	atomic64_t total_size;
};

struct memstat_obj {
	struct memstat_class *class;
	size_t size;
};

static atomic64_t memstat_kcnt;
static atomic64_t memstat_ksize;
static atomic_t memstat_state;
static DEFINE_XARRAY(memstat_class_map);

void memstat_done(void)
{
	unsigned long inx = 0;
	struct memstat_class *class;

	if (atomic64_read(&memstat_kcnt))
		pr_err("Critical error! Memory leak found.\nNumber of objects used %lld total %lld bytes.\n",
			atomic64_read(&memstat_kcnt),
			atomic64_read(&memstat_ksize));

	xa_for_each(&memstat_class_map, inx, class) {
		if (likely(class)) {
			if (atomic64_read(&class->count)) {
				pr_err("%s:%d count: %lld total: %lld\n",
					class->file, class->line,
					atomic64_read(&class->count),
					atomic64_read(&class->total_size));
			}
			kfree(class);
		}
	}
	xa_destroy(&memstat_class_map);
}

void memstat_print(void)
{
	unsigned long inx = 0;
	struct memstat_class *class;

	pr_debug("Number of objects used %lld total %lld bytes.\n",
		atomic64_read(&memstat_kcnt),
		atomic64_read(&memstat_ksize));

	if (!atomic_read(&memstat_state))
		return;

	xa_for_each(&memstat_class_map, inx, class) {
		if (likely(class) && atomic64_read(&class->count)) {
			pr_debug("%s:%d count: %lld total: %lld\n",
				class->file, class->line,
				atomic64_read(&class->count),
				atomic64_read(&class->total_size));
		}
	}
}

void memstat_enable(int state)
{
	atomic_set(&memstat_state, state);
}

void *memstat_kmalloc(const char *file, const int line, size_t size, gfp_t flags)
{
	void *ptr;
	int ret;
	unsigned long inx;
	struct memstat_class *class;
	struct memstat_obj *obj;

	ptr = kmalloc(size + sizeof(struct memstat_obj), flags);
	if (unlikely(!ptr))
		return NULL;

	obj = ptr;
	ptr += sizeof(struct memstat_obj);

	obj->class = NULL;
	obj->size = size;
	atomic64_inc(&memstat_kcnt);
	atomic64_add(size, &memstat_ksize);
	if (!atomic_read(&memstat_state))
		return ptr;

#ifdef CONFIG_64BIT
	inx = (unsigned long)(file) & 0x0000FFFFffffffff;
	inx |= (unsigned long)(line) << 48;
#else
	inx = (unsigned long)(file) & 0x00ffffff;
	inx |= (unsigned long)(line) << 24;
#endif

	class = xa_load(&memstat_class_map, inx);
	if (!class) {
		class = kmalloc(sizeof(struct memstat_class), GFP_KERNEL);
		if (unlikely(!class))
			goto out;

		class->file = file;
		class->line = line;
		atomic64_set(&class->count, 0);
		atomic64_set(&class->total_size, 0);

		ret = xa_insert(&memstat_class_map, inx, class, GFP_KERNEL);
		if (unlikely(ret)) {
			if (ret == -EBUSY) {
				kfree(class);
				class = xa_load(&memstat_class_map, inx);
				if (unlikely(!class))
					goto out;
			} else
				goto out;
		}
	}

	obj->class = class;
	atomic64_inc(&class->count);
	atomic64_add(size, &class->total_size);
out:
	return ptr;
}

void memstat_kfree(void *ptr)
{
	struct memstat_obj *obj;

	if (unlikely(!ptr))
		return;

	ptr -= sizeof(struct memstat_obj);
	obj = ptr;

	if (obj->class) {
		atomic64_dec(&obj->class->count);
		atomic64_sub(obj->size, &obj->class->total_size);
	}
	atomic64_dec(&memstat_kcnt);
	atomic64_sub(obj->size, &memstat_ksize);
	return kfree(ptr);
}

#endif /*BLKSNAP_MEMSTAT*/
