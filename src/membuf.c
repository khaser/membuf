#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/cdev.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("andrei.khorokhorin@cloudbear.ru");
MODULE_DESCRIPTION("in-memory knapsack for educational purposes");
MODULE_VERSION("0.0.1");

#define DEVICE_NAME "membuf"

dev_t dev = 0;
static struct cdev membuf_dev;
static struct class *cls;
char* buff;

static unsigned int size = 256;
module_param(size, int, 0444);
MODULE_PARM_DESC(size, "Size of the memory buffer");
static struct kobject *buff_obj;

static ssize_t size_show(struct kobject *kobj, struct kobj_attribute *attr, char* buf) {
    return sprintf(buf, "%d\n", size);
}

static ssize_t size_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
                          size_t count) {
    unsigned int old_size = size;
    int res;
    if ((res = kstrtouint(buf, 10, &size)) < 0) {
        pr_err("membuf: failed to update size\n");
        return res;
    }
    if (size == 0) {
        pr_err("membuf: size should be >= 0\n");
        return -ERANGE;
    }
    buff = kvrealloc(buff, old_size, size + 1, GFP_KERNEL | __GFP_ZERO);
    return count;
}

static struct kobj_attribute size_attribute = __ATTR(size, 0660, size_show, size_store);


static ssize_t membuf_read(struct file *filp, char __user *ubuf, size_t len, loff_t *off) {
    int to_copy = (len + *off > size ? size - *off : len);
    if (*off >= size) {
        *off = 0;
        return 0;
    }
    pr_info("membuf: process %d try to read %lu bytes with %lld offset\n", current->pid, len, *off);
    if (copy_to_user(ubuf, buff + *off, to_copy)) {
        pr_info("membuf: copy to user failed, return EFAULT\n");
        return -EFAULT;
    }
    pr_info("Data successfully written into user buffer\n");
    *off += to_copy;
    return to_copy;
}


static ssize_t membuf_write(struct file *filp, const char __user *ubuf, size_t len, loff_t *off) {
    int to_copy = (len + *off > size ? size - *off : len);
    if (*off >= size) {
        *off = 0;
        return 0;
    }
    pr_info("membuf: process %d try to write %lu bytes with %lld offset\n", current->pid, len, *off);
    if (copy_from_user(buff + *off, ubuf, to_copy)) {
        pr_info("membuf: copy from user failed, return EFAULT\n");
        return -EFAULT;
    }
    pr_info("Data successfully written into knapsack\n");
    buff[*off + len] = '\0';
    *off += to_copy;
    return to_copy;
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

    if (IS_ERR(cls = class_create(THIS_MODULE, DEVICE_NAME))) {
        pr_err("membuf: error on class_create\n");
        res = -1;
        goto fail2;
    }

    if (IS_ERR(device_create(cls, NULL, dev, 0, DEVICE_NAME))) {
        pr_err("membuf: error on device_create\n");
        res = -1;
        goto fail3;
    }

    if (!(buff_obj = kobject_create_and_add("membuf", buff_obj))) {
        pr_err("membuf: error on membuf kobject create");
        goto fail4;
    }

    if ((res = sysfs_create_file(buff_obj, &size_attribute.attr))) {
        pr_err("membuf: error on sysfs attribute file create");
        goto fail5;
    }

    buff = kvzalloc(size + 1, GFP_KERNEL);
    if (buff == 0) {
        pr_err("membuf: error on buffer allocation\n");
        res = -1;
        goto fail5;
    }

    pr_info("membuf: device created successfully\n");

    return 0;

    fail5:
    kobject_put(buff_obj);
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
    kobject_put(buff_obj);
    device_destroy(cls, dev);
    class_destroy(cls);
    cdev_del(&membuf_dev);
    unregister_chrdev_region(dev, 1);
    pr_info("membuf: module unload\n");
}

module_init(membuf_init);
module_exit(membuf_cleanup);
