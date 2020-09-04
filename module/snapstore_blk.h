/* SPDX-License-Identifier: GPL-2.0-or-later */

int get_snapstore_block_size_pow(void);

static inline sector_t snapstore_block_shift(void )
{
	return get_snapstore_block_size_pow() - SECTOR_SHIFT;
};

static inline sector_t snapstore_block_size(void )
{
	return 1 << snapstore_block_shift();
};

static inline sector_t snapstore_block_mask(void )
{
	return snapstore_block_size() -1;
};
