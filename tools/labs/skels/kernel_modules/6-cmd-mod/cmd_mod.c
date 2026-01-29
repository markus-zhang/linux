#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>

MODULE_DESCRIPTION("Command-line args module");
MODULE_AUTHOR("Kernel Hacker");
MODULE_LICENSE("GPL");

static char *str = "the worm";
int value = 16;

module_param(str, charp, 0000);
MODULE_PARM_DESC(str, "A simple string");

module_param(value, int, 0000);
MODULE_PARM_DESC(int, "A simple integer");

static int __init cmd_init(void)
{
	pr_info("Early bird gets %s\n", str);
	pr_info("The value is %d\n", value);
	return 0;
}

static void __exit cmd_exit(void)
{
	pr_info("Exit, stage left\n");
}

module_init(cmd_init);
module_exit(cmd_exit);
