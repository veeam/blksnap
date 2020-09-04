#include "stdafx.h"
#include "ctrl_sysfs.h"
#include "blk-snap-ctl.h"

#ifdef PERSISTENT_CBT
#include "cbt_persistent.h"
#endif

#define SECTION "ctrl_sysfs"
#include "log_format.h"

#include <linux/sysfs.h>

#ifdef PERSISTENT_CBT
uint64_t _notify_counter = 0;

// major

int get_veeamsnap_major(void);

static ssize_t major_show(struct class *class, struct class_attribute *attr, char *buf)
{
    sprintf(buf, "%d", get_veeamsnap_major());
    return strlen(buf);
}

// blkdev_notify
static ssize_t blkdev_notify_show(struct class *class, struct class_attribute *attr, char *buf)
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

static ssize_t blkdev_notify_store(struct class *class, struct class_attribute *attr, const char *buf, size_t count) 
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
                    dev_name = kzalloc(5+len+1, GFP_KERNEL);
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
                    dev_path = kzalloc(len+1, GFP_KERNEL);
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
        kfree(dev_name);
    if (dev_path != NULL)
        kfree(dev_path);

    return count;
}
#endif //PERSISTENT_CBT

#ifdef PERSISTENT_CBT
CLASS_ATTR_RW(blkdev_notify);
CLASS_ATTR_RO(major);
#endif

static struct class *veeamsnap_class = NULL;

int ctrl_sysfs_init(struct device **p_device)
{
    int res;
    veeamsnap_class = class_create(THIS_MODULE, MODULE_NAME);
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

        {
            struct device *dev = device_create(veeamsnap_class, NULL, MKDEV(get_veeamsnap_major(), 0), NULL, MODULE_NAME);
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

#ifdef PERSISTENT_CBT
        class_remove_file(veeamsnap_class, &class_attr_blkdev_notify);
		class_remove_file(veeamsnap_class, &class_attr_major);
#endif
        class_destroy(veeamsnap_class);
        veeamsnap_class = NULL;
    }
}

