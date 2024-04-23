/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023 Veeam Software Group GmbH */
#ifndef __BLKSNAP_SNAPIMAGE_H
#define __BLKSNAP_SNAPIMAGE_H

struct tracker;

#if !defined(HAVE_BLK_ALLOC_DISK)
int snapimage_init(void);
void snapimage_done(void);
#endif

void snapimage_free(struct tracker *tracker);
int snapimage_create(struct tracker *tracker);
#endif /* __BLKSNAP_SNAPIMAGE_H */
