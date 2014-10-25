#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sysrq.h>
#include <asm/uaccess.h>

#include "st7565.h"

static struct st7565 st;

//for test purposes only
#define BUF_LEN 80		/* Max length of the message from the device */
static char msg[BUF_LEN];	/* The msg the device will give when asked */
static char *msgPtr;

int init_module()
{
    int error = -1;
    handle_sysrq('g');	// st7565.ko+0x24
    //comment out if not debug
    static struct file_operations fops =
    {
        .open	= glcd_open,
        .release	= glcd_release,
        .read	= glcd_read,
        .write	= glcd_write,
        .llseek	= glcd_llseek
    };
    st.fops = &fops;
    error = alloc_chrdev_region(&st.dev, 0, DEVICE_MINORS, DEVICE_NAME);
    if(error < 0)
    {
        printk(KERN_ALERT "Device major number allocation failed with %d\n", error);
        goto out;
    }
    st.cl = class_create(THIS_MODULE, DEVICE_NAME);
    if(st.cl == NULL)
    {
        printk(KERN_ALERT "Class creation failed\n");
        goto unregchr;
    }
    st.device = device_create(st.cl, NULL, st.dev, NULL, DEVICE_NAME);
    if(st.device == NULL)
    {
        printk(KERN_ALERT "Device creation failed\n");
        goto destroyclass;
    }
    cdev_init(&st.cdev, st.fops);
    error = cdev_add(&st.cdev, st.dev, DEVICE_MINORS);
    if(error < 0)
    {
      printk(KERN_ALERT "Adding character device failed with %d\n", error);
      goto devicedestroy;
    }
    printk(KERN_INFO "module loaded\n");
    return SUCCESS;
devicedestroy:
    device_destroy(st.cl, st.dev);
destroyclass:
    class_destroy(st.cl);
unregchr:
    unregister_chrdev_region(st.dev, DEVICE_MINORS);
out:
    printk(KERN_INFO "module cannot be loaded\n");
    return error;
}

void cleanup_module()
{
    device_destroy(st.cl, st.dev);
    class_destroy(st.cl);
    unregister_chrdev_region(st.dev, DEVICE_MINORS);
    printk(KERN_INFO "module unloaded\n");
}

MODULE_LICENSE("GPL");

static int glcd_open(struct inode *inode, struct file *file)
{
    static int counter = 0;
    int error = -1;

    if (st.dev_opened)
        return -EBUSY;

    st.dev_opened++;
    sprintf(msg, "I already told you %d times Hello world!\n", counter++);
    msgPtr = msg;
    error = try_module_get(THIS_MODULE);
    if(error == 0)
      return -1;

    return SUCCESS;
}

static int glcd_release(struct inode *inode, struct file *file)
{
    st.dev_opened--;		/* We're now ready for our next caller */

    /*
     * Decrement the usage count, or else once you opened the file, you'll
     * never get get rid of the module.
     */
    module_put(THIS_MODULE);

    return 0;
}

static ssize_t glcd_read(struct file *filp,	/* see include/linux/fs.h   */
                         char *buffer,	/* buffer to fill with data */
                         size_t length,	/* length of the buffer     */
                         loff_t * offset)
{
    /*
     * Number of bytes actually written to the buffer
     */
    int bytes_read = 0;

    /*
     * If we're at the end of the message,
     * return 0 signifying end of file
     */
    if (*msgPtr == 0)
        return 0;

    /*
     * Actually put the data into the buffer
     */
    while (length && *msgPtr) {

        /*
         * The buffer is in the user data segment, not the kernel
         * segment so "*" assignment won't work.  We have to use
         * put_user which copies data from the kernel data segment to
         * the user data segment.
         */
        put_user(*(msgPtr++), buffer++);

        length--;
        bytes_read++;
    }

    /*
     * Most read functions return the number of bytes put into the buffer
     */
    return bytes_read;
}

static ssize_t
glcd_write(struct file *filp, const char *buff, size_t len, loff_t * off)
{
    printk(KERN_ALERT "Sorry, this operation isn't supported.\n");
    return -EINVAL;
}

static loff_t glcd_llseek(struct file * filp, loff_t off, int whence)
{
  //TODO: change position according to off and whence
  return filp->f_pos;
}
