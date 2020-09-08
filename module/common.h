#ifndef STDAFX_H_
#define STDAFX_H_

#include <linux/version.h> /*rudiment - needed for using KERNEL_VERSION */

#include <linux/types.h>
#include <linux/errno.h>

#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <asm/atomic.h>

#include "log.h"

#ifndef SUCCESS
#define SUCCESS 0
#endif







#endif /* STDAFX_H_ */
