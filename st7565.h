#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>

#define SUCCESS		0
#define DEVICE_NAME	"glcd"
#define DEVICE_MINORS	1
#define LCD_WIDTH	128
#define LCD_HEIGHT	64
#define LCD_BUFF_SIZE	(LCD_WIDTH * LCD_HEIGHT / 8)

#define ST7565_BACK	2
#define ST7565_CS	17
#define ST7565_RST	27
#define ST7565_A0	22

struct st7565 {
    dev_t dev;
    struct class *cl;
    struct device *device;
    struct cdev cdev;
    struct file_operations *fops;
    unsigned int major;
    int dev_opened;
    unsigned char buffer[LCD_BUFF_SIZE];
};

const uint8_t initcmd[] =
{
    0xa1,							//screen orientation
    0x41,							//set starting line
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

static void st7565_init_lcd(void);
static void st7565_release_lcd(void);
