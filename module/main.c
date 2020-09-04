#include "stdafx.h"
#include "version.h"
#include "blk-snap-ctl.h"
#include "ctrl_fops.h"
#include "ctrl_pipe.h"

#include "blk_direct.h"
#include "blk_redirect.h"
#include "blk_deferred.h"
#include "snapimage.h"
#include "snapdata_collect.h"

#include "snapstore.h"
#include "snapstore_device.h"

#include "snapshot.h"
#include "tracker_queue.h"
#include "tracker.h"
#include "tracking.h"
#include "sparse_bitmap.h"
#include "ctrl_sysfs.h"

#define SECTION "main      "
#include "log_format.h"

#include <linux/notifier.h>
#include <linux/blk-filter.h>
#define VEEAMSNAP_DEFAULT_ALTITUDE BLK_FILTER_ALTITUDE_MIN

#include <linux/reboot.h>       //use old methon
//#include <linux/syscore_ops.h>    //more modern method


static int g_param_zerosnapdata = 0;        /*rudiment */
static int g_param_debuglogging = 0;        /*rudiment */
static unsigned int g_param_fixflags = 0;   /*rudiment */

static char* g_logdir = NULL; 
static unsigned long g_param_logmaxsize = 15*1024*1024;

static int g_param_snapstore_block_size_pow = 14;
static int g_param_change_tracking_block_size_pow = 18;


int get_debuglogging( void )
{
    return g_param_debuglogging;
}


int get_snapstore_block_size_pow(void)
{
    return g_param_snapstore_block_size_pow;
}
int inc_snapstore_block_size_pow(void)
{
    if (g_param_snapstore_block_size_pow > 30)
        return -EFAULT;
    
    ++g_param_snapstore_block_size_pow;
    return SUCCESS;
}
int get_change_tracking_block_size_pow(void)
{
    return g_param_change_tracking_block_size_pow;
}

static int veeamsnap_major = 0;

int get_veeamsnap_major(void)
{
    return veeamsnap_major;
}


blk_qc_t filter_submit_original_bio(struct bio *bio);

static void filter_disk_add(struct gendisk *disk)
{
    log_tr_format("new disk [%s] in system", disk->disk_name);
}
static void filter_disk_del(struct gendisk *disk)
{
    log_tr_format("del disk [%s] from system", disk->disk_name);
}
static void filter_disk_release(struct gendisk *disk)
{
    log_tr_format("release disk [%s] from system", disk->disk_name);
}
static blk_qc_t filter_submit_bio(struct bio *bio)
{
    blk_qc_t result;
    if (tracking_submit_bio(bio, &result))
        return result;
    else
        return filter_submit_original_bio(bio);
}

static const struct blk_filter_ops g_filter_ops = {
    .disk_add = filter_disk_add,
    .disk_del = filter_disk_del,
    .disk_release = filter_disk_release,
    .submit_bio = filter_submit_bio
};

static struct blk_filter g_filter = {
    .name = MODULE_NAME,
    .ops = &g_filter_ops,
    .altitude = VEEAMSNAP_DEFAULT_ALTITUDE,
    .blk_filter_ctx = NULL
};

blk_qc_t filter_submit_original_bio(struct bio *bio)
{
    return blk_filter_submit_bio_next(&g_filter, bio);
}

static struct device* veeamsnap_device = NULL;

static struct file_operations ctrl_fops = {
    .owner  = THIS_MODULE,
    .read   = ctrl_read,
    .write  = ctrl_write,
    .open   = ctrl_open,
    .release= ctrl_release,
    .poll   = ctrl_poll,
    .unlocked_ioctl = ctrl_unlocked_ioctl
};

static inline void show_distrib(const char* distrib_name)
{
    log_tr_format("Compile for distributive: %s", distrib_name);
}

static inline void show_distrib_version(const char* distrib_name)
{
#if defined(DISTRIB_VERSION_1) && defined(DISTRIB_VERSION_2)
    log_tr_format("Compile for distributive: %s %d.%d", distrib_name, DISTRIB_VERSION_1, DISTRIB_VERSION_2);
#else
#if defined(DISTRIB_VERSION_1)
    log_tr_format("Compile for distributive: %s %d", distrib_name, DISTRIB_VERSION_1);
#else
    show_distrib(distrib_name);
#endif
#endif
}


static void _cbt_syscore_shutdown(void)
{
    //stop logging thread. In this time it is not needed
    logging_done();

    {//stop tracking
        int result = tracker_remove_all();
        if (result != SUCCESS)
            log_err("Failed to remove all tracking devices from tracking");
    }
}

#ifdef _LINUX_SYSCORE_OPS_H


struct syscore_ops _cbt_syscore_ops = {
    .suspend = NULL/*_cbt_syscore_suspend*/,
    .resume = NULL/*_cbt_syscore_resume*/,
    .shutdown = _cbt_syscore_shutdown,
};
#endif

#ifdef _LINUX_REBOOT_H
static int veeamsnap_shutdown_notify(struct notifier_block *nb, unsigned long type, void *p)
{
    logging_mode_sys(); //switch logger to system log

    switch (type) {
    case SYS_HALT:
        log_tr("system halt");
        break;
    case SYS_POWER_OFF:
        log_tr("system power off");
        break;
    case SYS_DOWN:
    default:
        log_tr("system down");
        break;
    }
    logging_flush();

    _cbt_syscore_shutdown();

    return NOTIFY_DONE;
}

static struct notifier_block _veeamsnap_shutdown_nb = {
    .notifier_call = veeamsnap_shutdown_notify,
};
#endif


int __init veeamsnap_init(void)
{
    //int conteiner_cnt = 0;
    int result = SUCCESS;

    logging_init( g_logdir, g_param_logmaxsize );
    log_tr( "================================================================================" );
    log_tr( "Loading" );
    log_tr_s( "Version: ", FILEVER_STR );

    log_tr_d("snapstore_block_size_pow: ", g_param_snapstore_block_size_pow);
    log_tr_d("change_tracking_block_size_pow: ", g_param_change_tracking_block_size_pow);
    log_tr_s("logdir: ", g_logdir);
    log_tr_ld("logmaxsize: ", g_param_logmaxsize);

    if (g_param_snapstore_block_size_pow > 23){
        g_param_snapstore_block_size_pow = 23;
        log_tr_d("Limited snapstore_block_size_pow: ", g_param_snapstore_block_size_pow);
    }
    else if (g_param_snapstore_block_size_pow < 12){
        g_param_snapstore_block_size_pow = 12;
        log_tr_d("Limited snapstore_block_size_pow: ", g_param_snapstore_block_size_pow);
    }

    if (g_param_change_tracking_block_size_pow > 23){
        g_param_change_tracking_block_size_pow = 23;
        log_tr_d("Limited change_tracking_block_size_pow: ", g_param_change_tracking_block_size_pow);
    }
    else if (g_param_change_tracking_block_size_pow < 12){
        g_param_change_tracking_block_size_pow = 12;
        log_tr_d("Limited change_tracking_block_size_pow: ", g_param_change_tracking_block_size_pow);
    }


    page_arrays_init( );

    ctrl_init();

    do{
        log_tr("Registering reboot notification");

#ifdef _LINUX_REBOOT_H
        result = register_reboot_notifier(&_veeamsnap_shutdown_nb); //always return SUCCESS
        if (result != SUCCESS){
            log_err_d("Failed to register reboot notification. Error code ", result);
            break;
        }
#endif
#ifdef _LINUX_SYSCORE_OPS_H
        register_syscore_ops(&_cbt_syscore_ops);
#endif


        veeamsnap_major = register_chrdev(0, MODULE_NAME, &ctrl_fops);
        if (veeamsnap_major < 0) {
            log_err_d("Failed to register a character device. errno=", veeamsnap_major);
            result = veeamsnap_major;
            break;
        }
        log_tr_format("Module major [%d]", veeamsnap_major);

        if ((result = blk_direct_bioset_create( )) != SUCCESS)
            break;
        if ((result = blk_redirect_bioset_create( )) != SUCCESS)
            break;

        blk_deferred_init( );

        if ((result = blk_deferred_bioset_create( )) != SUCCESS)
            break;

        if ((result = sparsebitmap_init( )) != SUCCESS)
            break;

        if ((result = tracker_init( )) != SUCCESS)
            break;

        if ((result = tracker_queue_init( )) != SUCCESS)
            break;

        if ((result = snapshot_Init( )) != SUCCESS)
            break;

        if ((result = snapstore_device_init( )) != SUCCESS)
            break;
        if ((result = snapstore_init( )) != SUCCESS)
            break;

        if ((result = snapdata_collect_Init( )) != SUCCESS)
            break;

        if ((result = snapimage_init( )) != SUCCESS)
            break;

        if ((result = ctrl_sysfs_init(&veeamsnap_device)) != SUCCESS){
			log_err("Failed to initialize sysfs attributes");
            break;
        }

        if ((result = blk_filter_register(&g_filter)) != SUCCESS) {
            const char* exist_filter = blk_filter_check_altitude(g_filter.altitude);
            if (exist_filter)
                log_err_format("Block io layer filter [%s] already exist on altitude [%d]", exist_filter, g_filter.altitude);

            log_err("Failed to register block io layer filter");
            break;
        }

    }while(false);

    return result;
}

void __exit veeamsnap_exit(void)
{
    int conteiner_cnt = 0;
    int result;
    log_tr("Unloading module");


    log_tr("Unregistering reboot notification");
#ifdef _LINUX_SYSCORE_OPS_H
    unregister_syscore_ops(&_cbt_syscore_ops);
#endif

#ifdef _LINUX_REBOOT_H
    unregister_reboot_notifier(&_veeamsnap_shutdown_nb);
#endif

    ctrl_sysfs_done(&veeamsnap_device);

    result = snapshot_Done( );
    if (SUCCESS == result){

        snapdata_collect_Done( );

        snapstore_device_done( );
        snapstore_done( );

        result = tracker_done( );
        if (SUCCESS == result){
            result = tracker_queue_done( );
            if (SUCCESS == result)
                result = blk_filter_unregister(&g_filter);
        }

        snapimage_done( );

        sparsebitmap_done( );

        blk_deferred_bioset_free( );
        blk_deferred_done( );

        blk_redirect_bioset_free( );
        blk_direct_bioset_free( );
    }

    if (SUCCESS != result){
        log_tr_d( "Failed to unload. errno=", result );
        return;
    }

    unregister_chrdev(veeamsnap_major, MODULE_NAME);

    ctrl_done( );

    logging_done( );

    conteiner_cnt = container_alloc_counter( );
    if (conteiner_cnt != 0)
        log_err_d( "container_alloc_counter=", conteiner_cnt );

    conteiner_cnt = container_sl_alloc_counter( );
    if (conteiner_cnt != 0)
        log_err_d( "container_sl_alloc_counter=", conteiner_cnt );
}

module_init(veeamsnap_init);
module_exit(veeamsnap_exit);


module_param_named( zerosnapdata, g_param_zerosnapdata, int, 0644 );
MODULE_PARM_DESC( zerosnapdata, "Zeroing snapshot data algorithm determine." );

module_param_named( debuglogging, g_param_debuglogging, int, 0644 );
MODULE_PARM_DESC( debuglogging, "Logging level switch." );

module_param_named(logdir, g_logdir, charp, 0644);
MODULE_PARM_DESC( logdir, "Directory for module logs." );

module_param_named( logmaxsize, g_param_logmaxsize, ulong, 0644 );
MODULE_PARM_DESC( logmaxsize, "Maximum log file size." );

module_param_named(snapstore_block_size_pow, g_param_snapstore_block_size_pow, int, 0644);
MODULE_PARM_DESC(snapstore_block_size_pow, "Snapstore block size binary pow. 20 for 1MiB block size");

module_param_named(change_tracking_block_size_pow, g_param_change_tracking_block_size_pow, int, 0644);
MODULE_PARM_DESC(change_tracking_block_size_pow, "Change-tracking block size binary pow. 18 for 256 KiB block size");

module_param_named(fixflags, g_param_fixflags, uint, 0644);
MODULE_PARM_DESC(fixflags, "Flags for known issues");

MODULE_DESCRIPTION("Veeam Snapshot Kernel Module");
MODULE_VERSION(FILEVER_STR);
MODULE_AUTHOR("Veeam Software Group GmbH");
MODULE_LICENSE("GPL");
