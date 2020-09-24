#pragma once

#include <linux/uuid.h>
#include <linux/kref.h>
#include "blk-snap-ctl.h"
#include "rangevector.h"
#include "snapstore_mem.h"
#include "snapstore_file.h"
#include "snapstore_multidev.h"
#include "blk_redirect.h"
#include "ctrl_pipe.h"
#include "big_buffer.h"

typedef struct snapstore_s {
	struct list_head link;
	struct kref refcount;

	uuid_t id;

	snapstore_mem_t *mem;
	snapstore_file_t *file;
#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV
	snapstore_multidev_t *multidev;
#endif

	ctrl_pipe_t *ctrl_pipe;
	sector_t empty_limit;

	volatile bool halffilled;
	volatile bool overflowed;
} snapstore_t;

void snapstore_done(void);

int snapstore_create(uuid_t *id, dev_t snapstore_dev_id, dev_t *dev_id_set,
		     size_t dev_id_set_length);
#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV
int snapstore_create_multidev(uuid_t *id, dev_t *dev_id_set, size_t dev_id_set_length);
#endif
int snapstore_cleanup(uuid_t *id, u64 *filled_bytes);

snapstore_t *snapstore_get(snapstore_t *snapstore);
void snapstore_put(snapstore_t *snapstore);

int snapstore_stretch_initiate(uuid_t *unique_id, ctrl_pipe_t *ctrl_pipe, sector_t empty_limit);

int snapstore_add_memory(uuid_t *id, unsigned long long sz);
int snapstore_add_file(uuid_t *id, struct big_buffer *ranges, size_t ranges_cnt);
#ifdef CONFIG_BLK_SNAP_SNAPSTORE_MULTIDEV
int snapstore_add_multidev(uuid_t *id, dev_t dev_id, struct big_buffer *ranges, size_t ranges_cnt);
#endif

void snapstore_order_border(struct blk_range *in, struct blk_range *out);

union blk_descr_unify snapstore_get_empty_block(snapstore_t *snapstore);

int snapstore_request_store(snapstore_t *snapstore, blk_deferred_request_t *dio_copy_req);

int snapstore_redirect_read(blk_redirect_bio_t *rq_redir, snapstore_t *snapstore,
			    union blk_descr_unify blk_descr, sector_t target_pos, sector_t rq_ofs,
			    sector_t rq_count);
int snapstore_redirect_write(blk_redirect_bio_t *rq_redir, snapstore_t *snapstore,
			     union blk_descr_unify blk_descr, sector_t target_pos, sector_t rq_ofs,
			     sector_t rq_count);

int snapstore_check_halffill(uuid_t *unique_id, sector_t *fill_status);
