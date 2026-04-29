## LDD 3rd edition Chapter 9: Communicating with Hardware

### Using I/O ports

AFAIK from the book, there are two ways to perform I/O operations -- through port or through memory mapped regions.

https://en.wikipedia.org/wiki/Memory-mapped_I/O_and_port-mapped_I/O

From Wikipedia:

- Port-mapped I/O often uses a special class of CPU instructions designed specifically for performing I/O, such as the `in` and `out` instructions found on microprocessors based on the x86 architecture. In the book I read about similar instructions, such as `outb`, `outw`, etc. So basically there is an address for the Port, and programmers can use said instructions to read from / write into it.

- Memory-mapped I/O uses the same address space to address both main memory and I/O devices. The memory and registers of the I/O devices are mapped to (in Linux it is performed by `ioremap()` function) virtual addresses. This enables the programmers to use memory access instructions (e.g. `MOV`) to access the devices. To accommodate the I/O devices, some areas of the address bus used by the CPU must be reserved for I/O (e.g. ISA address space from 640KBytes to 1MBytes).

The book discusses Port-mapped I/O first.

#### I/O port allocation

I/O port programming is relatively simpler for bare metal programming, as far as I remember. I could simply bit-bang the ports and get something done, like, emulating a protocol. Under Linux, there are rituals to follow as the kernel manages everything.

Before the programmer talks to the I/O ports, he needs to call `request_region()` to tell the kernel that he would like to make use of `n` ports, starting with `start`. Comparing the macro in the 5.10.14 kernel against the book, which uses 2.6.x kernel, there is very little change in the delcaration, but I don't know whether the actual implementation changed.

```C
// Definition of theb macro
#define request_region(start,n,name)		__request_region(&ioport_resource, (start), (n), (name), 0)

// Heavy lifting
struct resource * __request_region(struct resource *parent,
				   resource_size_t start, resource_size_t n,
				   const char *name, int flags)
```

One thing the book mentions is that `requiest_region()` performs the required locking to ensure that the allocation is done in a safe, atomic manner. Checking the code, this might be the part:

```C
// resource.s
struct resource * __request_region(struct resource *parent,
				   resource_size_t start, resource_size_t n,
				   const char *name, int flags)
{
	DECLARE_WAITQUEUE(wait, current);
	struct resource *res = alloc_resource(GFP_KERNEL);
	struct resource *orig_parent = parent;

	if (!res)
		return NULL;

	res->name = name;
	res->start = start;
	res->end = start + n - 1;

	// I think this is the write lock? Not sure but this is the only line that manipulates a lock before the code goes into a loop.
	write_lock(&resource_lock);

	// Rest of the code
}
```

We can use `sudo cat /proc/ioports` to view the currently allocated I/O ports. We can use it to view which program claimed the port before we do, if `requiest_region()` returns `NULL`.

When we are done with a set of I/O ports, e.g. at module unload time, we should call `release_region()` to return them to the kernel.

```C
// There is a comment saying this is compatibility cruft, so I suspect there is a new function for this purpose.
#define release_region(start,n)	__release_region(&ioport_resource, (start), (n))

// Heavy lifting
void __release_region(struct resource *parent, resource_size_t start,
		      resource_size_t n)
```

#### I/O port manipulation

Assuming the program claims the port successfully, the rest of the operations is just read and write. I think this is where Wikipedia claims that "Port-mapped I/O uses `in`, `out` instructions to access the devices". The book introduces a bunch of such functions: `inb()`, `outb()`, `inw()`, `outw()`, etc. The kernel I'm using (5.10.14) seems to have these as macros. These macros look pretty cursed, so I'll skip them for now, but they are simply GCC inline assembly code.

```C
// io.h under x86 arch
#define BUILDIO(bwl, bw, type)						\
static inline void out##bwl(unsigned type value, int port)		\
{									\
	asm volatile("out" #bwl " %" #bw "0, %w1"			\
		     : : "a"(value), "Nd"(port));			\
}									\
									\
static inline unsigned type in##bwl(int port)				\
{									\
	unsigned type value;						\
	asm volatile("in" #bwl " %w1, %" #bw "0"			\
		     : "=a"(value) : "Nd"(port));			\
	return value;							\
}		
```

The book proceeds to claim that these functions can also be used from user space, and I can find them under `sys/io.h`. I found it as `/usr/include/sys/io.h` (Note that the 5.9.0 kernel doesn't have such a file, guess it is a barebone kernel). Here is one example:

```C
static __inline unsigned short int
inw (unsigned short int __port)
{
  unsigned short _v;

  __asm__ __volatile__ ("inw %w1,%0":"=a" (_v):"Nd" (__port));
  return _v;
}
```

There are also a bunch of limitations calling these functions from user space, which the book meticulously lists, but I'm not sure whether they are up-to-date:

- The user space program must be compiled with the `-O` option to force expansion of inline functions.

- The `ioperm` or `iopl` system calls must be used to get permission to perform I/O operations on ports. Both functions are x86-specific. I found the code in `ioport.c` defined with `SYSCALL_DEFINE3(ioperm, unsigned long, from, unsigned long, num, int, turn_on)`

- The user space program must run as root, or one of its ancestors must have gained port access running as root. The book continues to tell that if the platform does not have these system calls, it can still access I/O ports by using the `/dev/port` device file. Of course it still needs root access.

With such limitation, I'm not sure what are the usages of accessing ports from user space, other than making malwares.

The book then goes over some of the string operations, as some processors implement special instructions to transfer a sequence of bytes, words, or longs to and from a single I/O port or the same size. I have encountered some string instructions when studying the Linux 0.95 kernel, that uses `rep`, and these functions use it, too.

```C
// io.h under x86 arch
static inline void ins##bwl(int port, void *addr, unsigned long count)	\
{									\
	if (sev_key_active()) {						\
		unsigned type *value = (unsigned type *)addr;		\
		while (count) {						\
			*value = in##bwl(port);				\
			value++;					\
			count--;					\
		}							\
	} else {							\
		asm volatile("rep; ins" #bwl				\
			     : "+D"(addr), "+c"(count)			\
			     : "d"(port) : "memory");			\
	}								\
}
```

**Something to note** -- The book specifically warns, that if the port and the host system have different byte orders, programmers using the above string operations should be careful, because they simply move a byte stream to or from the port, and do not care about endianness.

### Using I/O Memory

The main mechanism used to communicate with devices is through memory-mapped registers and device memory. Both are called I/O memory because the difference between registers and memory is transparent to software. **Device registers implemented as memory mapped behave just like I/O ports, i.e. they have side effects associated with reading and writing them.**

Depending on the computer platform and bus being used, I/O memory may or may not be accessed through page tables. When access passes through page tables, the kernel must fist arrange for the physical address to be mapped to the virtual addresses, i.e. usually through the `ioremap()` function, before doing any I/O. If no page tables are needed, I/O memory locations look pretty much like I/O ports, and programmers can just read and write to them using proper wrapper functions. **Either way, it is discouraged to use pointer dereferencing to access I/O memory directly.**

#### I/O Memory Allocation and Mapping

I/O memory regions must be allocated prior to use. The function to use is `request_mem_region()`. In 5.10.14 the macro uses the same `__request_region()` function. Looks like it is a generalization of region-resource-request of some sort. **All I/O memory allocations are listed in /proc/iomem.**

```C
// macro
#define request_mem_region(start,n,name) __request_region(&iomem_resource, (start), (n), (name), 0)

// heavy lifting
struct resource * __request_region(struct resource *parent,
				   resource_size_t start, resource_size_t n,
				   const char *name, int flags)
```

When no longer needed, memory regions should be freed by calling `release_mem_region()`.

To ensure that this I/O memory has been made accessible to the kernel, the program should use `ioremap()` for the mapping. And of course use `iounmap()` if such mapping is no long required. The declaration is slightly different from the ones given in the book. From the declarations of the book, I can see that `offset` is a physical memory address.

```C
// io.h for x86 arch
void __iomem *ioremap(resource_size_t offset, unsigned long size);

extern void iounmap(volatile void __iomem *addr);
```

#### Accessing I/O Memory

Similar to accessing I/O ports, we have a bunch of `ioread` and `iowrite` functions. Here is one example. Note that `addr` is a `__iomem *` object, which confirms that the programmer is supposed to call `ioremap()` first, and then use the returned value to call `ioread8()` or `iowrite8()`. The programmer can also apply an integer offset on the returned value.

```C
// iomap.c
unsigned int ioread8(const void __iomem *addr)
{
	IO_COND(addr, return inb(port), return readb(addr));
	return 0xff;
}

void iowrite8(u8 val, void __iomem *addr)
{
	IO_COND(addr, outb(val,port), writeb(val, addr));
}
```

If the program must read or write a series of values to a given I/O memory address, the programmer can use the repeating versions of the functions. `ioread16_rep()` reads `count` of 16-byte values. from `dst`, into `addr`. **However, I'm not sure what does "reading" `count` of 16-byte values mean here. Does it move to the next 16-byte for each read, or keep reading the same address? I think it is the latter, because...see the next paragraph.**

```C
void ioread16_rep(const void __iomem *addr, void *dst, unsigned long count)
{
	IO_COND(addr, insw(port,dst,count), mmio_insw(addr, dst, count));
}
```

If the programmer wants to manipulate a block of memory, there are `memset/memcpy_io()` functions. I couldn't find the definition, but here is the declaration.

```C
// io.h
void memset_io(volatile void __iomem *, int, size_t);
```


### I/O Port example

Most of the time, I/O pins are controlled by two I/O locations: one that **selects** what pins are used as input and what pins are used as output, and one in which the programmer can actually read or write logic levels. Sometimes the pins are hardwired to perform input or output operations. The book uses the parallel port as an example. I'll paste the full program into this document and make as many comments as I can.

I break down the reading of the source code into a few stages:

- Stage 1: Read the initialization and cleanup functions.

- Stage 2: Read the function that reads from the port.

- Stage 3: Read the function that writes into the port.

All comments of the style of `//` are made by me, as the original author conveniently used `/*...*/` for comments.

#### Initialization

```C
int short_init(void)
{
	int result;

	/*
	 * first, sort out the base/short_base ambiguity: we'd better
	 * use short_base in the code, for clarity, but allow setting
	 * just "base" at load time. Same for "irq".
	 */
	short_base = base;
	short_irq = irq;

	/* Get our needed resources. */
	// We have two situations, using I/O ports, or use I/O memory.
	if (!use_mem) {
		// This is the request_region() function the book talked about above, for I/O ports. Programmers must allocate I/O ports prior to use.
		if (! request_region(short_base, SHORT_NR_PORTS, "short")) {
			printk(KERN_INFO "short: can't get I/O port address 0x%lx\n",
					short_base);
			return -ENODEV;
		}

	} else {
		// This is the request_mem_region() function the book talked about above, for I/O memory. Programmers must allocate I/O memory prior to use.
		if (! request_mem_region(short_base, SHORT_NR_PORTS, "short")) {
			printk(KERN_INFO "short: can't get I/O mem address 0x%lx\n",
					short_base);
			return -ENODEV;
		}

		/* also, ioremap it */
		// Since the archtecture the book uses is x86, the driver accesses physical addresses through the page table and the TLB. It is mandatory to map physical addresses to virtual addresses prior to use. And if it fails, we need to release the memory region allocated as it is useless now.
		short_base = (unsigned long) ioremap(short_base, SHORT_NR_PORTS);
		if (!short_base) {
			release_mem_region(short_base, SHORT_NR_PORTS);
			printk(KERN_INFO "short: can't remap I/O mem address 0x%lx\n",
					short_base);
			return -ENOMEM;
		}
	}
	/* Here we register our device - should not fail thereafter */
	// This is different from what the lab uses in so2_cddev. In so2_cddev it calls register_chrdev_region(). Maybe both are OK? Maybe one of the is better for more recent versions of the kernel?
	// OK looks like every question in the world has been asked at least once: https://stackoverflow.com/questions/71835899/whats-the-difference-between-register-chrdev-and-register-chrdev-region
	// So the answer is -- register_chrdev() is old. It existed before `struct cdev` was introduced in 2.5.70. register_chrdev_region() is more recent.
	result = register_chrdev(major, "short", &short_fops);
	if (result < 0) {
		printk(KERN_INFO "short: can't get major number\n");
		if (!use_mem) {
			release_region(short_base, SHORT_NR_PORTS);
		} else {
			release_mem_region(short_base, SHORT_NR_PORTS);
		}
		return result;
	}
	// I have no idea what this is.
	if (major == 0) major = result; /* dynamic */

	short_buffer = __get_free_pages(GFP_KERNEL,0); /* never fails */  /* FIXME */
	short_head = short_tail = short_buffer;

	/*
	 * Fill the workqueue structure, used for the bottom half handler.
	 * The cast is there to prevent warnings about the type of the
	 * (unused) argument.
	 */
	/* this line is in short_init() */
	// In the lab, `init_waitqueue_head(&devs[i].wq);` is used. Both are char device drivers. What is the difference between workqueue and waitqueue? The lab doesn't talke about tasklet, either.
	// For one thing, looks like tasklet has lost the favor in recent years. 5.9.10 came out on 2020-11-22 so I highly doubt that's why the lab skips it.
	// See this article for more info: https://lwn.net/Articles/960041/
	//
	// The book "Understand the Linux Kernel" talks a bit about both wait queues and work queues. But it doesn't compare them, so I suspect they are kinda unrelated (i.e. incomparable)?
	// One important piece of information: work queues run in process context -- so cannot use in interrupt context.
	//
	// OK I asked ChatGPT and it mentioned one important point: wait queues are for a thread to sleep until a condition has been met. I think this is the key difference. Work queues are deferred, but there is no condition to wait for. Work queue only works in process context, not in interrupt context.
	//
	// **This initialization of a work item gives interrupt handler an opportunity to schedule work in the work queue. See short_wq_interrupt() for such an example.**
	// short_do_tasklet() is the function to be queued/scheduled. It is also part of the code. Check it out. This is the callback function.
	// 
	// ChatGPT gives a good mental model:
	// INIT_WORK(&x, fn) = “prepare job object x; its job is to run fn later”
	// schedule_work(&x) = “put job x in the queue”
	// worker thread = “later, someone in kernel process context runs fn for job x”
	INIT_WORK(&short_wq, (void (*)(struct work_struct *)) short_do_tasklet);

	/*
	 * Now we deal with the interrupt: either kernel-based
	 * autodetection, DIY detection or default number
	 */

	if (short_irq < 0 && probe == 1)
		short_kernelprobe();

	if (short_irq < 0 && probe == 2)
		short_selfprobe();

	if (short_irq < 0) /* not yet specified: force the default on */
		switch(short_base) {
		    case 0x378: short_irq = 7; break;
		    case 0x278: short_irq = 2; break;
		    case 0x3bc: short_irq = 5; break;
		}

	/*
	 * If shared has been specified, installed the shared handler
	 * instead of the normal one. Do it first, before a -EBUSY will
	 * force short_irq to -1.
	 */
	if (short_irq >= 0 && share > 0) {
		// Request to obtain an interrupt. request_irq() returns in interrupt context.
		// short_sh_interrupt seems to be the said "shared handler". It is a function defined in this driver.
		// IRQF_SHARED is defined in interrupt.h
		// #define IRQF_SHARED		0x00000080
		result = request_irq(short_irq, short_sh_interrupt,
				     IRQF_SHARED,"short",
				short_sh_interrupt);
		// request_irq() returns non-zero values if something is wrong.
		if (result) {
			printk(KERN_INFO "short: can't get assigned irq %i\n", short_irq);
			short_irq = -1;
		}
		// request_irq() returns successfully.
		else { /* actually enable it -- assume this *is* a parallel port */
			// short_base+2 is the control port. Bit 4 is irq_enable, which is enabled by pusing 0x10.
			outb(0x10, short_base+2);
		}
		return 0; /* the rest of the function only installs handlers */
	}

	if (short_irq >= 0) {
		result = request_irq(short_irq, short_interrupt,
				     0, "short", NULL);
		if (result) {
			printk(KERN_INFO "short: can't get assigned irq %i\n",
					short_irq);
			short_irq = -1;
		}
		else { /* actually enable it -- assume this *is* a parallel port */
			outb(0x10,short_base+2);
		}
	}

	/*
	 * Ok, now change the interrupt handler if using top/bottom halves
	 * has been requested
	 */
	// wq and tasklet can be set by user using command line argument. They are both module_params.
	// What this part does is to "uninstall" the previously installed handler (short_irq), and install a second one. If tasklet > 0 then use a special handler, otherwise use the workqueue one.
	if (short_irq >= 0 && (wq + tasklet) > 0) {
		free_irq(short_irq,NULL);
		result = request_irq(short_irq,
				tasklet ? short_tl_interrupt :
				short_wq_interrupt,
				0, "short-bh", NULL);
		if (result) {
			printk(KERN_INFO "short-bh: can't get assigned irq %i\n",
					short_irq);
			short_irq = -1;
		}
	}

	return 0;
}
```

#### VFS and open operation

I vaguely recall that read/write/open ports are similar to files. The first entry point in the kernel is probably the read/write/open system call, then it goes into VFS (because everything in Linux is a file), then it found out it is a port, so it goes into the specific open/write/read functions for that specific device. Maybe I missed a few hops here, but that should be the general idea.

Here is the filesystem part of the code. I call them "hook functions". They "hook" into VFS and are the heavy-lifting functions when the kernel ask for them. Only 5 functions are implemented.

```C
struct file_operations short_fops = {
	.owner	 = THIS_MODULE,
	.read	 = short_read,
	.write	 = short_write,
	.poll	 = short_poll,
	.open	 = short_open,
	.release = short_release,
};
```

Let's go over them one by one, starting from `short_open()` as it is the shortest (punt?) one. Opening a port is completely different from opening a file. IIRC, opening a file is a lot of hassle -- the program needs to find the cwd, find the directory entry, find the inode, allocate a `struct file` and a `fp`. The port is already "opened" in the sense that during initialization the work has been done. Here the `short_open()` merely checks minor number and points `filp->f_op` to another set of file operation functions if minor is 128.

```C
/*
 * The devices with low minor numbers write/read burst of data to/from
 * specific I/O ports (by default the parallel ones).
 * 
 * The device with 128 as minor number returns ascii strings telling
 * when interrupts have been received. Writing to the device toggles
 * 00/FF on the parallel data lines. If there is a loopback wire, this
 * generates interrupts.  
 */
int short_open (struct inode *inode, struct file *filp)
{
	extern struct file_operations short_i_fops;

	// ChatGPT: low minor numbers: direct port read/write behavior. minor 7th bit set: interrupt-driven behavior, returning ASCII timestamps, etc.
	if (iminor (inode) & 0x80)
		filp->f_op = &short_i_fops; /* the interrupt-driven node */
	return 0;
}
```

The following list shows `short_i_fops` which is another `struct file_operations`. For now I'll skip these because I don't know much about interrupts yet.

```C
struct file_operations short_i_fops = {
	.owner	 = THIS_MODULE,
	.read	 = short_i_read,
	.write	 = short_i_write,
	.open	 = short_open,
	.release = short_release,
};
```

#### read operation (non-interrupt version)

Non-interrupt read is the same as reading a GPIO port on an embedded device. Without an operating system, it is very trivial -- the programmer finds the bit of the register that represents the port, and use bit operations to read that bit, and that's it. With Linux in the middle, there is a fair amount of code. `short_read` is a wrapper function for `do_short_read()`.

```C
/*
 * Version-specific methods for the fops structure.  FIXME don't need anymore.
 */
ssize_t short_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	return do_short_read(file_dentry(filp)->d_inode, filp, buf, count, f_pos);
}


// The real heavy lifting happens here.

enum short_modes {SHORT_DEFAULT=0, SHORT_PAUSE, SHORT_STRING, SHORT_MEMORY};

// First, filp is not used in this function, so I'm not sure why it is here.
// Neither is f_pos used. Oh well...
// Judging by the copy_to_user(buf, kbuf, retval) call at the end, with the definition of copy_to_user(void __user *to, const void *from, unsigned long n),
// do_short_read() copies "count" of bytes, from kbuf to buf -- judging by the names, kbuf is kernel space buffer and buf is user space buffer?
ssize_t do_short_read (struct inode *inode, struct file *filp, char __user *buf,
		size_t count, loff_t *f_pos)
{
	int retval = count, minor = iminor (inode);
	// I think minor&0x0f will be 1, 2 or 3?
	unsigned long port = short_base + (minor&0x0f);
	void *address = (void *) short_base + (minor&0x0f);
	// 0x70 is 0111'0000, not sure what's the point of taking the lower 3 bits of the top byte, and then shift right 4 bits.
	// So for example, if minor has a higher byte of 0x40, we get 0x04.
	// Considering short_modes only has 0, 1, 2 and 3, the higher byte is either 0, 1, 2 or 3. What does it mean?
	int mode = (minor&0x70) >> 4;
	// Allocate the kernel buffer. Both ptr and kbuf are unsigned char *.
	unsigned char *kbuf = kmalloc(count, GFP_KERNEL), *ptr;
    
	if (!kbuf)
		return -ENOMEM;
	ptr = kbuf;

	// This is the I/O Memory mode. I think users can set this variable using the command line when loading the module.
	if (use_mem)
		mode = SHORT_MEMORY;
	
	switch(mode) {
		// String operations (check book page 241)
	    case SHORT_STRING:
		insb(port, ptr, count);
		rmb();
		break;

		// Read a byte from the port, and write into ptr. Note the usage of read memory barrier.
	    case SHORT_DEFAULT:
		while (count--) {
			*(ptr++) = inb(port);
			rmb();
		}
		break;

		// I/O Memory?
	    case SHORT_MEMORY:
		while (count--) {
			*ptr++ = ioread8(address);
			rmb();
		}
		break;

		// Pausing I/O, Some platforms can have problems when the processor tries to transfer data too quickly.
	    case SHORT_PAUSE:
		while (count--) {
			*(ptr++) = inb_p(port);
			rmb();
		}
		break;

	    default: /* no more modes defined by now */
		retval = -EINVAL;
		break;
	}

	// All done, if there is no error, copy to user space buffer "buf".
	if ((retval > 0) && copy_to_user(buf, kbuf, retval))
		retval = -EFAULT;

	// Don't forget to free the kernel buffer. We don't need this middle man anymore.
	kfree(kbuf);
	return retval;
}
```


#### write and release operation (non-interrupt version)

Similar to the read operation, `short_write()` is a wrapper function for `do_short_write()`. This function looks pretty much the same as `do_short_read()`. Instead of `in` functions, there are `out` functions. And instead of read memory barriers, there are write memory barriers.

```C
ssize_t short_write(struct file *filp, const char __user *buf, size_t count,
		loff_t *f_pos)
{
	return do_short_write(file_dentry(filp)->d_inode, filp, buf, count, f_pos);
}

ssize_t do_short_write (struct inode *inode, struct file *filp, const char __user *buf,
		size_t count, loff_t *f_pos)
{
	int retval = count, minor = iminor(inode);
	unsigned long port = short_base + (minor&0x0f);
	void *address = (void *) short_base + (minor&0x0f);
	int mode = (minor&0x70) >> 4;
	unsigned char *kbuf = kmalloc(count, GFP_KERNEL), *ptr;

	if (!kbuf)
		return -ENOMEM;
	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	ptr = kbuf;

	if (use_mem)
		mode = SHORT_MEMORY;

	switch(mode) {
	case SHORT_PAUSE:
		while (count--) {
			outb_p(*(ptr++), port);
			wmb();
		}
		break;

	case SHORT_STRING:
		outsb(port, ptr, count);
		wmb();
		break;

	case SHORT_DEFAULT:
		while (count--) {
			outb(*(ptr++), port);
			wmb();
		}
		break;

	case SHORT_MEMORY:
		while (count--) {
			iowrite8(*ptr++, address);
			wmb();
		}
		break;

	default: /* no more modes defined by now */
		retval = -EINVAL;
		break;
	}
	kfree(kbuf);
	return retval;
}
```

`short_release()` is a dummy function.

```C
int short_release (struct inode *inode, struct file *filp)
{
	return 0;
}
```

#### poll operation (non-interrupt version)

I have no idea what it does. I vaguely understand what poll means, but I don't know what it means for the parallel ports. All of these POLL* stuffs are defined in `poll.h`. The book says, "The poll method should return a bit mask indicating whether non-blocking reads or writes are possible, and, possibly, provide the kernel with information that can be used to put the calling process to sleep until I/O becomes possible. If a driver leaves its poll menthod NULL, the device is assumed to be both readable and writable without blocking". OK, eh, that's what it means.

```C
unsigned int short_poll(struct file *filp, poll_table *wait)
{
	return POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM;
}
```

### Race condition and Locking in Linux kernel

#### Introduction to interleaving

To understand interrupts, I need to read the chapter on Concurrency first. I think the most important things to use mutual exclusion is to understand what are "critical sections", what can access these critical sections concurrenctly, and which locking mechanism to use under which context. Race condition and locking themselves are not particularly hard to understand. It is *when* race conditions appear, and *which* locking mechanism to pick, the more tricky ones.

A critical section if a part of the code that ideally should be executed from beginning to end in one shot, without disruption. For example, if the code reads a global variable, and then increments it (write), then this read-write combo *could* be a critical section -- I said *could* because I do not know the platform and how the code is written. If it is MS-DOS 3.3 in 8086/80286, then everything runs in a single task (but with TSR that provides the illusion of multitasking). There is no premption from the kernel. There is no true multitasking. However, it is still possible for multiple code paths to interleave with each other, especially for TSR programs. I shall quote:

> Luckily, MS-DOS provides a solution to this problem as well – the idle interrupt. While MS-DOS is in an indefinite loop wait for an I/O device, it continually executes an int 28h instruction. By patching into the int 28h vector, your TSR can determine when DOS is sitting in such a loop. When DOS executes the int 28h instruction, it is safe to make any DOS call whose function number (the value in ah) is greater than 0Ch.

> So if DOS is busy when your TSR wants to make a DOS call, you must use either a timer interrupt or the idle interrupt (int 28h) to activate the portion of your TSR that must make DOS calls. One final thing to keep in mind is that whenever you test or modify any of the above mentioned flags, you are in a critical section. Make sure the interrupts are off. If not, your TSR make activate two copies of itself or you may wind up entering DOS at the same time some other TSR enters DOS.
	
In Linux, there are many ways for interleaving to happen that cause race conditions. The kernel preempts. Multi-core systems can literally run the same program at the same time. Even for single-core systems, there is the problem of interrupts and concurrency (programs run in different time slices, interleaving with each other, and access/modify a common resource such as a global variable).


#### Race condition and mutual exclusion

Now that we roughly understand what can cause interleaving, the next topic is to understand which mutual exclusion to use under which context. Programs can run in one of two contexts: interrupt context, or process context. **I have consulted both Linux books and various webpages, and I'm still not sure about anything that I write down below. I think the only solution is to actually understand how each mechanism is implemented in the kernel, and understand how the kernel deals with interrupt/process context -- only after that I can understand which mechanism is more suitable for which context.**

**In interrupt context**:

When an interrupt occurs, the processer saves the registers onto the stack, and jump to the ISR. This means that when ISR runs, it is running in the **same context** of the process that was executing when the interrupt occurred. ISR doesn't have its own stack -- it is executing on that process' stack, and when ISR concludes that process simply resumes executing (without any context switch).

This means, if the ISR sleeps, the whole process sleeps, including the whatever process that was executing when the ISR executes. Technically, this is allowed -- but it is the sign of bad design. This also means that whatever mutual exclusion we want to use in an ISR, it should not introduce wait/sleep. Since a **semaphore** introduce wait, it should not be used in an ISR. A **spinlock** spin-waits to obtain the lock. **In a uniprocessor system**, both semaphores and spin locks are not ideal candidates, because they both have the potential to block the whole system for an undetermined amount of time. So the only solution is to disable interrupts. **In a multiprocessor system**, disabling local interrupts are not good enough, because other cores can still call ISRs. However, we can add a spinlock to make it work better -- in a multiprocessor system, spinning is not a big issue, because while spinning blocks one CPU, ISRs from other CPUs will eventually release the lock. (**To be honest, I'm still not very clear about this part -- what if the spin lock is obtained by programs in the same CPU? It looks like the book is only discussing locks obtained by ISRs -- in this case it makes sense, because ISRs are guaranteed to be serialized locally.**)

#### Spin lock

**Spin lock issues**:

I have to keep my notes scattered because I consume material from multiple books. In LDD, several scenarios that involve spin locks are described:

- **Scenario 1**: The device driver acquires a spin lock (which is legitimate), and goes on to do its stuffs. In the middle it is preempted by the kernel, or it makes a call to a kernel function. Either way, it is paused while holding the lock. Now if some other thread needs to acquire the lock, it has to spin-wait. This is very bad for performance, and can potentially cause deadlock -- let's say the called kernel function needs to acquire the same lock, finish its job, and returns to the device driver. This leads to the conclusion that if the driver acquires a spin lock, it **MUST EXECUTE** the critical section atomically -- i.e. in one shot, without giving up the processor. There are spin lock functions that disable kernel preemptions for the programmer.

- **Scenario 2**: The device driver acquires a spin lock, calls a kernel function. The kernel function actually can sleep, and it sleeps. This is the same issue as above -- best case it impacts performance and worst case the system stucks. The conclusion is that device writer needs to look at each function call in the critical section closely and make sure they don't sleep. Some kernel functions are not very well documented so it is best to scan the source code.

- **Scenario 3**: The device driver acquires a spin lock, and then an interrupt preempts. The interrupt spins for the same lock, and the deadlock freezes the process. The conclusion is that the driver should disable interrupt when going into a critical section. This is even true for uniprocessor system because interrupts still occur in them. Some spin lock functions disable interrupts for the programmer.

*I have decided to read the chapter of Interrupt Handling at the moment. I'll probably come back to this chapter (locking) from time to time.*


### Interrupt Handling

`short.c` includes some code to deal with interrupt, so we will take a look there first, before we go into `shortprint.c`, the version that actually drives a printer through the pport. The first piece of code is to probe for IRQ line number. I dug in the concept a bit and found out that IRQ line number has nothing to do with the interrupt vector number. In Intel 8259 family of programmable interrupt controllers, there are 8 interrupt inputs/lines -- IRQ0 to IRQ7. The programmer needs to figure out which line the device is going to use in order to install the handler correctly. The program demonstrates two ways: 1) kernel-assisted probing, and 2) self-probing.

Once probing is done and the program knows which IRQ line the device is using, the program demonstrates how to write an interrupt handler. Recall that there are things that an interrupt handler are not supposed to do -- for example, it should not wait/sleep; it can't transfer data to user space because it doesn't execute in the process context (**TODO: Figure out why it can't transfer data to user space**); it can't call `schedule()`.

#### Probing for IRQ line

First function to research is the kernel-assisted version. Basically the program uses `probe_irq_on()` and `probe_irq_off()` (note how it passes `mask` to `probe_irq_off()` at the end). Between the pair it tries to trigger an interrupt. It tries 5 times to obtain a clean IRQ number, because sometimes multiple interrupts occur. `probe_irq_off()` is the key here: it returns **the number of the interrupt that was issued after `probe_irq_on()`**. Now that interrupts occur all the time, so it returns a negative number if the line detected changes.

```C
void short_kernelprobe(void)
{
	int count = 0;
	do {
		unsigned long mask;

		mask = probe_irq_on();
		outb_p(0x10,short_base+2); /* enable reporting */
		outb_p(0x00,short_base);   /* clear the bit */
		outb_p(0xFF,short_base);   /* set the bit: interrupt! */
		outb_p(0x00,short_base+2); /* disable reporting */
		udelay(5);  /* give it some time */
		short_irq = probe_irq_off(mask);

		if (short_irq == 0) { /* none of them? */
			printk(KERN_INFO "short: no irq reported by probe\n");
			short_irq = -1;
		}
		/*
		 * if more than one line has been activated, the result is
		 * negative. We should service the interrupt (no need for lpt port)
		 * and loop over again. Loop at most five times, then give up
		 */
	} while (short_irq < 0 && count++ < 5);
	if (short_irq < 0)
		printk("short: probe failed %i times, giving up\n", count);
}
```

Secone funtion is the self-probing version. Basically the programmer implements a "probing handler" and passes it to `request_irq()`. I copied and pasted my comments in the code here.

```C
/*
 * Markus note: This is the probing handler that is passed to request_irq() in short_selfprobe().
 * We use it to update short_irq once an interrupt has been triggered.
 * Note that short_irq is negative if it gets different interrupts during the 5 tests.
 */

irqreturn_t short_probing(int irq, void *dev_id)
{
	if (short_irq == 0) short_irq = irq;	/* found */
	if (short_irq != irq) short_irq = -irq; /* ambiguous */
	return IRQ_HANDLED;
}

void short_selfprobe(void)
{
	// 3, 5, 7, 9 are the values some parallel devices allow the device programmer to select. 0 serves as a stop-gap (see below)
	int trials[] = {3, 5, 7, 9, 0};
	int tried[]  = {0, 0, 0, 0, 0};
	int i, count = 0;

	/*
	 * install the probing handler for all possible lines. Remember
	 * the result (0 for success, or -EBUSY) in order to only free
	 * what has been acquired
      */
	for (i = 0; trials[i]; i++)
		/*
	     * Markus note: short_probing() is the probing handler that updates short_irq.
	     * Question: If short_irq is updated by the handler, why does the do...while loop put it to 0 for each loop?
	     * I guess the answer is that triggering the interrupt somehow calls short_probing().
	     * Actually read the docstring of request_irq() -- it confirms the above.
	    */
		tried[i] = request_irq(trials[i], short_probing,
				       0, "short probe", NULL);

	do {
		short_irq = 0; /* none got, yet */
		outb_p(0x10,short_base+2); /* enable */
		outb_p(0x00,short_base);
		outb_p(0xFF,short_base); /* toggle the bit */
		outb_p(0x00,short_base+2); /* disable */
		udelay(5);  /* give it some time */

		/* the value has been set by the handler */
		if (short_irq == 0) { /* none of them? */
			printk(KERN_INFO "short: no irq reported by probe\n");
		}
		/*
		 * If more than one line has been activated, the result is
		 * negative. We should service the interrupt (but the lpt port
		 * doesn't need it) and loop over again. Do it at most 5 times
		 */
	} while (short_irq <=0 && count++ < 5);

	/* end of loop, uninstall the handler */
	// Since trials[4] is always 0, it stops the loop.
	for (i = 0; trials[i]; i++)
		if (tried[i] == 0)
			free_irq(trials[i], NULL);

	if (short_irq < 0)
		printk("short: probe failed %i times, giving up\n", count);
}

```

#### Implementing the interrupt handler

I quote from the book:

> The role of the interrupt handler is to give feedback to its device about interrupt reception and to read or write data according to the meaning of the interrupt being serviced. A typical task for an interrupt handler is awakening processes sleeping on the device if the interrupt signals the event they are waiting for, such as the arrival of new data.

> The interrupt handler should execute in minimum amount of time. If a long computation needs to be performed, the best approach is to use a tasklet or workqueue to schedule computation at a safer time.

```C
/*
 * Atomicly increment an index into short_buffer
 */
static inline void short_incr_bp(volatile unsigned long *index, int delta)
{
	unsigned long new = *index + delta;
	// **TODO**: Figure out why we don't need a lock in the code. The book says barrier() is lockless, which for sure is true, but why don't we need a lock for any of the code here?
	barrier();  /* Don't optimize these two together */

	// Wrap around: short_buffer is the fixed head of the buffer, so we need to check whether we go over the PAGE_SIZE boundary.
	// short_buffer is initialized in short_init(), I'll quote the code:
	// short_buffer = __get_free_pages(GFP_KERNEL,0); /* never fails */  /* FIXME */
	// short_head = short_tail = short_buffer;

	// This updates short_head (check the code in short_interrupt)
	*index = (new >= (short_buffer + PAGE_SIZE)) ? short_buffer : new;
}


irqreturn_t short_interrupt(int irq, void *dev_id)
{
	struct timespec64 tv;
	int written;

	// Get timestamp of some sort.
	ktime_get_real_ts64(&tv);

	    /* Write a 16 byte record. Assume PAGE_SIZE is a multiple of 16 */
	// short_head is the head of the circular buffer short_queue
	written = sprintf((char *)short_head,"%08u.%06lu\n",
			(int)(tv.tv_sec % 100000000), (int)(tv.tv_nsec) /  NSEC_PER_USEC);
	BUG_ON(written != 16);
	short_incr_bp(&short_head, written);
	// wake up processes because new data comes into the circular buffer
	wake_up_interruptible(&short_queue); /* awake any reading process */
	return IRQ_HANDLED;
}
```