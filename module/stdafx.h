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


#define SECTORS_IN_PAGE (PAGE_SIZE / SECTOR_SIZE)

#ifndef SUCCESS
#define SUCCESS 0
#endif

#define SNAPIMAGE_MAX_DEVICES 2048

#define DEFER_IO_DIO_REQUEST_LENGTH 250
#define DEFER_IO_DIO_REQUEST_SECTORS_COUNT (10*1024*1024/SECTOR_SIZE)

//#define VEEAMIMAGE_THROTTLE_TIMEOUT ( 30*HZ )    //delay 30 sec
//#define VEEAMIMAGE_THROTTLE_TIMEOUT ( 3*HZ )    //delay 3 sec
#define VEEAMIMAGE_THROTTLE_TIMEOUT ( 1*HZ )    //delay 1 sec
//#define VEEAMIMAGE_THROTTLE_TIMEOUT ( HZ/1000 * 10 )    //delay 10 ms

#endif /* STDAFX_H_ */
