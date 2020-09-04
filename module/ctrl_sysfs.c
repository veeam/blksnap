#include "stdafx.h"
#include "ctrl_sysfs.h"
#include "blk-snap-ctl.h"


#define SECTION "ctrl_sysfs"
#include "log_format.h"

#include <linux/sysfs.h>

int get_veeamsnap_major(void);

static ssize_t major_show(struct class *class, struct class_attribute *attr, char *buf)
{
    sprintf(buf, "%d", get_veeamsnap_major());
    return strlen(buf);
}

CLASS_ATTR_RO(major);


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
        log_tr("Create 'major' sysfs attribute");
        res = class_create_file(veeamsnap_class, &class_attr_major);
        if (res != SUCCESS){
            log_err("Failed to create 'major' sysfs file");
            break;
        }

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

    if (veeamsnap_class != NULL) {
		class_remove_file(veeamsnap_class, &class_attr_major);

        class_destroy(veeamsnap_class);
        veeamsnap_class = NULL;
    }
}
