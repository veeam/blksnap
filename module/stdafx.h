#ifndef STDAFX_H_
#define STDAFX_H_

#include <linux/init.h>
#include <linux/module.h>
#include <linux/version.h>

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/genhd.h> // For basic block driver framework
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/hdreg.h> // For struct hd_geometry
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/rwsem.h>
#include <linux/wait.h>
#include <linux/bitmap.h>
#include <asm/atomic.h>
#include <linux/random.h>
#include "log.h"

#ifndef SUCCESS
#define SUCCESS 0
#endif







#endif /* STDAFX_H_ */
