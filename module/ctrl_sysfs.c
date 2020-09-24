#include "common.h"
#ifdef MODSECTION
#undef MODSECTION
#define MODSECTION "-ctrl"
#endif

#include "ctrl_sysfs.h"
#include "blk-snap-ctl.h"

#include <linux/blkdev.h>
#include <linux/sysfs.h>

int get_blk_snap_major(void);

static ssize_t major_show(struct class *class, struct class_attribute *attr, char *buf)
{
	sprintf(buf, "%d", get_blk_snap_major());
	return strlen(buf);
}

CLASS_ATTR_RO(major);

static struct class *blk_snap_class = NULL;

int ctrl_sysfs_init(struct device **p_device)
{
	int res;
	blk_snap_class = class_create(THIS_MODULE, MODULE_NAME);
	if (IS_ERR(blk_snap_class)) {
		res = PTR_ERR(blk_snap_class);
		pr_err("Bad class create. errno=%d\n", 0 - res);
		return res;
	}

	do {
		pr_info("Create 'major' sysfs attribute\n");
		res = class_create_file(blk_snap_class, &class_attr_major);
		if (res != SUCCESS) {
			pr_err("Failed to create 'major' sysfs file\n");
			break;
		}

		{
			struct device *dev =
				device_create(blk_snap_class, NULL,
					      MKDEV(get_blk_snap_major(), 0), NULL, MODULE_NAME);
			if (IS_ERR(dev)) {
				res = PTR_ERR(dev);
				pr_err("Failed to create device, result=%d\n", res);
				break;
			}

			*p_device = dev;
		}
	} while (false);

	if (res != SUCCESS) {
		class_destroy(blk_snap_class);
		blk_snap_class = NULL;
	}
	return res;
}

void ctrl_sysfs_done(struct device **p_device)
{
	if (*p_device) {
		device_unregister(*p_device);
		*p_device = NULL;
	}

	if (blk_snap_class != NULL) {
		class_remove_file(blk_snap_class, &class_attr_major);

		class_destroy(blk_snap_class);
		blk_snap_class = NULL;
	}
}
