/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023 Veeam Software Group GmbH */
#ifndef __BLKSNAP_COMPAT_H
#define __BLKSNAP_COMPAT_H

#include <linux/sched/mm.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/bio.h>
#include <linux/fs.h>

#ifndef PAGE_SECTORS
#define PAGE_SECTORS	(1 << (PAGE_SHIFT - SECTOR_SHIFT))
#endif

#ifndef HAVE_MEMCPY_PAGE
static inline void memcpy_page(struct page *dst_page, size_t dst_off,
			       struct page *src_page, size_t src_off,
			       size_t len)
{
	char *dst = kmap_atomic(dst_page);
	char *src = kmap_atomic(src_page);

	memcpy(dst + dst_off, src + src_off, len);
	kunmap_atomic(src);
	kunmap_atomic(dst);
}
#endif

#ifndef HAVE_BVEC_SET_PAGE
static inline void bvec_set_page(struct bio_vec *bv, struct page *page,
		unsigned int len, unsigned int offset)
{
	bv->bv_page = page;
	bv->bv_len = len;
	bv->bv_offset = offset;
}
#endif

#if defined(HAVE_SUPER_BLOCK_FREEZE)
static inline int _freeze_bdev(struct block_device *bdev,
			       struct super_block **psuperblock)
{
	struct super_block *superblock;

	pr_debug("Freezing device [%u:%u]\n", MAJOR(bdev->bd_dev),
		 MINOR(bdev->bd_dev));

	if (bdev->bd_super == NULL) {
		pr_warn("Unable to freeze device [%u:%u]: no superblock was found\n",
			MAJOR(bdev->bd_dev), MINOR(bdev->bd_dev));
		return 0;
	}

	superblock = freeze_bdev(bdev);
	if (IS_ERR_OR_NULL(superblock)) {
		int result;

		pr_err("Failed to freeze device [%u:%u]\n", MAJOR(bdev->bd_dev),
		       MINOR(bdev->bd_dev));

		if (superblock == NULL)
			result = -ENODEV;
		else {
			result = PTR_ERR(superblock);
			pr_err("Error code: %d\n", result);
		}
		return result;
	}

	pr_debug("Device [%u:%u] was frozen\n", MAJOR(bdev->bd_dev),
		 MINOR(bdev->bd_dev));
	*psuperblock = superblock;

	return 0;
}

static inline void _thaw_bdev(struct block_device *bdev,
			      struct super_block *superblock)
{
	if (superblock == NULL)
		return;

	if (thaw_bdev(bdev, superblock))
		pr_err("Failed to unfreeze device [%u:%u]\n",
		       MAJOR(bdev->bd_dev), MINOR(bdev->bd_dev));
	else
		pr_debug("Device [%u:%u] was unfrozen\n", MAJOR(bdev->bd_dev),
			 MINOR(bdev->bd_dev));
}
#endif

#ifndef BIO_MAX_VECS
static inline unsigned int bio_max_segs(unsigned int nr_segs)
{
	return min(nr_segs, 256U);
}
#endif

#if !defined(HAVE_DISK_LIVE)
struct bdev_inode {
	struct block_device bdev;
	struct inode vfs_inode;
};

static inline struct inode *BD_INODE(struct block_device *bdev)
{
	return &container_of(bdev, struct bdev_inode, bdev)->vfs_inode;
}
#endif

#ifndef HAVE_BDEV_NR_SECTORS
static inline sector_t bdev_nr_sectors(struct block_device *bdev)
{
	return i_size_read(BD_INODE(bdev)) >> 9;
};
#endif

#if defined(HAVE_BDEV_FILE_OPEN)
typedef struct file bdev_holder_t;
#elif defined(HAVE_BDEV_HANDLE)
typedef struct bdev_handle bdev_holder_t;
#else
typedef struct block_device bdev_holder_t;
#endif

#if defined(HAVE_BDEV_FILE_OPEN)
static inline int bdev_open(const char *bdevpath,
			    bdev_holder_t **bdev_holder,
			    struct block_device **bdev)
{
	struct file *bdev_file;

	bdev_file = bdev_file_open_by_path(bdevpath,
					   BLK_OPEN_READ | BLK_OPEN_WRITE,
					   NULL, NULL);
	if (IS_ERR(bdev_file))
		return PTR_ERR(bdev_file);

	*bdev_holder = bdev_file;
	*bdev = file_bdev(bdev_file);
	return 0;
}
static inline void bdev_close(bdev_holder_t *bdev_holder)
{
	bdev_fput(bdev_holder);
}
static inline int bdev_open_excl(const char *bdevpath, void *owner,
				 bdev_holder_t **bdev_holder,
				 struct block_device **bdev)
{
	struct file *file;

	file = bdev_file_open_by_path(bdevpath,
				BLK_OPEN_EXCL | BLK_OPEN_READ | BLK_OPEN_WRITE,
				owner, NULL);
	if (IS_ERR(file))
		return PTR_ERR(file);

	*bdev_holder = file;
	*bdev = file_bdev(file);
	return 0;
}
static inline void bdev_close_excl(bdev_holder_t *bdev_holder, void *owner)
{
	(void)owner;
	bdev_fput(bdev_holder);
}
static inline dev_t bdev_id_by_holder(bdev_holder_t *bdev_holder)
{
	return file_bdev(bdev_holder)->bd_dev;
}
static inline struct block_device *bdev_by_holder(bdev_holder_t *bdev_holder)
{
	return file_bdev(bdev_holder);
}
#elif defined(HAVE_BDEV_HANDLE)

static inline int bdev_open(const char *bdevpath,
			    bdev_holder_t **bdev_holder,
			    struct block_device **bdev)
{
	struct bdev_handle *handle;

	handle = bdev_open_by_path(bdevpath,
				   BLK_OPEN_READ | BLK_OPEN_WRITE,
				   NULL, NULL);
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	*bdev_holder = handle;
	*bdev = handle->bdev;
	return 0;
}
static inline void bdev_close(struct bdev_handle *bdev_holder)
{
	bdev_release(bdev_holder);
}
static inline int bdev_open_excl(const char *bdevpath, void *owner,
				 bdev_holder_t **bdev_holder,
				 struct block_device **bdev)
{
	struct bdev_handle *handle;

	handle = bdev_open_by_path(bdevpath,
				BLK_OPEN_EXCL | BLK_OPEN_READ | BLK_OPEN_WRITE,
				owner, NULL);
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	*bdev_holder = handle;
	*bdev = handle->bdev;
	return 0;
}
static inline void bdev_close_excl(bdev_holder_t *bdev_holder, void *owner)
{
	(void)owner;
	bdev_release(bdev_holder);
}
static inline dev_t bdev_id_by_holder(bdev_holder_t *bdev_holder)
{
	return bdev_holder->bdev->bd_dev;
}
static inline struct block_device *bdev_by_holder(bdev_holder_t *bdev_holder)
{
	return bdev_holder->bdev;
}
#else

static inline int bdev_open(const char *bdevpath,
			    bdev_holder_t **bdev_holder,
			    struct block_device **bdev)
{
	struct block_device *bd;

	bd = blkdev_get_by_path(bdevpath,
#if defined(HAVE_BLK_HOLDER_OPS)
				BLK_OPEN_READ | BLK_OPEN_WRITE, NULL, NULL
#else
				FMODE_READ | FMODE_WRITE, NULL
#endif
				);
	if (IS_ERR(bd))
		return PTR_ERR(bd);

	*bdev_holder = bd;
	*bdev = bd;
	return 0;
}

static inline void bdev_close(bdev_holder_t *bdev_holder)
{
#if defined(HAVE_BLK_HOLDER_OPS)
	blkdev_put(bdev_holder, NULL);
#else
	blkdev_put(bdev_holder, FMODE_READ | FMODE_WRITE);
#endif
}
static inline int bdev_open_excl(const char *bdevpath, void *owner,
				 bdev_holder_t **bdev_holder,
				 struct block_device **bdev)
{
	struct block_device *bd;

	bd = blkdev_get_by_path(bdevpath,
#if defined(HAVE_BLK_HOLDER_OPS)
				BLK_OPEN_EXCL | BLK_OPEN_READ | BLK_OPEN_WRITE, owner, NULL
#else
				FMODE_EXCL | FMODE_READ | FMODE_WRITE, owner
#endif
				);
	if (IS_ERR(bd))
		return PTR_ERR(bd);

	*bdev_holder = bd;
	*bdev = bd;
	return 0;
}
static inline void bdev_close_excl(bdev_holder_t *bdev_holder, void *owner)
{
#if defined(HAVE_BLK_HOLDER_OPS)
	blkdev_put(bdev_holder, owner);
#else
	(void)owner;
	blkdev_put(bdev_holder, FMODE_EXCL | FMODE_READ | FMODE_WRITE);
#endif
}
static inline dev_t bdev_id_by_holder(bdev_holder_t *bdev_holder)
{
	return bdev_holder->bd_dev;
}
static inline struct block_device *bdev_by_holder(bdev_holder_t *bdev_holder)
{
	return bdev_holder;
}

#endif

#endif /* __BLKSNAP_COMPAT_H */
