#pragma once
#include "sector.h"
#include <linux/interval_tree_generic.h>

struct blk_range_tree_node {
	struct rb_node _node;
	struct blk_range range;
	sector_t _subtree_last;
};

void blk_range_rb_insert(struct blk_range_tree_node *node, struct rb_root_cached *root);

void blk_range_rb_remove(struct blk_range_tree_node *node, struct rb_root_cached *root);

struct blk_range_tree_node *blk_range_rb_iter_first(struct rb_root_cached *root, sector_t start,
						    sector_t last);

struct blk_range_tree_node *blk_range_rb_iter_next(struct blk_range_tree_node *node, sector_t start,
						   sector_t last);

typedef struct rangevector_s {
	struct rb_root_cached root;

	struct rw_semaphore lock;
} rangevector_t;

void rangevector_init(rangevector_t *rangevector);

void rangevector_done(rangevector_t *rangevector);

int rangevector_add(rangevector_t *rangevector, struct blk_range *rg);
