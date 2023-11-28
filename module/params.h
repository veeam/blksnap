/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023 Veeam Software Group GmbH */
#ifndef __BLKSNAP_PARAMS_H
#define __BLKSNAP_PARAMS_H

unsigned int get_tracking_block_minimum_shift(void);
unsigned int get_tracking_block_maximum_shift(void);
unsigned int get_tracking_block_maximum_count(void);
unsigned int get_chunk_minimum_shift(void);
unsigned int get_chunk_maximum_shift(void);
unsigned long get_chunk_maximum_count(void);
unsigned int get_chunk_maximum_in_queue(void);
unsigned int get_free_diff_buffer_pool_size(void);
sector_t get_diff_storage_minimum(void);

#endif /* __BLKSNAP_PARAMS_H */
