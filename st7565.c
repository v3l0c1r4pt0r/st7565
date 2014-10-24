#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sysrq.h>
#include <linux/slab.h>
#include <linux/fs.h>

#include "st7565.h"

static struct st7565 st7565_data;

int init_module()
{
  handle_sysrq('g');	// st7565.ko+0x24
			//comment out if not debug
  static struct file_operations fops =
  {
    
  };
  st7565_data.fops = &fops;
//   st7565_data.major = register_chrdev(0, DEVICE_NAME, st7565_data.fops);
  printk(KERN_INFO "module loaded\n");
  return SUCCESS;
out:
  printk(KERN_INFO "module cannot be loaded\n");
  return -1;
}

void cleanup_module()
{
  printk(KERN_INFO "module unloaded\n");
}

MODULE_LICENSE("GPL");
