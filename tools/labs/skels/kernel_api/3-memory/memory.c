/*
 * SO2 lab3 - task 3
 */

#include "asm/current.h"
#include "linux/gfp.h"
#include "linux/jiffies.h"
#include "linux/list.h"
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>

MODULE_DESCRIPTION("Memory processing");
MODULE_AUTHOR("SO2");
MODULE_LICENSE("GPL");

struct task_info {
	pid_t pid;
	unsigned long timestamp;
};

static struct task_info *ti1, *ti2, *ti3, *ti4;

static struct task_info *task_info_alloc(int pid)
{
	struct task_info *ti;

	/* TODO 1: allocated and initialize a task_info struct */
	ti = kmalloc(sizeof *ti, GFP_KERNEL);
	ti->pid = pid;
	ti->timestamp = get_jiffies_64();

	return ti;
}

static int memory_init(void)
{
	struct task_struct *task_next;
	struct task_struct *task_next_next;
	/* TODO 2: call task_info_alloc for current pid */
	ti1 = task_info_alloc(current->pid);

	/* TODO 2: call task_info_alloc for parent PID */
	ti2 = task_info_alloc(current->parent->pid);

	/* TODO 2: call task_info alloc for next process PID */
	// struct list_head *list_next = current->tasks.next;
	// struct task_struct *task_next = list_entry(list_next, struct task_struct, tasks);
	task_next = next_task(current);
	ti3 = task_info_alloc(task_next->pid);


	/* TODO 2: call task_info_alloc for next process of the next process */
	// struct list_head *list_next_next = task_next->tasks.next;
	// struct task_struct *task_next_next = list_entry(list_next_next, struct task_struct, tasks);
	task_next_next = next_task(task_next);
	ti4 = task_info_alloc(task_next_next->pid);

	return 0;
}

static void memory_exit(void)
{

	/* TODO 3: print ti* field values */
	printk("current pid-> %d, timestamp-> %lu\n", ti1->pid, ti1->timestamp);
	printk("parent pid-> %d, timestamp-> %lu\n", ti2->pid, ti2->timestamp);
	printk("next pid-> %d, timestamp-> %lu\n", ti3->pid, ti3->timestamp);
	printk("next next pid-> %d, timestamp-> %lu\n", ti4->pid, ti4->timestamp);

	/* TODO 4: free ti* structures */
	kfree(ti1);
	kfree(ti2);
	kfree(ti3);
	kfree(ti4);
}

module_init(memory_init);
module_exit(memory_exit);
