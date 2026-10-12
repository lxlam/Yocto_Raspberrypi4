#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>

static struct cdev *my_cdev;
static dev_t my_dev;

static size_t my_read()

static int __init file_operation_init(void)
{
    int ret;
    /* allocate character device number */
    ret = alloc_chrdev_region(&my_dev, 0, 1, "file_operation_driver");
    if (ret < 0) {
        printk(KERN_ERR "LamLX - Failed to allocate character device number\n");
        return ret;
    }


    printk(KERN_INFO "LamLX - File operation driver initialized\n");
    return 0;
}

static void __exit file_operation_exit(void)
{
    printk(KERN_INFO "LamLX -File operation driver exited\n");
}

module_init(file_operation_init);
module_exit(file_operation_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("lamlx");
MODULE_DESCRIPTION("My first kernel module");
MODULE_VERSION("1.0");