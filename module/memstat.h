/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023 Veeam Software Group GmbH */
#ifndef __BLKSNAP_MEM_H
#define __BLKSNAP_MEM_H

#ifdef BLKSNAP_MEMSTAT

void memstat_done(void);
void memstat_print(void);
void memstat_enable(int state);

#define ms_kmalloc(size, gfp) \
	memstat_kmalloc(__FILE__, __LINE__, size, gfp)
#define ms_kzalloc(size, gfp) \
	memstat_kmalloc(__FILE__, __LINE__, size, gfp | __GFP_ZERO)
#define ms_kfree(ptr) \
	memstat_kfree(ptr)

void *memstat_kmalloc(const char *key_file, const int key_line, size_t size, gfp_t flags);
void memstat_kfree(void *ptr);

#else /*BLKSNAP_MEMSTAT*/

#define ms_kmalloc(size, gfp) \
	kmalloc(size, gfp)
#define ms_kzalloc(size, gfp) \
	kzalloc(size, gfp)
#define ms_kfree(ptr) \
	kfree(ptr)

#endif /*BLKSNAP_MEMSTAT*/

#endif /* __BLKSNAP_MEM_H */
