#ifndef STDAFX_H_
#define STDAFX_H_

#include <linux/init.h>
#include <linux/module.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,18,0)
#define VEEAMSNAP_MQ_IO
#endif

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

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0)
#define HAVE_MAKE_REQUEST_INT
#endif

#ifndef pr_warn
#define pr_warn pr_warning
#endif

#include "log.h"

#define VEEAMSNAP_MEMORY_LEAK_CONTROL
#include "mem_alloc.h"

#define DESCRIPTION_STR "Veeam Snapshot Kernel Module"

#define MODULE_NAME "veeamsnap"
#define VEEAM_SNAP_IMAGE "veeamimage"

#ifndef SECTOR_SHIFT
#define SECTOR_SHIFT 9
#endif
#ifndef SECTOR_SIZE
#define SECTOR_SIZE (1 << SECTOR_SHIFT)
#endif

#define SECTORS_IN_PAGE (PAGE_SIZE / SECTOR_SIZE)

#define SUCCESS 0

#define SNAPSHOTDATA_MEMORY_SIZE ( 128 << 20 )

typedef unsigned char    byte_t;
typedef unsigned long long stream_size_t;

#define SNAPDATA_SPARSE_CHANGES // use sparse bitmap for snapdata collection
#define SPARSE_BLOCK_CACHEABLE  // use cache for sparse block arrays

#define DEFER_IO_COPY_REQUEST_LENGTH 10
#define DEFER_IO_DIO_REQUEST_LENGTH 250
#define DEFER_IO_DIO_REQUEST_SECTORS_COUNT (10*1024*1024/SECTOR_SIZE)

//#define VEEAMIMAGE_THROTTLE_TIMEOUT ( 30*HZ )    //delay 30 sec
//#define VEEAMIMAGE_THROTTLE_TIMEOUT ( 3*HZ )    //delay 3 sec
#define VEEAMIMAGE_THROTTLE_TIMEOUT ( 1*HZ )    //delay 1 sec
//#define VEEAMIMAGE_THROTTLE_TIMEOUT ( HZ/1000 * 10 )    //delay 10 ms

int get_debuglogging( void );
#define VEEAM_LL_DEFAULT   0    /* default as normal*/
#define VEEAM_LL_LO           2    /* minimal logging */
#define VEEAM_LL_NORM       4    /* normal */
#define VEEAM_LL_HI         7    /* debug logging */

#define VEEAM_ZEROSNAPDATA_OFF 0
#define VEEAM_ZEROSNAPDATA_ON  1
int get_zerosnapdata( void );
int get_snapstore_block_size_pow(void);
int inc_snapstore_block_size_pow(void);
int get_change_tracking_block_size_pow(void);

#define FIXFLAG_RH6_SPINLOCK 1    //https://www.veeam.com/kb2786 
unsigned int get_fixflags(void);

#define SNAPDATA_ZEROED

#define CBT_BLOCK_SIZE_DEGREE get_change_tracking_block_size_pow()
#define CBT_BLOCK_SIZE (1<<CBT_BLOCK_SIZE_DEGREE)

#define COW_BLOCK_SIZE_DEGREE get_snapstore_block_size_pow()
#define COW_BLOCK_SIZE (1<<COW_BLOCK_SIZE_DEGREE)

#define SNAPSTORE_BLK_SHIFT (sector_t)(COW_BLOCK_SIZE_DEGREE - SECTOR_SHIFT)
#define SNAPSTORE_BLK_SIZE  (sector_t)(1 << SNAPSTORE_BLK_SHIFT)
#define SNAPSTORE_BLK_MASK  (sector_t)(SNAPSTORE_BLK_SIZE-1)

//#define VEEAM_IOCTL_LOGGING

#define SNAPSTORE_MULTIDEV

#if defined(DISTRIB_NAME_OPENSUSE_LEAP) || defined(DISTRIB_NAME_OPENSUSE) || defined(DISTRIB_NAME_SLES) || defined(DISTRIB_NAME_SLES_SAP)
#define OS_RELEASE_SUSE
#endif

#define PERSISTENT_CBT

//#define DEBUG_CBT_LOAD 0x5A5A3CE1 // only for debugging

////Persistent CBT is not supported for SLES 11
// #if defined(DISTRIB_NAME_SLES) && defined(DISTRIB_VERSION_1)
// #if DISTRIB_VERSION_1 == 11
// #undef PERSISTENT_CBT
// #endif
// #endif

//#define VEEAMSNAP_SYSFS_PARAMS

//#define SNAPIMAGE_TRACER

#endif /* STDAFX_H_ */
