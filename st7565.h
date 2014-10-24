#include <linux.fs.h>

struct st7565 {
  struct file_operations fops;
}