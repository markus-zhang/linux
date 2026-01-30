#include "linux/mm_types.h"
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>

MODULE_DESCRIPTION("List current memory");
MODULE_AUTHOR("Kernel Hacker");
MODULE_LICENSE("GPL");

static int my_proc_init(void)
{
	struct task_struct *p = current;
  struct vm_area_struct *vma = p->mm->mmap;

  if(!vma)
    return -1;

  while(1)
  {
    printk("vm_start: 0x%lx -> vm_end: 0x%lx\n", vma->vm_start, vma->vm_end);
    vma = vma->vm_next;
    if (!vma)
      break;
  }

  return 0;
}

static void my_proc_exit(void)
{
	printk("Bye!\n");
}

module_init(my_proc_init);
module_exit(my_proc_exit);
