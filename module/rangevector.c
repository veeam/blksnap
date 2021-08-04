// SPDX-License-Identifier: GPL-2.0
#include "common.h"
#include "rangevector.h"

#define SECTION "ranges	"

static inline sector_t range_node_start(struct blk_range_tree_node *range_node)
{
	return range_node->range.ofs;
}

static inline sector_t range_node_last(struct blk_range_tree_node *range_node)
{
	return range_node->range.ofs + range_node->range.cnt - 1;
}

#ifndef INTERVAL_TREE_DEFINE
#pragma message("INTERVAL_TREE_DEFINE is undefined")
#endif
INTERVAL_TREE_DEFINE(struct blk_range_tree_node, _node, sector_t, _subtree_last,
		     range_node_start, range_node_last, static inline, _blk_range_rb)

void blk_range_rb_insert(struct blk_range_tree_node *node, struct rb_root_cached *root)
{
	_blk_range_rb_insert(node, root);
}

void blk_range_rb_remove(struct blk_range_tree_node *node, struct rb_root_cached *root)
{
	_blk_range_rb_remove(node, root);
}

struct blk_range_tree_node *blk_range_rb_iter_first(struct rb_root_cached *root, sector_t start,
						    sector_t last)
{
	return _blk_range_rb_iter_first(root, start, last);
}

struct blk_range_tree_node *blk_range_rb_iter_next(struct blk_range_tree_node *node, sector_t start,
						   sector_t last)
{
	return _blk_range_rb_iter_next(node, start, last);
}

void rangevector_init(struct rangevector *rangevector)
{
	init_rwsem(&rangevector->lock);

	rangevector->root = RB_ROOT_CACHED;
}

void rangevector_done(struct rangevector *rangevector)
{
	struct rb_node *rb_node = NULL;

	down_write(&rangevector->lock);
	rb_node = rb_first_cached(&rangevector->root);
	while (rb_node) {
		struct blk_range_tree_node *range_node = (struct blk_range_tree_node *)
			rb_node; //container_of(rb_node, struct blk_range_tree_node, node);

		blk_range_rb_remove(range_node, &rangevector->root);
		kfree(range_node);

		rb_node = rb_first_cached(&rangevector->root);
	}
	up_write(&rangevector->lock);
}

int rangevector_add(struct rangevector *rangevector, struct blk_range *rg)
{
	struct blk_range_tree_node *range_node;

	range_node = kzalloc(sizeof(struct blk_range_tree_node), GFP_KERNEL);
	if (range_node)
		return -ENOMEM;

	range_node->range = *rg;

	down_write(&rangevector->lock);
	blk_range_rb_insert(range_node, &rangevector->root);
	up_write(&rangevector->lock);

	return 0;
}
