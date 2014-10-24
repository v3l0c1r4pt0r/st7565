#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sysrq.h>
#include <linux/slab.h>
#include <linux/fs.h>
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
        .write	= glcd_write
    };
    st.fops = &fops;
    st.major = register_chrdev(0, DEVICE_NAME, st.fops);
    if(st.major < 0)
    {
        printk(KERN_ALERT "Registering char device failed with %d\n", st.major);
        goto out;
    }
    printk(KERN_INFO "module loaded\n");
    return SUCCESS;
out:
    printk(KERN_INFO "module cannot be loaded\n");
    return error;
}

void cleanup_module()
{
    int ret = unregister_chrdev(st.major, DEVICE_NAME);
    if (ret < 0)
        printk(KERN_ALERT "Error in unregister_chrdev: %d\n", ret);
    printk(KERN_INFO "module unloaded\n");
}

MODULE_LICENSE("GPL");

static int glcd_open(struct inode *inode, struct file *file)
{
    static int counter = 0;

    if (st.dev_opened)
        return -EBUSY;

    st.dev_opened++;
    sprintf(msg, "I already told you %d times Hello world!\n", counter++);
    msgPtr = msg;
    try_module_get(THIS_MODULE);

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
