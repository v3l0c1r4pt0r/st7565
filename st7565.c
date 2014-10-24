#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sysrq.h>

int init_module()
{
  printk(KERN_INFO "module loaded\n");
  handle_sysrq('g');	// st7565.ko+0x24
  return 0;
}

void cleanup_module()
{
  printk(KERN_INFO "module unloaded\n");
}

MODULE_LICENSE("GPL");
