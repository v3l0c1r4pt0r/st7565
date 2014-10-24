#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>

#define SUCCESS 0
#define DEVICE_NAME "glcd"
#define DEVICE_MINORS	1

struct st7565 {
  dev_t dev;
  struct class *cl;
  struct device *device;
  struct cdev cdev;
  struct file_operations *fops;
  unsigned int major;
  int dev_opened;
};

static int glcd_open(struct inode *inode, struct file *file);
static int glcd_release(struct inode *inode, struct file *file);
static ssize_t glcd_read(struct file *filp, char *buffer, size_t length, loff_t * offset);
static ssize_t glcd_write(struct file *filp, const char *buff, size_t len, loff_t * off);
