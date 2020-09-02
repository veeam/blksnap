#include "stdafx.h"
#include "ctrl_sysfs.h"

#ifdef PERSISTENT_CBT
#include "cbt_persistent.h"
#endif

#define SECTION "ctrl_sysfs"
#include "log_format.h"

#include <linux/sysfs.h>

#ifdef VEEAMSNAP_SYSFS_PARAMS
int set_params(char* param_name, char* param_value);
int get_params(char* buf);
#endif

#ifdef PERSISTENT_CBT
uint64_t _notify_counter = 0;

// major

int get_veeamsnap_major(void);
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,32)
static ssize_t major_show(struct class *class, struct class_attribute *attr, char *buf)
#else
static ssize_t major_show(struct class *class, char *buf)
#endif
{
    sprintf(buf, "%d", get_veeamsnap_major());
    return strlen(buf);
}

// blkdev_notify

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,32)
static ssize_t blkdev_notify_show(struct class *class, struct class_attribute *attr, char *buf)
#else
static ssize_t blkdev_notify_show(struct class *class, char *buf)
#endif
{
    log_tr_lld("Show notify counter ", _notify_counter);
    sprintf(buf, "%lld", _notify_counter);
    return strlen(buf);
}

enum ENotifyCommandType
{
    NotifyCommandInvalid = 0,
    NotifyCommandAdd = 1,
    NotifyCommandRemove = 2
};

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,32)
static ssize_t blkdev_notify_store(struct class *class, struct class_attribute *attr, const char *buf, size_t count) 
#else
static ssize_t blkdev_notify_store(struct class *class, const char *buf, size_t count) 
#endif
{
    int res = SUCCESS;
    /*
    string format: "add sda /devices/.../sda"
    */
    size_t state = 0;
    size_t ofs = 0;
    size_t len = 0;

    enum ENotifyCommandType type = NotifyCommandInvalid;
    char* dev_name = NULL;
    char* dev_path = NULL;

    //log_tr_s("DEBUG! buffer: ", buf);

    ++_notify_counter;

    // to do string parsing
    while ((ofs+len) < count)
    {
        if ( (buf[ofs + len] == ' ') || (buf[ofs + len] == '\n') || (buf[ofs + len] == '\t') || ((ofs + len) == (count-1)) )
        {
            if (len > 1)
            {
                switch (state){
                case 0://add or remove
                    if ((len == 3) && (0 == memcmp("add", buf + ofs, 3))){
                        type = NotifyCommandAdd;
                        //log_tr("DEBUG! Found add command");
                    }
                    else if ((len == 6) && (0 == memcmp("remove", buf + ofs, 6))){
                        type = NotifyCommandRemove;
                        //log_tr("DEBUG! Found remove command");
                    }else{
                        log_tr("Invalid command found");
                        type = NotifyCommandInvalid;
                    }
                    break;
                case 1://device name
                    dev_name = dbg_kzalloc(5+len+1, GFP_KERNEL);
                    if (dev_name == NULL){
                        res = -ENOMEM;
                        log_err("Failed allocate memory for device name");
                    }
                    memcpy(dev_name, "/dev/", 5);
                    memcpy(dev_name+5, buf + ofs, len);
                    dev_name[5+len] = '\0';
                    //log_tr_s("DEBUG! Found device name", dev_name);
                    break;
                case 2://device path
                    dev_path = dbg_kzalloc(len+1, GFP_KERNEL);
                    if (dev_path == NULL){
                        res = -ENOMEM;
                        log_err("Failed allocate memory for device path");
                    }
                    memcpy(dev_path, buf + ofs, len);
                    dev_path[len] = '\0';
                    //log_tr_s("DEBUG! Found device path", dev_path);
                    break;
                default:
                    log_err_s("Failed to parse text: ", buf + ofs);
                }
                ++state;
            }
            ofs += len+1;
            len = 0;
        }
        ++len;
    }

    //call block device notifier
    if ((res == SUCCESS) && (dev_name != NULL) && (dev_path != NULL) && (type != NotifyCommandInvalid)){
        if (type == NotifyCommandAdd)
            cbt_persistent_device_attach(dev_name, dev_path);
        if (type == NotifyCommandRemove)
            cbt_persistent_device_detach(dev_name, dev_path);
    }
    else{
        log_err_s("Failed to parse notification ", buf);
    }

    if (dev_name != NULL)
        dbg_kfree(dev_name);
    if (dev_path != NULL)
        dbg_kfree(dev_path);

    return count;
}
#endif //PERSISTENT_CBT

#ifdef VEEAMSNAP_SYSFS_PARAMS
// params
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,32)
static ssize_t params_show(struct class *class, struct class_attribute *attr, char *buf)
#else
static ssize_t params_show(struct class *class, char *buf)
#endif
{
    int res = get_params(buf);
    if (res == SUCCESS)
        return strlen(buf);

    log_err("Failed to read parameters");
    return 0;
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,32)
static ssize_t params_store(struct class *class, struct class_attribute *attr, const char *buf, size_t count)
#else
static ssize_t params_store(struct class *class, const char *buf, size_t count)
#endif
{
    int res = -EINVAL;
    size_t ofs = 0;

    char* param_name = NULL;
    char* param_value = NULL;

    //log_tr_s("DEBUG! buffer: ", buf);

    // to do string parsing
    while (ofs < count)
    {
        if (buf[ofs] == '='){ //separator found
            size_t len = ofs;
            param_name = dbg_kzalloc(len + 1, GFP_KERNEL);
            if (param_name == NULL){
                res = -ENOMEM;
                break;
            }
            memcpy(param_name, buf, len);
            param_name[len] = '\0';

            ofs += 1;//skip separator
            len = count - ofs;
            param_value = dbg_kzalloc(len + 1, GFP_KERNEL);
            if (param_value == NULL){
                res = -ENOMEM;
                break;
            }
            memcpy(param_value, buf+ofs, len);
            param_value[len] = '\0';

            res = SUCCESS;
            break;
        }

        ++ofs;
    }

    //call block device notifier
    res = set_params(param_name, param_value);
    if (SUCCESS != res)
        log_err_s("Failed to set parameter ", param_name);

    if (param_name != NULL)
        dbg_kfree(param_name);
    if (param_value != NULL)
        dbg_kfree(param_value);


    return count;
}
#endif

#ifndef __ATTR_RW
#define __ATTR_RW(_name) __ATTR(_name, 0644, _name##_show, _name##_store)
#endif

#ifndef __ATTR_RO
#define __ATTR_RO(_name) {						\
	.attr	= { .name = __stringify(_name), .mode = 0444 },		\
	.show	= _name##_show,						\
}
#endif

#ifndef CLASS_ATTR_RW
#define CLASS_ATTR_RW(_name) \
struct class_attribute class_attr_##_name = __ATTR_RW(_name)
#endif
#ifndef CLASS_ATTR_RO
#define CLASS_ATTR_RO(_name) \
struct class_attribute class_attr_##_name = __ATTR_RO(_name)
#endif

#ifdef PERSISTENT_CBT
CLASS_ATTR_RW(blkdev_notify);
CLASS_ATTR_RO(major);
#endif

#ifdef VEEAMSNAP_SYSFS_PARAMS
CLASS_ATTR_RW(params);
#endif

static struct class *veeamsnap_class = NULL;

int ctrl_sysfs_init(struct device **p_device)
{
    int res;
    veeamsnap_class = class_create(THIS_MODULE, "veeamsnap");
    if (IS_ERR(veeamsnap_class)){
        res = PTR_ERR(veeamsnap_class);
        log_err_d("bad class create. Error code ", 0-res);
        return res;
    }

    do{
#ifdef PERSISTENT_CBT
        log_tr("Create 'major' sysfs attribute");
        res = class_create_file(veeamsnap_class, &class_attr_major);
        if (res != SUCCESS){
            log_err("Failed to create 'major' sysfs file");
            break;
        }

        log_tr("Create 'blkdev_notify' sysfs attribute");
        res = class_create_file(veeamsnap_class, &class_attr_blkdev_notify);
        if (res != SUCCESS){
            log_err("Failed to create 'blkdev_notify' sysfs file");
            break;
        }
#endif
#ifdef VEEAMSNAP_SYSFS_PARAMS
        log_tr("Create 'params' sysfs attribute");
        res = class_create_file(veeamsnap_class, &class_attr_params);
        if (res != SUCCESS){
            log_err("Failed to create 'params' sysfs file");
            break;
        }
#endif
        {
            struct device *dev = device_create(veeamsnap_class, NULL, MKDEV(get_veeamsnap_major(), 0), NULL, "veeamsnap");
            if (IS_ERR(dev)){
                res = PTR_ERR(dev);
                log_err_d("Failed to create device, result=", res);
                break;
            }

            *p_device = dev;
        }
    } while (false);

    if (res != SUCCESS){
        class_destroy(veeamsnap_class);
        veeamsnap_class = NULL;
    }
    return res;
}

void ctrl_sysfs_done(struct device **p_device)
{
    if (*p_device) {
        device_unregister(*p_device);
        *p_device = NULL;
    }

    if (veeamsnap_class != NULL){
#ifdef VEEAMSNAP_SYSFS_PARAMS
		class_remove_file(veeamsnap_class, &class_attr_params);
#endif
#ifdef PERSISTENT_CBT
        class_remove_file(veeamsnap_class, &class_attr_blkdev_notify);
		class_remove_file(veeamsnap_class, &class_attr_major);
#endif
        class_destroy(veeamsnap_class);
        veeamsnap_class = NULL;
    }
}

