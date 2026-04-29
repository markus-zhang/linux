## Intro

Q: Using LXR, find the definitions of the following symbols in the Linux kernel:

- `struct resource`
- `request_region()` and `__request_region()`
- `request_irq()` and `request_threaded_irq()`
- `inb()` for the x86 architecture.


A:

`resource` seems to be a tree-like structure. I don't know how it is being used and why it must be a tree-like structure.

```C
struct resource {
	resource_size_t start;
	resource_size_t end;
	const char *name;
	unsigned long flags;
	unsigned long desc;
	struct resource *parent, *sibling, *child;
};
```

```C
#define request_region(start,n,name)		__request_region(&ioport_resource, (start), (n), (name), 0)

// __request_region() is implemented in resource.c, so it has something to do with resources.
```

```C
static inline int __must_check
request_irq(unsigned int irq, irq_handler_t handler, unsigned long flags,
	    const char *name, void *dev)
{
	return request_threaded_irq(irq, handler, NULL, flags, name, dev);
}

// request_threaded_irq() is implemented in manage.c
```

```C
static inline unsigned type in##bwl(int port)				\
{									\
	unsigned type value;						\
	asm volatile("in" #bwl " %w1, %" #bw "0"			\
		     : "=a"(value) : "Nd"(port));			\
	return value;							\
}			
```

Q: Analyze i8042_setup_kbd(), the keyboard initialization routine.

```C
static int __init i8042_setup_kbd(void)
{
	int error;

	// i8042_create_kbd_port() seems to create a struct serio object, which is 
	error = i8042_create_kbd_port();
	if (error)
		return error;

	// I8042_KBD_IRQ is the IRQ number, i8042_interrupt() is the interrupt handler
	error = request_irq(I8042_KBD_IRQ, i8042_interrupt, IRQF_SHARED,
			    "i8042", i8042_platform_device);
	if (error)
		goto err_free_port;

	// Enable port (not sure what this is, but guess it is like the enable pin)
	error = i8042_enable_kbd_port();
	if (error)
		goto err_free_irq;

	i8042_kbd_irq_registered = true;
	return 0;

 err_free_irq:
	free_irq(I8042_KBD_IRQ, i8042_platform_device);
 err_free_port:
 	// Why don't we call free_irq() here too?
	i8042_free_kbd_port();
	return error;
}
```