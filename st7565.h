#include <linux/fs.h>

#define SUCCESS 0
#define DEVICE_NAME "glcd"

struct st7565 {
  struct file_operations *fops;
  unsigned int major;
}