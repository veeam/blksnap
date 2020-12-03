/* SPDX-License-Identifier: GPL-2.0-or-later */

enum cow_state {
	cow_state_corrupted = 0,
	cow_state_empty,
	cow_state_reading,
	cow_state_was_read,
	cow_state_writing,
	cow_state_was_written
};

struct cow_block {
	spinlock_t state_lock;
	enum cow_state state;
	//struct kref kref;
	atomic_t bio_cnt;

	struct completion complete;

	struct snapstore_device *snapstore_device;
	struct blk_range rg; //offset in original block device and real sectors count
	struct page **page_array; //null pointer on tail

	union blk_descr_unify blk_descr;
};

int cow_block_update_state(struct cow_block *blk,
			   enum cow_state from_state, enum cow_state to_state);
void cow_blk_corrupted(struct cow_block *cow_block, int err);
int  cow_block_wait(struct cow_block *blk);
void cow_block_empty(struct cow_block *blk);
void cow_block_free(struct cow_block *blk);
struct cow_block *cow_block_new(struct snapstore_device *snapstore_device, struct blk_range rg);
