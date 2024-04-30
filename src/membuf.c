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

// Contain info about allocated major and minor numbers
dev_t dev = 0;
// Contain device op functions
static struct cdev membuf_dev;
static struct class *cls;
char* buff;

DECLARE_RWSEM(rw_lock);

static unsigned int size = 256;
module_param(size, int, 0444);
MODULE_PARM_DESC(size, "Size of the memory buffer");

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,4,0)
#define CONST
#else
#define CONST const
#endif
static ssize_t size_show(CONST struct class *kclass, CONST struct class_attribute *attr, char* buf) {
    int res;
    down_read(&rw_lock);
    res = sprintf(buf, "%d\n", size);
    up_read(&rw_lock);
    return res;
}

static ssize_t size_store(CONST struct class *kclass, CONST struct class_attribute *attr, const char *buf,
                          size_t count) {
    unsigned int old_size;
    int res;
    down_write(&rw_lock);
    old_size = size;
    if ((res = kstrtouint(buf, 10, &size)) < 0) {
        pr_err("membuf: failed to update size\n");
        goto exit;
    }
    if (size == 0) {
        pr_err("membuf: size should be >= 0\n");
        res = -ERANGE;
        goto exit;
    }
    buff = kvrealloc(buff, old_size, size, GFP_KERNEL | __GFP_ZERO);
    if (buff == 0) {
        pr_err("membuf: error on buffer realloc");
        res = -ENOMEM;
        goto exit;
    }
    res = count;
    exit:
    up_write(&rw_lock);
    pr_info("membuf: size updated through sys attribute\n");
    return res;
}

CLASS_ATTR_RW(size);

static ssize_t membuf_read(struct file *filp, char __user *ubuf, size_t len, loff_t *off) {
    int to_copy = (len + *off > size ? size - *off : len);
    int res;
    down_read(&rw_lock);
    if (*off >= size) {
        *off = 0;
        res = 0;
        goto exit;
    }
    pr_info("membuf: process %d try to read %lu bytes with %lld offset\n", current->pid, len, *off);
    if (copy_to_user(ubuf, buff + *off, to_copy)) {
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
    int to_copy = (len + *off > size ? size - *off : len);
    int res;
    down_write(&rw_lock);
    pr_info("membuf: process %d try to write %lu bytes with %lld offset\n", current->pid, len, *off);
    if (*off >= size) {
        pr_info("membuf: offset out of device buffer range, return ENOSPC\n");
        res = -ENOSPC;
        goto exit;
    }
    if (copy_from_user(buff + *off, ubuf, to_copy)) {
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

static int __init membuf_init(void)
{
    int res;
    pr_info("membuf: module load\n");

    if ((res = alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME)) < 0) {
        pr_err("Error allocating major number\n");
        return res;
    }

    cdev_init(&membuf_dev, &membuf_ops);

    if ((res = cdev_add(&membuf_dev, dev, 1)) < 0) {
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

    if (IS_ERR(device_create(cls, NULL, dev, 0, DEVICE_NAME))) {
        pr_err("membuf: error on device_create\n");
        res = -1;
        goto fail3;
    }

    /* // TODO: 6.6 version doesn't work */
    if ((res = class_create_file(cls, &class_attr_size))) {
        pr_err("membuf: error on sysfs attribute file create");
        goto fail4;
    }

    buff = kvzalloc(size, GFP_KERNEL);
    if (buff == 0) {
        pr_err("membuf: error on buffer allocation\n");
        res = -1;
        goto fail5;
    }

    pr_info("membuf: device created successfully\n");

    return 0;

    fail5:
    class_remove_file(cls, &class_attr_size);
    fail4:
    device_destroy(cls, dev);
    fail3:
    class_destroy(cls);
    fail2:
    cdev_del(&membuf_dev);
    fail1:
    unregister_chrdev_region(dev, 1);
    return res;
}

static void __exit membuf_cleanup(void) {
    kvfree(buff);
    class_remove_file(cls, &class_attr_size);
    device_destroy(cls, dev);
    class_destroy(cls);
    cdev_del(&membuf_dev);
    unregister_chrdev_region(dev, 1);
    pr_info("membuf: module unload\n");
}

module_init(membuf_init);
module_exit(membuf_cleanup);
