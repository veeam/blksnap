/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __BLK_SNAP_LOG_H
#define __BLK_SNAP_LOG_H

#ifdef BLK_SNAP_FILELOG

void log_init(void);
void log_done(void);
int log_restart(int level, char *filepath, int tz_minuteswest);
void log_printk(const int level, const char *fmt, ...);


#undef pr_emerg
#define pr_emerg(fmt, ...) \
({ \
	log_printk(LOGLEVEL_EMERG, fmt, ##__VA_ARGS__); \
	printk(KERN_EMERG pr_fmt(fmt), ##__VA_ARGS__) \
})

#undef pr_alert
#define pr_alert(fmt, ...) \
({ \
	log_printk(LOGLEVEL_ALERT, fmt, ##__VA_ARGS__); \
	printk(KERN_ALERT pr_fmt(fmt), ##__VA_ARGS__) \
})

#undef pr_crit
#define pr_crit(fmt, ...) \
({ \
	log_printk(LOGLEVEL_CRIT, fmt, ##__VA_ARGS__); \
	printk(KERN_CRIT pr_fmt(fmt), ##__VA_ARGS__); \
})

#undef pr_err
#define pr_err(fmt, ...) \
({ \
	log_printk(LOGLEVEL_ERR, fmt, ##__VA_ARGS__); \
	printk(KERN_ERR pr_fmt(fmt), ##__VA_ARGS__); \
})

#undef pr_warn
#define pr_warn(fmt, ...) \
({ \
	log_printk(LOGLEVEL_WARNING, fmt, ##__VA_ARGS__); \
	printk(KERN_WARNING pr_fmt(fmt), ##__VA_ARGS__); \
})

#undef pr_notice
#define pr_notice(fmt, ...) \
({ \
	log_printk(LOGLEVEL_NOTICE, fmt, ##__VA_ARGS__); \
	printk(KERN_NOTICE pr_fmt(fmt), ##__VA_ARGS__) \
})
#undef pr_info
#define pr_info(fmt, ...) \
({ \
	log_printk(LOGLEVEL_INFO, fmt, ##__VA_ARGS__); \
	printk(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__); \
})

#undef pr_debug
#if defined(BLK_SNAP_DEBUGLOG) || defined(DEBUG)
#define pr_debug(fmt, ...) \
({ \
	log_printk(LOGLEVEL_DEBUG, fmt, ##__VA_ARGS__); \
	printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__); \
})
#else
#define pr_debug(fmt, ...) \
({ \
	log_printk(LOGLEVEL_DEBUG, fmt, ##__VA_ARGS__); \
	no_printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__); \
})
#endif

#elif defined(BLK_SNAP_DEBUGLOG)

#undef pr_debug
#if defined(BLK_SNAP_DEBUGLOG) || defined(DEBUG)
#define pr_debug(fmt, ...) \
	printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#else
#define pr_debug(fmt, ...) \
	no_printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#endif

#endif

#endif /* __BLK_SNAP_LOG_H */
