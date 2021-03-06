#include <linux/device.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/stat.h>
#include <linux/sysfs.h>
#include <linux/sysrq.h>
#include <asm/delay.h>
#include <asm/uaccess.h>

#include "st7565.h"

static struct st7565 st;

/*
 * module params
 */
static u8 chip_select = SPI_BUS_CS0;
static unsigned a0_gpio_num = 22;
static unsigned rst_gpio_num = 27;
static unsigned backlight_gpio_num = 2;

module_param(chip_select, byte, S_IRUGO);
module_param(a0_gpio_num, uint, S_IRUGO);
module_param(rst_gpio_num, uint, S_IRUGO);
module_param(backlight_gpio_num, uint, S_IRUGO);

static int __init st7565_init(void)
{
    int error = -1;
    memset(st.buffer, 0, LCD_BUFF_SIZE);
    static struct file_operations fops =
    {
        .open		= glcd_open,
        .release	= glcd_release,
        .read		= glcd_read,
        .write		= glcd_write,
        .llseek		= glcd_llseek
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

    //create attribute for backlight diode in sysfs
    static struct device_attribute backlight = {
        .attr = {
            .name = "backlight",
            .mode = S_IRUSR | S_IWUSR
        },
        .show = st7565_backlight_show,
        .store = st7565_backlight_store
    };
    st.backlight = &backlight;
    error = device_create_file(st.device, st.backlight);
    if(error < 0)
    {
        printk(KERN_ALERT "Creating backlight adjustment attribute failed with %d\n", error);
        goto cdevdel;
    }

    //create attribute for brightness regulation in sysfs
    static struct device_attribute brightness = {
        .attr = {
            .name = "brightness",
            .mode = S_IRUSR | S_IWUSR
        },
        .show = st7565_brightness_show,
        .store = st7565_brightness_store
    };
    st.brightness = &brightness;
    error = device_create_file(st.device, st.brightness);
    if(error < 0)
    {
        printk(KERN_ALERT "Creating brighntess adjustment attribute failed with %d\n", error);
        goto backllrem;
    }

    return SUCCESS;
    
    device_remove_file(st.device, st.brightness);
backllrem:
    device_remove_file(st.device, st.backlight);
cdevdel:
    cdev_del(&st.cdev);
devicedestroy:
    device_destroy(st.cl, st.dev);
destroyclass:
    class_destroy(st.cl);
unregchr:
    unregister_chrdev_region(st.dev, DEVICE_MINORS);
out:
    return error;
}

module_init(st7565_init);

static void __exit st7565_cleanup(void)
{
    st7565_release_lcd();

    device_remove_file(st.device, st.backlight);
    cdev_del(&st.cdev);
    device_destroy(st.cl, st.dev);
    class_destroy(st.cl);
    unregister_chrdev_region(st.dev, DEVICE_MINORS);
}

module_exit(st7565_cleanup);

MODULE_LICENSE("GPL");

static int glcd_open(struct inode *inode, struct file *file)
{
    int error = -1;

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

    /*
     * Number of bytes actually written to the buffer
     */
    if(length + filp->f_pos > LCD_BUFF_SIZE)
        length = LCD_BUFF_SIZE - filp->f_pos;
    
    /*
     * If we're at the end of the message,
     * return 0 signifying end of file
     */
    if (filp->f_pos >= LCD_BUFF_SIZE)
    {
        bytes_read = 0;
        goto out;
    }

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
    int i;

    if(len + filp->f_pos > LCD_BUFF_SIZE)
        len = LCD_BUFF_SIZE - filp->f_pos;

    if (filp->f_pos >= LCD_BUFF_SIZE)
    {
        bytes_written = 0;
        goto out;
    }

    bytes_written = len;

    if(copy_from_user(st.buffer + filp->f_pos, buff, len))
        bytes_written = -EFAULT;

    *off += len;

    //set location on screen
    st7565_set_position(filp->f_pos);

    //send buffer to screen
    for(i = 0; i < len; i++)
    {
        st7565_spi_transfer((st.buffer + filp->f_pos)[i], ST7565_DATA);
	if(filp->f_pos + i % LCD_WIDTH == 0)
	  st7565_set_position(filp->f_pos + i);
    }

out:
    return bytes_written;
}

static loff_t glcd_llseek(struct file * filp, loff_t off, int whence)
{
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
    }

    //set location on screen
    st7565_set_position(filp->f_pos);

    return filp->f_pos;
}

static void st7565_set_position(loff_t pos)
{
    u8 page = pos / LCD_WIDTH;
    u8 column = pos % LCD_WIDTH;

    st7565_spi_transfer(0xb0 | page, ST7565_CMD);			//set page
    st7565_spi_transfer(0x10 | ((0xf0 & column)>>4),ST7565_CMD);	//set 4 msb's of column
    st7565_spi_transfer(0x0f & column,ST7565_CMD);			//set 4 lsb's of column
}

static int st7565_init_lcd(void)
{
    int error = 0;
    int i, j;

    error = st7565_spi_init();
    if(error < 0)
        goto out;

    //init a0, rst, backlight pins
    static struct gpio gpiov[] = {
        {
            .gpio =	ST7565_A0,
            .label =	"st7565->a0"
        },
        {
            .gpio =	ST7565_RST,
            .label =	"st7565->rst"
        },
	{
	    .gpio =	ST7565_BACK,
	    .label =	"st7565->back"
	}
    };
    
    gpiov[GPIO_A0].gpio = a0_gpio_num;
    gpiov[GPIO_RST].gpio = rst_gpio_num;
    gpiov[GPIO_BACK].gpio = backlight_gpio_num;
    
    st.gpioc = 3;
    st.gpiov = gpiov;

    if(gpio_request_array(st.gpiov, st.gpioc))
    {
        error = -1;
        goto out;
    }

    //set pin direction to out
    for(i = 0; i < st.gpioc; i++)
    {
        if(gpio_direction_output(st.gpiov[i].gpio, 0))
        {
            error = -1;
            gpio_free_array(st.gpiov,st.gpioc);
            goto out;
        }
    }
    
    st7565_init_backlight();

    //hardware reset lcd
    udelay(1);
    gpio_set_value(st.gpiov[GPIO_RST].gpio, 1);

    //initialization procedure
    for(i = 0; i < sizeof(initcmd); i++)
    {
        error = st7565_spi_transfer(initcmd[i], ST7565_CMD);
        if(error < 0)
        {
            gpio_free_array(st.gpiov,st.gpioc);
            goto out;
        }
    }
    
    //remember brightness value in st7565 struct
    st.brightness_state = 0x18;

    //clear st7565 buffer
    for(i = 0; i < LCD_HEIGHT; i++)
    {
	st7565_set_position(LCD_HEIGHT * i);
        for(j = 0; j < LCD_WIDTH; j++)
            st7565_spi_transfer(0,ST7565_DATA);
    }
out:
    return error;
}

static void st7565_release_lcd(void)
{
    spi_unregister_device(st.spi_device);
    spi_unregister_driver(st.spi_driver);

    st7565_release_backlight();

    //hardware reset lcd
    gpio_set_value(st.gpiov[GPIO_RST].gpio, 0);
    udelay(1);
    gpio_set_value(st.gpiov[GPIO_RST].gpio, 1);

    gpio_free_array(st.gpiov, st.gpioc);
}

static int st7565_init_backlight(void)
{
    gpio_set_value(st.gpiov[GPIO_BACK].gpio, 1);

    st.backlight_state = 1;

    return SUCCESS;
}

static void st7565_release_backlight(void)
{
    gpio_set_value(st.gpiov[GPIO_BACK].gpio, 0);
}

static int st7565_spi_init(void)
{
    int error = 0;
    struct spi_master *spi_master;
    struct spi_device *spi_device;
    struct device *pdev;
    char devname[64];
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
        spi_unregister_driver(st.spi_driver);
        return -1;
    }

    spi_device = spi_alloc_device(spi_master);
    if (!spi_device) {
        put_device(&spi_master->dev);
	//FIXME: somethings wrong here
    }

    spi_device->chip_select = chip_select;

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

static int st7565_spi_transfer(u8 byte, int a0)
{
    int error = 0;
    struct spi_transfer spi_transfer =  {
        .tx_buf = &byte,
        .rx_buf = NULL,
        .len	= 1
    };
    struct spi_message spi_message;

    if (down_interruptible(&st.spi_sem))
        return -ERESTARTSYS;

    if (!st.spi_device) {
        up(&st.spi_sem);
        return -ENODEV;
    }

    spi_message_init(&spi_message);
    spi_message_add_tail(&spi_transfer, &spi_message);

    //set a0 state
    gpio_set_value(st.gpiov[GPIO_A0].gpio, a0);

    error = spi_sync(st.spi_device, &spi_message);

    //reverse a0 state
    if(a0)
        gpio_set_value(st.gpiov[GPIO_A0].gpio, 0);
    else
        gpio_set_value(st.gpiov[GPIO_A0].gpio, 1);

    up(&st.spi_sem);

    return error;
}

ssize_t st7565_backlight_show(struct device *dev, struct device_attribute *attr,
                              char *buf)
{
    return sprintf(buf, "%d\n", st.backlight_state);
}

ssize_t st7565_backlight_store(struct kobject *dev, struct attribute *attr, const char *buf, size_t count)
{
    int state;
    if(sscanf(buf, "%d\n", &state) == 1 && state <= 1)
    {
        gpio_set_value(st.gpiov[GPIO_A0].gpio, state);
        st.backlight_state = state;

        return count;
    }
    else
        return -EINVAL;
}

ssize_t st7565_brightness_show(struct device *dev, struct device_attribute *attr,
                              char *buf)
{
    return sprintf(buf, "%d\n", st.brightness_state);
}

ssize_t st7565_brightness_store(struct kobject *dev, struct attribute *attr, const char *buf, size_t count)
{
    int state;
    int error;
    if(sscanf(buf, "%d\n", &state) == 1 && state >= 0 && state < 0x40)
    {
        //send brightness to lcd
        error = st7565_spi_transfer(0x81, ST7565_CMD);
	if(error < 0)
	  return -EINVAL;
        error = st7565_spi_transfer(state, ST7565_CMD);
	if(error < 0)
	  return -EINVAL;
	
        st.brightness_state = state;

        return count;
    }
    else
        return -EINVAL;
}
