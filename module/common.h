#ifndef COMMON_H_
#define COMMON_H_

#define MODSECTION ""

#define pr_fmt(fmt) KBUILD_MODNAME MODSECTION ": " fmt

#include <linux/version.h> /*rudiment - needed for using KERNEL_VERSION */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <asm/atomic.h>

#ifndef SUCCESS
#define SUCCESS 0
#endif

#endif /* COMMON_H_ */
