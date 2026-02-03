/*
 * Kernel API lab
 * 
 * mem.c - Memory allocation in Linux
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/ctype.h>

MODULE_DESCRIPTION("Print memory");
MODULE_AUTHOR("SO2");
MODULE_LICENSE("GPL");

static char *mem;

static int mem_init(void)
{
	size_t i;

	// Alocate 4096 bytes
	mem = kmalloc(4096 * sizeof(*mem), GFP_KERNEL);
	if (mem == NULL)
		goto err_mem;

	pr_info("chars: ");
	for (i = 0; i < 4096; i++) {
		// How come it prints 'Z' to the screen?
		// Because 0x5A is the default value of any memory allocated by kmalloc()
		if (isalpha(mem[i]))
			printk("%c ", mem[i]);
	}
	pr_info("\n");

	return 0;

err_mem:
	return -1;
}

static void mem_exit(void)
{
	kfree(mem);
}

module_init(mem_init);
module_exit(mem_exit);
