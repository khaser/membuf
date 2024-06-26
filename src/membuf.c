#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <linux/version.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/cdev.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("andrei.khorokhorin@cloudbear.ru");
MODULE_DESCRIPTION("in-memory knapsack for educational purposes");
MODULE_VERSION("0.2.0");

#define DEVICE_NAME "membuf"

#define MAX_DEV_CNT 255
static uint8_t dev_cnt = 1;

struct file_private_data {
    size_t epoch;
};

struct membuf_device {
    char* buff;
    struct device* kdevice;
    unsigned int size;
    size_t epoch; // counts device create+remove operations
    struct rw_semaphore mutex;
};
struct membuf_device devs[MAX_DEV_CNT];

static unsigned int default_size = 256;
module_param(default_size, int, 0444);
MODULE_PARM_DESC(default_size, "Size of a new memory buffer");

// Contain info about allocated major and minor numbers
dev_t dev_region = 0;
// Contain device op functions
static struct cdev membuf_dev;
static struct class *cls;

DECLARE_RWSEM(module_mutex);

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,4,0)
#define CONST
#else
#define CONST const
#endif

static ssize_t size_show(struct device *kdev, struct device_attribute *attr, char *buf) {
    int res;
    int minor = MINOR(kdev->devt);
    down_read(&devs[minor].mutex);
    res = sprintf(buf, "%d\n", devs[minor].size);
    up_read(&devs[minor].mutex);
    return res;
}

static ssize_t size_store(struct device *kdev, struct device_attribute *attr, const char *buf,
                          size_t count) {
    int minor = MINOR(kdev->devt);
    unsigned int new_val;
    int res;
    struct membuf_device *dev = devs + minor;
    down_write(&dev->mutex);
    if ((res = kstrtouint(buf, 10, &new_val)) < 0) {
        pr_err("membuf: failed to update size\n");
        goto exit;
    }
    if (new_val < 0) {
        pr_err("membuf: size should be >= 0\n");
        res = -ERANGE;
        goto exit;
    }

    if ((dev->buff = kvrealloc(dev->buff, dev->size, new_val, GFP_KERNEL | __GFP_ZERO)) == 0) {
        pr_err("membuf: error on buffer realloc");
        res = -ENOMEM;
        goto exit;
    }
    dev->size = new_val;
    res = count;

    pr_info("membuf: size updated through sys attribute\n");
    exit:
    up_write(&dev->mutex);
    return res;
}

DEVICE_ATTR_RW(size);

static ssize_t membuf_device_create(dev_t dev_id) {
    int res;
    int minor = MINOR(dev_id);
    struct membuf_device *dev = devs + minor;
    WARN_ON(!rwsem_is_locked(&module_mutex));

    if (IS_ERR(dev->kdevice = device_create(cls, NULL, dev_id, 0, DEVICE_NAME "%d", minor))) {
        pr_err("membuf: error on device_create\n");
        res = PTR_ERR(dev->kdevice);
        goto fail1;
    }

    if ((res = device_create_file(dev->kdevice, &dev_attr_size))) {
        pr_err("membuf: error on create `size` sysfs attribute");
        goto fail2;
    }

    dev->buff = kvzalloc(default_size, GFP_KERNEL);
    dev->size = default_size;
    if (dev->buff == 0) {
        pr_err("membuf: error on buffer allocation\n");
        res = -ENOMEM;
        goto fail3;
    }

    dev->epoch++;
    init_rwsem(&dev->mutex);
    pr_info("membuf: device alloc success\n");
    return res;

    fail3:
    device_remove_file(dev->kdevice, &dev_attr_size);
    fail2:
    device_destroy(cls, dev_id);
    fail1:
    dev->kdevice = 0;
    dev->size = 0;
    dev->buff = 0;
    return res;
}

static void membuf_device_remove(dev_t dev_id) {
    int minor = MINOR(dev_id);
    struct membuf_device *dev = devs + minor;
    // Do nothing if device not allocated
    WARN_ON(!rwsem_is_locked(&module_mutex));
    if (dev->kdevice != 0) {
        kvfree(dev->buff);
        device_remove_file(dev->kdevice, &dev_attr_size);
        device_destroy(cls, dev_id);
        dev->kdevice = 0;
        dev->size = 0;
        dev->buff = 0;
        dev->epoch++;
    }
}

static ssize_t dev_cnt_show(CONST struct class *kclass, CONST struct class_attribute *attr, char *buf) {
    int res;
    down_read(&module_mutex);
    res = sprintf(buf, "%d\n", (unsigned int)dev_cnt);
    up_read(&module_mutex);
    return res;
}

static ssize_t dev_cnt_store(CONST struct class *kclass, CONST struct class_attribute *attr, const char *buf, size_t count) {
    unsigned int new_val;
    int res;
    down_write(&module_mutex);
    if ((res = kstrtouint(buf, 10, &new_val)) < 0) {
        pr_err("membuf: failed to update dev_cnt\n");
        goto exit;
    }
    if (new_val < 0) {
        pr_err("membuf: dev_cnt should be >= 0\n");
        res = -ERANGE;
        goto exit;
    }
    if (new_val > MAX_DEV_CNT) {
        pr_err("membuf: dev_cnt should be <= MAX_DEV_CNT\n");
        res = -ERANGE;
        goto exit;
    }

    while (dev_cnt != new_val) {
        if (new_val > dev_cnt) {
            if ((res = membuf_device_create(dev_region + dev_cnt++))) {
                goto exit;
            }
        } else {
            membuf_device_remove(dev_region + --dev_cnt);
        }
    }

    res = count;
    pr_info("membuf: dev_cnt updated through sys attribute\n");
    exit:
    up_write(&module_mutex);
    return res;
}

CLASS_ATTR_RW(dev_cnt);

static ssize_t membuf_read(struct file *filp, char __user *ubuf, size_t len, loff_t *off) {
    int minor = iminor(filp->f_inode);
    struct membuf_device *dev = devs + minor;
    struct file_private_data *filp_data = filp->private_data;
    int to_copy = (len + *off > dev->size ? dev->size - *off : len);
    int res;
    down_read(&dev->mutex);
    pr_info("membuf: process %d try to read %lu bytes with %lld offset\n", current->pid, len, *off);
    if (dev->epoch != filp_data->epoch) {
        pr_info("membuf: operation cancelled, out-of-date file descriptor\n");
        res = -ENODEV;
        goto exit;
    }
    if (*off >= dev->size) {
        *off = 0;
        goto exit;
    }
    if (copy_to_user(ubuf, dev->buff + *off, to_copy)) {
        pr_info("membuf: copy to user failed, return EFAULT\n");
        res = -EFAULT;
        goto exit;
    }
    pr_info("membuf: Data successfully written into user buffer\n");
    *off += to_copy;
    res = to_copy;
    exit:
    up_read(&dev->mutex);
    return res;
}

static ssize_t membuf_write(struct file *filp, const char __user *ubuf, size_t len, loff_t *off) {
    int minor = iminor(filp->f_inode);
    struct membuf_device *dev = devs + minor;
    struct file_private_data *filp_data = filp->private_data;
    int to_copy = (len + *off > dev->size ? dev->size - *off : len);
    int res;
    down_write(&dev->mutex);
    pr_info("membuf: process %d try to write %lu bytes with %lld offset\n", current->pid, len, *off);
    if (dev->epoch != filp_data->epoch) {
        pr_info("membuf: operation cancelled, out-of-date file descriptor\n");
        res = -ENODEV;
        goto exit;
    }
    if (*off >= dev->size) {
        pr_info("membuf: offset out of device buffer range, return ENOMEM\n");
        res = -ENOMEM;
        goto exit;
    }
    if (copy_from_user(dev->buff + *off, ubuf, to_copy)) {
        pr_info("membuf: copy from user failed, return EFAULT\n");
        res = -EFAULT;
        goto exit;
    }
    pr_info("membuf: Data successfully written into knapsack\n");
    *off += to_copy;
    res = to_copy;
    exit:
    up_write(&dev->mutex);
    return res;
}

static int membuf_open(struct inode* inode, struct file * filp) {
    int minor = iminor(inode);
    struct membuf_device *dev = devs + minor;
    int res = 0;
    struct file_private_data *filp_data = kmalloc(sizeof(struct file_private_data), GFP_KERNEL);

    down_read(&dev->mutex);
    pr_info("membuf: file opened with epoch %zu\n", dev->epoch);
    if (filp_data == 0) {
        pr_err("membuf: error on buffer allocation\n");
        goto exit;
        res = -ENOMEM;
    }
    filp_data->epoch = dev->epoch;
    filp->private_data = filp_data;
    exit:
    up_read(&dev->mutex);
    return res;
}

static int membuf_release(struct inode* inode, struct file * filp) {
    int minor = iminor(inode);
    struct membuf_device *dev = devs + minor;

    if (filp->private_data) {
        down_read(&dev->mutex);
        pr_info("membuf: file closed\n");
        kfree(filp->private_data);
        filp->private_data = 0;
        up_read(&dev->mutex);
    }
    return 0;
}

static struct file_operations membuf_ops = {
    .owner      = THIS_MODULE,
    .read       = membuf_read,
    .write      = membuf_write,
    .open       = membuf_open,
    .release    = membuf_release,
};

static int __init membuf_init(void)
{
    int res;
    dev_t dev_id;
    pr_info("membuf: module load\n");

    down_write(&module_mutex); // used to satisfy assertions
    if ((res = alloc_chrdev_region(&dev_region, 0, MAX_DEV_CNT, DEVICE_NAME)) < 0) {
        pr_err("Error allocating major number\n");
        return res;
    }

    cdev_init(&membuf_dev, &membuf_ops);

    if ((res = cdev_add(&membuf_dev, dev_region, MAX_DEV_CNT)) < 0) {
        pr_err("membuf: error on dev_add\n");
        goto fail1;
    }

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,4,0)
    if (IS_ERR(cls = class_create(THIS_MODULE, DEVICE_NAME))) {
#else
    if (IS_ERR(cls = class_create(DEVICE_NAME))) {
#endif
        pr_err("membuf: error on class_create\n");
        res = PTR_ERR(cls);
        goto fail2;
    }

    if ((res = class_create_file(cls, &class_attr_dev_cnt))) {
        pr_err("membuf: error on create `dev_cnt` sysfs attribute");
        goto fail3;
    }

    for (dev_id = dev_region; dev_id < dev_region + dev_cnt; ++dev_id) {
        if ((res = membuf_device_create(dev_id))) {
            goto fail4;
        }
    }

    pr_info("membuf: module init success\n");

    up_write(&module_mutex);
    return 0;

    fail4:
    while (--dev_id != dev_region) {
        membuf_device_remove(dev_id);
    }
    class_remove_file(cls, &class_attr_dev_cnt);
    fail3:
    class_destroy(cls);
    fail2:
    cdev_del(&membuf_dev);
    fail1:
    unregister_chrdev_region(dev_region, MAX_DEV_CNT);

    up_write(&module_mutex);
    return res;
}

static void __exit membuf_cleanup(void) {
    down_write(&module_mutex);
    for (dev_t dev_id = dev_region; dev_id < dev_region + MAX_DEV_CNT; ++dev_id) {
        membuf_device_remove(dev_id);
    }

    class_remove_file(cls, &class_attr_dev_cnt);
    class_destroy(cls);
    cdev_del(&membuf_dev);
    unregister_chrdev_region(dev_region, MAX_DEV_CNT);
    pr_info("membuf: module unload\n");
    up_write(&module_mutex);
}

module_init(membuf_init);
module_exit(membuf_cleanup);
