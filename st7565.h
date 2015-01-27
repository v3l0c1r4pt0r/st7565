#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/spi/spi.h>

#define SUCCESS		0
#define DEVICE_NAME	"glcd"
#define DEVICE_MINORS	1
#define LCD_WIDTH	128
#define LCD_HEIGHT	64
#define LCD_BUFF_SIZE	(LCD_WIDTH * LCD_HEIGHT / 8)

#define ST7565_BACK	2
// #define ST7565_CS	17
#define ST7565_RST	27
#define ST7565_A0	22

#define SPI_BUS		0
#define SPI_BUS_CS0	0
#define SPI_BUS_CS1	1
#define SPI_MAX_SPEED	62600000

#define GPIO_A0		0
#define GPIO_RST	1
#define GPIO_BACK	2

#define ST7565_CMD	0
#define ST7565_DATA	1

#define BRIGHTNESS_MASK	0x3f

struct st7565 {
    dev_t dev;
    struct class *cl;
    struct device *device;
    struct cdev cdev;
    struct file_operations *fops;
    unsigned int major;
    int dev_opened;
    unsigned char buffer[LCD_BUFF_SIZE];
    struct semaphore spi_sem;
    struct semaphore fop_sem;
    struct spi_device *spi_device;
    struct spi_driver *spi_driver;
    struct gpio *gpiov;
    int gpioc;
    struct device_attribute *backlight;
    int backlight_state;
    struct device_attribute *brightness;
    int brightness_state;
};

const uint8_t initcmd[] =
{
    0xa1,							//screen orientation
    0x40,							//set starting line
    0xc0,							//page count direction
    0xa3,							//1/7 bias
    0x2c,							//vc
    0x2e,							//vc+vr
    0x2f,							//vc+vr+vf
    0x24,							//voltage regulator (0x20-0x27)
    0xa6,							//do not reverse the display
    0xaf,							//display on
    0xa4,							//display from ram
    0x81,							//turn on brightness regulation
    0x18							//set brightness (0x0-0x40)
};

static int glcd_open(struct inode *inode, struct file *file);
static int glcd_release(struct inode *inode, struct file *file);
static ssize_t glcd_read(struct file *filp, char *buffer, size_t length, loff_t * offset);
static ssize_t glcd_write(struct file *filp, const char *buff, size_t len, loff_t * off);
static loff_t glcd_llseek(struct file* filp, loff_t off, int whence);

static int st7565_init_lcd(void);
static void st7565_release_lcd(void);
static int st7565_init_backlight(void);
static void st7565_release_backlight(void);
static void st7565_set_position(loff_t pos);

static int st7565_spi_init(void);
static int st7565_spi_probe(struct spi_device *spi_device);
static int st7565_spi_remove(struct spi_device *spi_device);

static int st7565_spi_transfer(u8 byte, int a0);

ssize_t st7565_backlight_show(struct device *dev, struct device_attribute *attr, char *buf);
ssize_t st7565_backlight_store(struct kobject *dev, struct attribute *attr, const char *buf, size_t count);

ssize_t st7565_brightness_show(struct device *dev, struct device_attribute *attr, char *buf);
ssize_t st7565_brightness_store(struct kobject *dev, struct attribute *attr, const char *buf, size_t count);
