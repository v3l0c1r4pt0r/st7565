#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sysrq.h>
#include <asm/uaccess.h>

#include "st7565.h"

static struct st7565 st;

static int __init st7565_init(void)
{
    int error = -1;
    memset(st.buffer, 0, LCD_BUFF_SIZE);
    //comment out if not debug
    /*FIXME:TMP*/
    unsigned char buf[] = //"\0\1\2\3\4\5\6\7\10\11\12\13\14\15\16\17\20\21\22\23\24\25\26\27\30\31\32\33\34\35\36\37"
        " !\"#$%&'()*+,-./0123456789:;<=>?"
        "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
        "`abcdefghijklmnopqrstuvwxyz{|}~\177"
        "e4htewfne8n4guiengeuihfre78bgf87"
        "p[dlf][ewo04owe[_{O{)i9pJo9jO[0I"
        "78hiUG&*G&*g*&g&g*&FG7F6%d%$6d4S"
        "f%^YTf%d%$ds235s5d6f7&^f^7fG&*(h"
        ":Llp[p[;p[lplooikKIOPLp[l[lppkok"
        " !\"#$%&'()*+,-./0123456789:;<=>?"
        "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
        "`abcdefghijklmnopqrstuvwxyz{|}~\177"
        "e4htewfne8n4guiengeuihfre78bgf87"
        "p[dlf][ewo04owe[_{O{)i9pJo9jO[0I"
        "78hiUG&*G&*g*&g&g*&FG7F6%d%$6d4S"
        "f%^YTf%d%$ds235s5d6f7&^f^7fG&*(h"
        ":Llp[p[;p[lplooikKIOPLp[l[lppkok"
        " !\"#$%&'()*+,-./0123456789:;<=>?"
        "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
        "`abcdefghijklmnopqrstuvwxyz{|}~\177"
        "e4htewfne8n4guiengeuihfre78bgf87"
        "p[dlf][ewo04owe[_{O{)i9pJo9jO[0I"
        "78hiUG&*G&*g*&g&g*&FG7F6%d%$6d4S"
        "f%^YTf%d%$ds235s5d6f7&^f^7fG&*(h"
        ":Llp[p[;p[lplooikKIOPLp[l[lppkok"
        " !\"#$%&'()*+,-./0123456789:;<=>?"
        "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
        "`abcdefghijklmnopqrstuvwxyz{|}~\177"
        "e4htewfne8n4guiengeuihfre78bgf87"
        "p[dlf][ewo04owe[_{O{)i9pJo9jO[0I"
        "78hiUG&*G&*g*&g&g*&FG7F6%d%$6d4S"
        "f%^YTf%d%$ds235s5d6f7&^f^7fG&*(h"
        ":Llp[p[;p[lplooikKIOPLp[l[lppkok";
    memcpy(st.buffer, buf, LCD_BUFF_SIZE);
    /*FIXME:END*/
    static struct file_operations fops =
    {
        .open	= glcd_open,
        .release	= glcd_release,
        .read	= glcd_read,
        .write	= glcd_write,
        .llseek	= glcd_llseek
    };
    handle_sysrq('g');	// st7565.ko+0x24 ??
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
        error = -1;
        printk(KERN_ALERT "Class creation failed\n");
        goto unregchr;
    }
    st.device = device_create(st.cl, NULL, st.dev, NULL, DEVICE_NAME);
    if(st.device == NULL)
    {
        error = -1;
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

module_init(st7565_init);

static void __exit st7565_cleanup(void)
{
    cdev_del(&st.cdev);
    device_destroy(st.cl, st.dev);
    class_destroy(st.cl);
    unregister_chrdev_region(st.dev, DEVICE_MINORS);
    printk(KERN_INFO "module unloaded\n");
}

module_exit(st7565_cleanup);

MODULE_LICENSE("GPL");

static int glcd_open(struct inode *inode, struct file *file)
{
    int error = -1;
    printk(KERN_INFO "opened glcd device\n");

    if (st.dev_opened)
    {
        return -EBUSY;
    }

    st.dev_opened++;

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
    ssize_t bytes_read;
    printk(KERN_INFO "reading glcd device\n");

    /*
     * Number of bytes actually written to the buffer
     */
// printk(KERN_INFO "length: 0x%X; buffer: 0x%X; fpos: 0x%X\n", length, st.buffer, filp->f_pos);
    if(length + filp->f_pos > LCD_BUFF_SIZE)
        length = LCD_BUFF_SIZE - filp->f_pos;
// printk(KERN_INFO "length: 0x%X; buffer: 0x%X; fpos: 0x%X\n", length, st.buffer, filp->f_pos);
    /*
     * If we're at the end of the message,
     * return 0 signifying end of file
     */
    if (filp->f_pos == LCD_BUFF_SIZE)
    {
        bytes_read = 0;
        goto out;
    }
//     printk(KERN_INFO "%c, %c", *(st.buffer), *(st.buffer + filp->f_pos));

    bytes_read = length;

    if(copy_to_user(buffer,
                    st.buffer + filp->f_pos,
                    length))
        bytes_read = -EFAULT;

    *offset += length;

    /*
     * Most read functions return the number of bytes put into the buffer
     */
out:
    return bytes_read;
}

static ssize_t glcd_write(struct file *filp, const char *buff, size_t len, loff_t * off)
{
    ssize_t bytes_written;
    
    if(len + filp->f_pos > LCD_BUFF_SIZE)
        len = LCD_BUFF_SIZE - filp->f_pos;
    
    if (filp->f_pos == LCD_BUFF_SIZE)
    {
        bytes_written = 0;
        goto out;
    }
    
    bytes_written = len;

    if(copy_from_user(st.buffer + filp->f_pos, buff, len))
        bytes_written = -EFAULT;
    
    //TODO: send data to ST7565
    
out:
    return bytes_written;
}

static loff_t glcd_llseek(struct file * filp, loff_t off, int whence)
{
    //TODO: update position on ST7565
    switch(whence)
    {
    case SEEK_SET:
        filp->f_pos = off;
        break;
    case SEEK_CUR:
        filp->f_pos += off;
        break;
    case SEEK_END:
        filp->f_pos = LCD_BUFF_SIZE + off;
        break;
    default:
        printk(KERN_INFO "I wasn't expected to get here!\n");
        //FIXME: what about the case when f_pos is beeing set outside of buffer, what would be read then?
    }
    return filp->f_pos;
}
