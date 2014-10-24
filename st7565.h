#include <linux/fs.h>

#define SUCCESS 0

struct st7565 {
  struct file_operations *fops;
  unsigned int major;
}