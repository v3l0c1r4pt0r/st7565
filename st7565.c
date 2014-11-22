#include <linux/device.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/sysrq.h>
#include <asm/uaccess.h>

#include "st7565.h"

/*
 *
 * TODO:
 *
 * - custom pinout as driver params
 *
 */

static struct st7565 st;

static int __init st7565_init(void)
{
    int error = -1;
    memset(st.buffer, 0, LCD_BUFF_SIZE);
    static struct file_operations fops =
    {
        .open	= glcd_open,
        .release	= glcd_release,
        .read	= glcd_read,
        .write	= glcd_write,
        .llseek	= glcd_llseek
    };
    handle_sysrq('g');	// st7565.ko+0x24 ??
    //comment out if not debug
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

    //initialize semaphores for spi device operations
    sema_init(&st.spi_sem, 1);
    sema_init(&st.fop_sem, 1);

    error = st7565_init_lcd();
    if(error < 0)
        goto cdevdel;

    printk(KERN_INFO "module loaded\n");
    return SUCCESS;
cdevdel:
    cdev_del(&st.cdev);
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
    st7565_release_lcd();

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

static int st7565_init_lcd(void)
{
    int error = 0;
    struct spi_master *spi_master;
    struct spi_device *spi_device;
    struct device *pdev;
    char devname[64];

    st7565_init_backlight();
    //TODO: initialization procedure

    static struct spi_driver spi_driver = {
        .driver = {
            .name = DEVICE_NAME,
            .owner = THIS_MODULE,
        },
        .probe = st7565_spi_probe,
        .remove = st7565_spi_remove,
    };

    st.spi_driver = &spi_driver;

    error = spi_register_driver(st.spi_driver);

    spi_master = spi_busnum_to_master(SPI_BUS);
    if (!spi_master) {
        printk(KERN_ALERT "SPI controller cannot be found!\n");
        return -1;
    }

    spi_device = spi_alloc_device(spi_master);
    if (!spi_device) {
        put_device(&spi_master->dev);
        //TODO
    }

    //FIXME: use chipselect according to driver parameter
    spi_device->chip_select = SPI_BUS_CS0;

    /* Check whether this SPI bus.cs is already claimed */
    snprintf(devname, sizeof(devname), "%s.%u",
             dev_name(&spi_device->master->dev),
             spi_device->chip_select);

    pdev = bus_find_device_by_name(spi_device->dev.bus, NULL, devname);
    if (pdev) {
        /* We are not going to use this spi_device, so free it */
        spi_dev_put(spi_device);
        /*
        * There is already a device configured for this bus.cs
        * It is okay if it us, otherwise complain and fail.
        */
        if (pdev->driver && pdev->driver->name &&
                strcmp(DEVICE_NAME, pdev->driver->name)) {
            printk(KERN_ALERT
                   "Driver '%s' already registered for %s\n",
                   pdev->driver->name, devname);
            error =  -1;
            goto out;
        }
    } else {
        spi_device->max_speed_hz = SPI_MAX_SPEED;
        spi_device->mode = SPI_MODE_0;
        spi_device->bits_per_word = 8;
        spi_device->irq = -1;
        spi_device->controller_state = NULL;
        spi_device->controller_data = NULL;
        strlcpy(spi_device->modalias, DEVICE_NAME, SPI_NAME_SIZE);
        error = spi_add_device(spi_device);
        if (error < 0) {
            spi_dev_put(spi_device);
            printk(KERN_ALERT "spi_add_device() failed: %d\n",
                   error);
        }
    }
    put_device(&spi_master->dev);
out:
    return error;
}

static void st7565_release_lcd(void)
{
    spi_unregister_device(st.spi_device);
    spi_unregister_driver(st.spi_driver);

    st7565_release_backlight();
}

static int st7565_init_backlight(void)
{
    int error = 0;
    struct gpio gpio = {
        .gpio =		ST7565_BACK,
        .label =	"st7565->back"
    };
    struct gpio gpiov[] = {gpio};
    if(gpio_request_array(gpiov, 1))
        ;//TODO: fail
    if(gpio_direction_output(gpio.gpio, 0))
        ;//TODO: fail
    gpio_set_value(gpio.gpio, 1);

    return error;
}

static void st7565_release_backlight(void)
{
    struct gpio gpio = {
        .gpio =		ST7565_BACK,
        .label =	"st7565->back"
    };
    struct gpio gpiov[] = {gpio};
    gpio_set_value(gpio.gpio, 0);
    gpio_free_array(gpiov, 1);
}

static int st7565_spi_probe(struct spi_device *spi_device)
{
    if (down_interruptible(&st.spi_sem))
        return -EBUSY;
    st.spi_device = spi_device;
    up(&st.spi_sem);
    return 0;
}
static int st7565_spi_remove(struct spi_device *spi_device)
{
    if (down_interruptible(&st.spi_sem))
        return -EBUSY;
    st.spi_device = NULL;
    up(&st.spi_sem);
    return 0;
}

static int st7565_spi_transfer(u8 byte)
{
    int error = 0;
    struct spi_transfer spi_transfer =  {
        .tx_buf = NULL,
        .rx_buf = NULL,
        .len	= 1
    };
    struct spi_message spi_message;
    
    spi_message_init(&spi_message);
    spi_message_add_tail(&spi_transfer, &spi_message);
    error = spi_sync(st.spi_device, &spi_message);
    
    return error;
}
