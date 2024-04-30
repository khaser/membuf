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
MODULE_VERSION("0.1.0");

#define DEVICE_NAME "membuf"

#define MAX_DEV_CNT 255
static uint8_t dev_cnt = 2;

static char* buffs[MAX_DEV_CNT];
static struct device* devices[MAX_DEV_CNT];;
static unsigned int sizes[MAX_DEV_CNT];;

static unsigned int default_size = 256;
module_param(default_size, int, 0444);
MODULE_PARM_DESC(default_size, "Size of a new memory buffer");


// Contain info about allocated major and minor numbers
dev_t dev_region = 0;
// Contain device op functions
static struct cdev membuf_dev;
static struct class *cls;

DECLARE_RWSEM(rw_lock);

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,4,0)
#define CONST
#else
#define CONST const
#endif

static ssize_t dev_cnt_show(CONST struct class *kclass, CONST struct class_attribute *attr, char *buf) {
    int res;
    down_read(&rw_lock);
    res = sprintf(buf, "%d\n", (unsigned int)dev_cnt);
    up_read(&rw_lock);
    return res;
}

static ssize_t dev_cnt_store(CONST struct class *kclass, CONST struct class_attribute *attr, const char *buf, size_t count) {
    unsigned int new_val;
    int res;
    down_write(&rw_lock);
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
    // TODO: logic
    dev_cnt = new_val;
    pr_info("membuf: dev_cnt updated through sys attribute\n");
    exit:
    up_write(&rw_lock);
    return res;
}

CLASS_ATTR_RW(dev_cnt);

static ssize_t size_show(CONST struct device *kdev, CONST struct device_attribute *attr, char *buf) {
    int res;
    int minor = MINOR(kdev->devt);
    down_read(&rw_lock);
    res = sprintf(buf, "%d\n", sizes[minor]);
    up_read(&rw_lock);
    return res;
}

static ssize_t size_store(CONST struct device *kdev, CONST struct device_attribute *attr, const char *buf,
                          size_t count) {
    int minor = MINOR(kdev->devt);
    unsigned int new_val;
    int res;
    down_write(&rw_lock);
    if ((res = kstrtouint(buf, 10, &new_val)) < 0) {
        pr_err("membuf: failed to update size\n");
        goto exit;
    }
    if (new_val < 0) {
        pr_err("membuf: size should be >= 0\n");
        res = -ERANGE;
        goto exit;
    }

    if ((buffs[minor] = kvrealloc(buffs[minor], sizes[minor], new_val, GFP_KERNEL | __GFP_ZERO)) == 0) {
        pr_err("membuf: error on buffer realloc");
        res = -ENOMEM;
        goto exit;
    }
    sizes[minor] = new_val;
    res = count;

    pr_info("membuf: size updated through sys attribute\n");
    exit:
    up_write(&rw_lock);
    return res;
}

DEVICE_ATTR_RW(size);

static ssize_t membuf_read(struct file *filp, char __user *ubuf, size_t len, loff_t *off) {
    int minor = MINOR(filp->f_inode->i_rdev);
    unsigned int size = sizes[minor];
    int to_copy = (len + *off > size ? size - *off : len);
    int res;
    down_read(&rw_lock);
    if (*off >= size) {
        *off = 0;
        res = 0;
        goto exit;
    }
    pr_info("membuf: process %d try to read %lu bytes with %lld offset\n", current->pid, len, *off);
    if (copy_to_user(ubuf, buffs[minor] + *off, to_copy)) {
        pr_info("membuf: copy to user failed, return EFAULT\n");
        res = -EFAULT;
        goto exit;
    }
    pr_info("Data successfully written into user buffer\n");
    *off += to_copy;
    res = to_copy;
    exit:
    up_read(&rw_lock);
    return res;
}

static ssize_t membuf_write(struct file *filp, const char __user *ubuf, size_t len, loff_t *off) {
    int minor = MINOR(filp->f_inode->i_rdev);
    unsigned int size = sizes[minor];
    int to_copy = (len + *off > size ? size - *off : len);
    int res;
    down_write(&rw_lock);
    pr_info("membuf: process %d try to write %lu bytes with %lld offset\n", current->pid, len, *off);
    if (*off >= size) {
        pr_info("membuf: offset out of device buffer range, return ENOSPC\n");
        res = -ENOSPC;
        goto exit;
    }
    if (copy_from_user(buffs[minor] + *off, ubuf, to_copy)) {
        pr_info("membuf: copy from user failed, return EFAULT\n");
        res = -EFAULT;
        goto exit;
    }
    pr_info("Data successfully written into knapsack\n");
    *off += to_copy;
    res = to_copy;
    exit:
    up_write(&rw_lock);
    return res;
}

static struct file_operations membuf_ops = {
    .owner      = THIS_MODULE,
    .read       = membuf_read,
    .write      = membuf_write,
};

// Should be called when lock taken
static ssize_t membuf_device_create(dev_t dev) {
    int res;
    int minor = MINOR(dev);
    if (IS_ERR(devices[minor] = device_create(cls, NULL, dev, 0, DEVICE_NAME "%d", minor))) {
        pr_err("membuf: error on device_create\n");
        res = -1;
        goto fail1;
    }

    if ((res = device_create_file(devices[minor], &dev_attr_size))) {
        pr_err("membuf: error on create `size` sysfs attribute");
        goto fail2;
    }

    buffs[minor] = kvzalloc(default_size, GFP_KERNEL);
    sizes[minor] = default_size;
    if (buffs[minor] == 0) {
        pr_err("membuf: error on buffer allocation\n");
        res = -ENOSPC;
        goto fail3;
    }

    pr_info("membuf: device alloc success\n");
    return res;

    fail3:
    sizes[minor] = 0;
    device_remove_file(devices[minor], &dev_attr_size);
    fail2:
    device_destroy(cls, dev_region);
    fail1:
    devices[minor] = 0;
    return res;
}

static void membuf_device_remove(dev_t dev) {
    int minor = MINOR(dev);
    // Do nothing if device not allocated
    if (devices[minor] != 0) {
        kvfree(buffs[minor]);
        device_remove_file(devices[minor], &dev_attr_size);
        device_destroy(cls, dev);
        sizes[minor] = 0;
        buffs[minor] = 0;
        devices[minor] = 0;
    }
}

static int __init membuf_init(void)
{
    int res;
    dev_t dev;
    pr_info("membuf: module load\n");

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
        res = -1;
        goto fail2;
    }

    if ((res = class_create_file(cls, &class_attr_dev_cnt))) {
        pr_err("membuf: error on create `dev_cnt` sysfs attribute");
        goto fail3;
    }

    for (dev = dev_region; dev < dev_region + dev_cnt; ++dev) {
        if ((res = membuf_device_create(dev))) {
            res = -1;
            goto fail4;
        }
    }

    pr_info("membuf: module init success\n");

    return 0;

    fail4:
    while (--dev != dev_region) {
        membuf_device_remove(dev);
    }
    class_remove_file(cls, &class_attr_dev_cnt);
    fail3:
    class_destroy(cls);
    fail2:
    cdev_del(&membuf_dev);
    fail1:
    unregister_chrdev_region(dev_region, MAX_DEV_CNT);
    return res;
}

static void __exit membuf_cleanup(void) {
    for (dev_t dev = dev_region; dev < dev_region + MAX_DEV_CNT; ++dev) {
        membuf_device_remove(dev);
    }

    class_remove_file(cls, &class_attr_dev_cnt);
    class_destroy(cls);
    cdev_del(&membuf_dev);
    unregister_chrdev_region(dev_region, MAX_DEV_CNT);
    pr_info("membuf: module unload\n");
}

module_init(membuf_init);
module_exit(membuf_cleanup);
