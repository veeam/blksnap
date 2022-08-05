// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#ifdef STANDALONE_BDEVFILTER
#include "blk_snap.h"
#else
#include <linux/blk_snap.h>
#endif
#include "version.h"
#include "memory_checker.h"
#include "log.h"

#ifdef BLK_SNAP_FILELOG

struct log_request_header {
	struct timespec64 time;
	pid_t pid;
	unsigned level;
	size_t size;
};

#define LOG_REQUEST_BUFFER_SIZE \
	(512 - sizeof(struct list_head) - sizeof(struct log_request_header))

struct log_request {
	struct list_head link;
	struct log_request_header header;
	char buffer[LOG_REQUEST_BUFFER_SIZE];
};

#define LOG_REQUEST_POOL_SIZE 128
struct log_request log_requests_pool[LOG_REQUEST_POOL_SIZE];
struct log_request log_request_direct;

static int log_level = -1;
static char *log_filepath = NULL;

static LIST_HEAD(log_free_requests);
static DEFINE_SPINLOCK(log_free_requests_lock);

static LIST_HEAD(log_active_requests);
static DEFINE_SPINLOCK(log_active_requests_lock);

static DECLARE_WAIT_QUEUE_HEAD(log_request_event_add);
static struct task_struct* log_task = NULL;
static atomic_t log_missed_counter = {0};

void log_init(void)
{
	size_t inx;

	INIT_LIST_HEAD(&log_free_requests);
	for (inx=0; inx < LOG_REQUEST_POOL_SIZE; inx++) {
		struct log_request *rq = &log_requests_pool[inx];

		INIT_LIST_HEAD(&rq->link);
		list_add_tail(&rq->link, &log_free_requests);
	}
}

void log_done(void)
{
	log_level = -1;

	if (log_task) {
		kthread_stop(log_task);
		log_task = NULL;
	}
	if (log_filepath) {
		kfree(log_filepath);
		memory_object_dec(memory_object_log_filepath);
		log_filepath = NULL;
	}
}

static inline struct log_request *log_request_new(void)
{
	struct log_request *rq;

	spin_lock(&log_free_requests_lock);
	rq = list_first_entry_or_null(&log_free_requests,
				      struct log_request, link);
	if (rq)
		list_del(&rq->link);
	else
		atomic_inc(&log_missed_counter);
	spin_unlock(&log_free_requests_lock);

	return rq;
}


static inline void log_request_free(struct log_request *rq)
{
	INIT_LIST_HEAD(&rq->link);

	spin_lock(&log_free_requests_lock);
	list_add_tail(&rq->link, &log_free_requests);
	spin_unlock(&log_free_requests_lock);
}

static inline void log_request_push(struct log_request *rq)
{
	INIT_LIST_HEAD(&rq->link);

	spin_lock(&log_active_requests_lock);
	list_add_tail(&rq->link, &log_active_requests);
	spin_unlock(&log_active_requests_lock);
}

static inline struct log_request *log_request_get(void)
{
	struct log_request *rq;

	spin_lock(&log_active_requests_lock);
	rq = list_first_entry_or_null(&log_active_requests,
				      struct log_request, link);
	if (rq)
		list_del(&rq->link);
	spin_unlock(&log_active_requests_lock);

	return rq;
}

static inline bool log_request_is_ready(void)
{
	bool ret;

	spin_lock(&log_active_requests_lock);
	ret = !list_empty(&log_active_requests);
	spin_unlock(&log_active_requests_lock);

	return ret;
}

#define MAX_PREFIX_SIZE 256
static const char *log_level_text[] = {"EMERG :","ALERT :","CRIT :","ERR :","WRN :","","",""};

static inline int log_request_write(struct file* filp, const struct log_request* rq)
{
	ssize_t ret;
	int size;
	struct tm time;
	char prefix_buf[MAX_PREFIX_SIZE];

	time64_to_tm(rq->header.time.tv_sec, 0, &time);

	size = snprintf(prefix_buf, MAX_PREFIX_SIZE,
		"[%02d.%02d.%04ld %02d:%02d:%02d-%06ld] <%d> | %s",
        	time.tm_mday, time.tm_mon + 1, time.tm_year + 1900,
        	time.tm_hour, time.tm_min, time.tm_sec,
        	rq->header.time.tv_nsec / 1000,
        	rq->header.pid, log_level_text[rq->header.level]
        );

	ret = kernel_write(filp, prefix_buf, size, &filp->f_pos);
	if (ret < 0)
		return (int)(ret);

	ret = kernel_write(filp, rq->buffer, rq->header.size, &filp->f_pos);
	if (ret < 0)
		return (int)(ret);

	return 0;
}

static inline bool log_waiting(void)
{
	int ret;

	ret = wait_event_interruptible_timeout(log_request_event_add,
		log_request_is_ready() || kthread_should_stop(), 10 * HZ);

	return (ret > 0);
}

static inline void log_request_fill(struct log_request *rq, const int level,
				    const char *fmt, va_list args)
{
	ktime_get_real_ts64(&rq->header.time);
	rq->header.pid = get_current()->pid;
	rq->header.level = level;
	rq->header.size = vscnprintf(rq->buffer, LOG_REQUEST_BUFFER_SIZE, fmt, args);
}

static int log_vprintk_direct(struct file* filp, const int level, const char *fmt, va_list args)
{
	log_request_fill(&log_request_direct, level, fmt, args);

	return log_request_write(filp, &log_request_direct);
}

static int log_printk_direct(struct file* filp, const int level, const char *fmt, ...)
{
	int ret;
	va_list args;

	va_start(args, fmt);
	ret = log_vprintk_direct(filp, level, fmt, args);
	va_end(args);

	return ret;
}

int log_processor(void *data)
{
	int ret = 0;
	struct log_request *rq;
	struct file* filp;

	filp = filp_open(log_filepath, O_WRONLY | O_APPEND | O_CREAT, 0644);
	if (IS_ERR(filp))
		return PTR_ERR(filp);

	ret = kernel_write(filp, "\n", 1, &filp->f_pos);
	if (ret < 0)
		return ret;

	log_printk_direct(filp, LOGLEVEL_INFO, "Start log for module %s version %s loglevel %d\n",
		BLK_SNAP_MODULE_NAME, VERSION_STR, log_level);

	while (!kthread_should_stop()) {
		int missed;

		missed = atomic_read(&log_missed_counter);
		if (missed) {
			log_printk_direct(filp, LOGLEVEL_INFO, "Missed %d messages\n", missed);
			atomic_sub(missed, &log_missed_counter);
		}

		rq = log_request_get();
		if (rq) {
			ret = log_request_write(filp, rq);
			log_request_free(rq);
			if (ret < 0)
				break;
		} else
			log_waiting();
	}

	while ((rq = log_request_get())) {
		if (!ret)
			ret = log_request_write(filp, rq);
		log_request_free(rq);
	}

	log_printk_direct(filp, LOGLEVEL_INFO, "Stop log for module %s\n\n",
		BLK_SNAP_MODULE_NAME);

	filp_close(filp, NULL);

	return ret;
}

int log_restart(int level, char *filepath)
{
	struct task_struct* task;

	if ((level < 0) || !filepath){
		/*
		 * Disable logging
		 */
		log_done();
		return 0;
	}


	if (log_filepath && (strcmp(filepath, log_filepath) == 0)) {
		if (level == log_level)
			/*
			 * If the request is executed for the same parameters
			 * that are already set for logging, then logging is
			 * not restarted and an error code EALREADY is returned.
			 */
			return -EALREADY;
		else if (level >= 0) {
			/*
			 * If only the logging level changes, then
			 * there is no need to restart logging.
			 */
			log_level = level;
			return 0;
		}
	}

	log_done();
	log_init();

	task = kthread_create(log_processor, filepath, "blksnaplog");
	if (IS_ERR(task))
		return PTR_ERR(task);

	log_task = task;
	log_filepath = filepath;
	log_level = level <= LOGLEVEL_DEBUG ? level : LOGLEVEL_DEBUG;

	wake_up_process(log_task);

        return 0;
}

static void log_vprintk(const int level, const char *fmt, va_list args)
{
	struct log_request *rq;

	rq = log_request_new();
	if (!rq)
		return;

	log_request_fill(rq, level, fmt, args);
	log_request_push(rq);
	wake_up(&log_request_event_add);
}

void log_printk(const int level, const char *fmt, ...)
{
	if (level <= log_level) {
		va_list args;

		va_start(args, fmt);
		log_vprintk(level, fmt, args);
		va_end(args);
	}
}

#endif /* BLK_SNAP_FILELOG */
