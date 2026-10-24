# DRV 01 — Kernel modules and the build system

> Prerequisite: `05_operating_systems/15_user_kernel_boundary.md`.
> Lab: `09_drivers/labs/`. This tutorial gets you to a loading `.ko`.

## The rules of the kernel environment

Before any code, internalise what is different in kernel space. Every one of
these has produced a real outage somewhere:

1. **No libc.** No `printf`, `malloc`, `memcpy` from userspace. Kernel
   equivalents only: `printk`/`pr_*`, `kmalloc`, `memcpy` (the kernel's own).
2. **No floating point** without explicit `kernel_fpu_begin/end`. The kernel does
   not save FP state on entry.
3. **Small stack.** 8 KiB or 16 KiB *total*, shared with interrupt handling on
   some configs. No large local arrays, no deep recursion. This is a real crash
   source.
4. **No exceptions, no RTTI, effectively no C++.** The kernel is C.
5. **A fault kills the machine**, or at least the subsystem. There is no
   supervisor to catch you.
6. **Preemption and concurrency are everywhere.** Your code can run on all cores
   at once, and be interrupted at almost any point.
7. **GPL matters legally.** Many symbols are `EXPORT_SYMBOL_GPL`. Your module's
   `MODULE_LICENSE` determines what you can link against.

## The minimal module

```c
// hello.c
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

static int __init hello_init(void)
{
    pr_info("hello: loaded\n");
    return 0;                 /* non-zero aborts the load */
}

static void __exit hello_exit(void)
{
    pr_info("hello: unloaded\n");
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Minimal module");
MODULE_VERSION("0.1");
```

`__init` puts the function in a section the kernel frees after boot/load;
`__exit` is dropped entirely if the kernel is built without module unloading.
Small details, but a reviewer notices when they are missing.

## Kbuild

```makefile
# Makefile
obj-m += hello.o

# Multi-file module: the object list, NOT the module name, gets -objs
# obj-m += rkdma.o
# rkdma-objs := rkdma_main.o rkdma_dma.o rkdma_sysfs.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

# Cross-compiling for an ARM target:
# make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- KDIR=/path/to/target/kernel
.PHONY: all clean
```

```bash
sudo insmod hello.ko
lsmod | grep hello
sudo rmmod hello
dmesg | tail
modinfo hello.ko          # licence, params, vermagic, dependencies
```

**vermagic** is the thing that bites newcomers: a module records the kernel
version, compiler and config options it was built against, and refuses to load
into a mismatched kernel. This is not bureaucracy — the kernel has no stable
internal ABI, so struct layouts genuinely differ between builds.

## Building a kernel you control

For Project 02 (`rkdma`) you want your own kernel in QEMU, so you can enable debug options
and crash freely:

```bash
git clone --depth 1 -b v6.12 \
  git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git
cd linux
make defconfig
make menuconfig     # enable the options below
make -j$(nproc)

# Boot it with a simple initramfs:
qemu-system-x86_64 -kernel arch/x86/boot/bzImage -initrd initramfs.cpio.gz \
    -append "console=ttyS0 nokaslr" -nographic -m 2G -s
```

Debug options worth enabling for driver development — these turn silent
corruption into loud, early failures:

```
CONFIG_DEBUG_KERNEL=y
CONFIG_DEBUG_INFO=y             # for gdb and pahole
CONFIG_KASAN=y                  # use-after-free, out-of-bounds
CONFIG_DEBUG_OBJECTS=y          # misuse of timers, work items
CONFIG_PROVE_LOCKING=y          # lockdep: deadlock detection before it deadlocks
CONFIG_DEBUG_ATOMIC_SLEEP=y     # "sleeping in atomic context" — catches the classic bug
CONFIG_DEBUG_KOBJECT=y
CONFIG_KGDB=y                   # attach gdb over the -s port
CONFIG_FUNCTION_TRACER=y        # ftrace
```

`CONFIG_PROVE_LOCKING` deserves emphasis. Lockdep builds a graph of lock
acquisition orders at runtime and reports a potential deadlock the *first time*
it sees an inconsistent ordering — even if that ordering never actually
deadlocks in your test. Mentioning lockdep by name in a kernel interview is a
strong signal.

## Module parameters

```c
static int queue_depth = 256;
module_param(queue_depth, int, 0644);          /* visible in /sys/module/.../parameters */
MODULE_PARM_DESC(queue_depth, "RX ring depth (entries)");

static char *mode = "polling";
module_param(mode, charp, 0444);
MODULE_PARM_DESC(mode, "Operating mode: polling | interrupt");
```

`insmod rkdma.ko queue_depth=1024 mode=interrupt`, and the 0644 ones are
writable through sysfs afterwards.

## Memory allocation

```c
/* kmalloc: physically contiguous, limited size (a few MB at most), fast.
 * Required for DMA. */
void *p = kmalloc(size, GFP_KERNEL);   /* may sleep — process context only */
void *q = kmalloc(size, GFP_ATOMIC);   /* never sleeps — interrupt context */
kfree(p);

/* kzalloc: same, zeroed. Prefer it — uninitialised kernel memory leaking to
 * userspace is a security bug class. */
void *z = kzalloc(size, GFP_KERNEL);

/* vmalloc: virtually contiguous only. Large allocations, but NOT usable for DMA
 * and slower to access (separate page tables). */
void *v = vmalloc(16 * 1024 * 1024);
vfree(v);

/* Page allocator, for whole pages: */
struct page *pg = alloc_pages(GFP_KERNEL, order);   /* 2^order pages */
__free_pages(pg, order);

/* Slab cache, for many same-sized objects. This is what you want for descriptors. */
struct kmem_cache *cache = kmem_cache_create("rkdma_desc", sizeof(struct desc),
                                             0, SLAB_HWCACHE_ALIGN, NULL);
struct desc *d = kmem_cache_alloc(cache, GFP_ATOMIC);
kmem_cache_free(cache, d);
kmem_cache_destroy(cache);
```

**The GFP flag rules matter more than the allocator choice.** `GFP_KERNEL` may
sleep, so it is illegal in interrupt context, while holding a spinlock, or in any
atomic section. `GFP_ATOMIC` never sleeps but draws from a smaller emergency pool
and fails more readily. `CONFIG_DEBUG_ATOMIC_SLEEP` catches the mistake; without
it you get a rare, load-dependent hang.

Note `SLAB_HWCACHE_ALIGN` above — the kernel's own answer to false sharing (`04_computer_organization/older_notes/07_cache_coherence_mesi.md`).

## Reference counting and lifetime

The kernel has no destructors, so lifetime is explicit refcounting:

```c
struct my_device {
    struct kref refcount;
    /* ... */
};

static void my_device_release(struct kref *kref)
{
    struct my_device *dev = container_of(kref, struct my_device, refcount);
    kfree(dev);
}

kref_get(&dev->refcount);
kref_put(&dev->refcount, my_device_release);
```

`container_of` is the kernel's central idiom: given a pointer to an embedded
member, recover the enclosing struct. It is how intrusive lists, kobjects and
callbacks all work, and you should be able to write it from memory.

## Logging and debugging

```c
pr_emerg / pr_alert / pr_crit / pr_err / pr_warn / pr_notice / pr_info / pr_debug

/* Prefix every message. Put this before the includes: */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

/* Device-aware variants — always prefer these in a driver, they identify
 * which device instance produced the message: */
dev_err(&pdev->dev, "dma map failed: %d\n", ret);
dev_dbg(&pdev->dev, "rx %u descriptors\n", n);

/* Rate limiting — essential on anything reachable from a data path: */
pr_warn_ratelimited("dropping frame, ring full\n");
```

`pr_debug` compiles out unless `CONFIG_DYNAMIC_DEBUG` is on, in which case you
can enable it per-file at runtime:

```bash
echo 'file rkdma_main.c +p' > /sys/kernel/debug/dynamic_debug/control
```

## Interview angles

**"What can't you do in kernel code?"**
Float, large stack allocations, sleeping in atomic context, libc. Then explain
*why* for at least one of them.

**"kmalloc vs vmalloc?"**
Physical contiguity, size limits, DMA suitability. The DMA point is the one that
matters for a driver role.

**"When is `GFP_ATOMIC` required?"**
Interrupt context or while holding a spinlock. Mention the smaller pool and
higher failure rate.

**"How do you debug a module?"**
`pr_debug` + dynamic debug, ftrace, KASAN, lockdep, KGDB over QEMU. Naming
lockdep and KASAN specifically separates you.

**"Why does a module fail to load with 'invalid module format'?"**
vermagic mismatch — no stable kernel ABI.

## Exercises

1. Build and boot a 6.x kernel in QEMU with the debug options above. This takes
   an afternoon the first time and pays for itself immediately.
2. Write the hello module, load it, and read `modinfo`. Then deliberately build
   it against a different kernel version and read the failure message.
3. Write a module that allocates a 4 MiB buffer with `kmalloc` and observe the
   failure. Do it with `vmalloc` and observe success. Explain the difference.
4. Deliberately call a `GFP_KERNEL` allocation while holding a spinlock with
   `CONFIG_DEBUG_ATOMIC_SLEEP` enabled. Read the splat carefully — learning to
   read kernel backtraces is a skill in itself.
5. Write a struct with an embedded `struct list_head` and iterate it with
   `list_for_each_entry`. Implement `container_of` yourself from the macro
   definition to be sure you understand it.
