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
static char *msgPtr;

int init_module()
{
    int error = -1;
    handle_sysrq('g');	// st7565.ko+0x24
    //comment out if not debug
    /*FIXME:TMP*/
    unsigned char buf[] = "\0\1\2\3\4\5\6\7\10\11\12\13\14\15\16\17\20\21\22\23\24\25\26\27\30\31\32\33\34\35\36\37"
                          " !\"#$%&'()*+,-./0123456789:;<=>?"
                          "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
                          "`abcdefghijklmnopqrstuvwxyz{|}~\177";
    memcpy(st.buffer, buf, 128);
    st.buffer[128]='\0';
    /*FIXME:END*/
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
    int error = -1;

    if (st.dev_opened)
    {
        printk(KERN_INFO "device already in use\n");
        return -EBUSY;
    }

    st.dev_opened++;
    msgPtr = st.buffer;
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
    unsigned long bytes_read;

    /*
     * Number of bytes actually written to the buffer
     */
    if(offset + length > (loff_t*)st.buffer + LCD_BUFF_SIZE)
        length = (loff_t*)st.buffer + LCD_BUFF_SIZE - offset;

    /*
     * Set msgPtr's offset to offset
     */
//     msgPtr += offset;

    /*
     * If we're at the end of the message,
     * return 0 signifying end of file
     */
    if (*(st.buffer + filp->f_pos) == 0)
        return 0;

    if(copy_to_user(buffer,
                    st.buffer + filp->f_pos,
                    length))
        bytes_read = -EFAULT;

    offset += length;

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
    //TODO: change position according to off and whence; update position on ST7565
    return filp->f_pos;
}
