/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

blk_qc_t filter_submit_original_bio(struct bio *bio);

int filter_init(void);
void filter_done(void);
