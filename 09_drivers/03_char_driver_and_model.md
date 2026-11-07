# DRV 03 — Character drivers and the Linux driver model

> Prerequisite: `01_kernel_modules.md`.
> This is the skeleton of Project 02 (`rkdma`).

## The two halves of a driver

Every Linux driver has two independent structures, and conflating them is the
most common beginner error:

1. **The device model side** — how the driver finds and binds to hardware.
   Bus-specific: `platform_driver`, `pci_driver`, `usb_driver`, `i2c_driver`.
2. **The userspace interface side** — how programs talk to it. A character
   device, a network device, a block device, sysfs attributes, or several.

A driver can bind to one device and expose three interfaces, or bind to twenty
devices and expose one each. Keep them separate in your head and the code
structure follows.

## Character device: registration

```c
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/device.h>

static dev_t          rk_devt;       /* major/minor */
static struct cdev    rk_cdev;
static struct class  *rk_class;

static const struct file_operations rk_fops = {
    .owner          = THIS_MODULE,     /* refcounts the module while open */
    .open           = rk_open,
    .release        = rk_release,
    .read           = rk_read,
    .write          = rk_write,
    .unlocked_ioctl = rk_ioctl,
    .mmap           = rk_mmap,
    .poll           = rk_poll,
    .llseek         = no_llseek,
};

static int __init rk_init(void)
{
    int ret;

    /* Dynamic major allocation — never hardcode a major number. */
    ret = alloc_chrdev_region(&rk_devt, 0, 1, "rkdma");
    if (ret)
        return ret;

    cdev_init(&rk_cdev, &rk_fops);
    rk_cdev.owner = THIS_MODULE;
    ret = cdev_add(&rk_cdev, rk_devt, 1);
    if (ret)
        goto err_cdev;

    /* Creating a class makes udev create /dev/rkdma0 automatically. */
    rk_class = class_create("rkdma");          /* older kernels: class_create(THIS_MODULE, "rkdma") */
    if (IS_ERR(rk_class)) {
        ret = PTR_ERR(rk_class);
        goto err_class;
    }

    if (IS_ERR(device_create(rk_class, NULL, rk_devt, NULL, "rkdma0"))) {
        ret = -ENODEV;
        goto err_dev;
    }
    return 0;

err_dev:
    class_destroy(rk_class);
err_class:
    cdev_del(&rk_cdev);
err_cdev:
    unregister_chrdev_region(rk_devt, 1);
    return ret;
}
```

**Note the error handling shape.** Kernel code uses `goto` ladders that unwind in
reverse order of acquisition. This is not bad style in C — it is *the* idiom, and
writing it any other way will be flagged in review. Every resource acquired has a
matching label, and the labels are named for what they undo.

## file_operations, the ones that matter

```c
static int rk_open(struct inode *inode, struct file *filp)
{
    /* Recover your device struct from the inode's cdev. */
    struct rk_dev *dev = container_of(inode->i_cdev, struct rk_dev, cdev);
    filp->private_data = dev;      /* every other op reads it from here */
    return 0;
}

static ssize_t rk_read(struct file *filp, char __user *buf,
                       size_t count, loff_t *ppos)
{
    struct rk_dev *dev = filp->private_data;
    ssize_t ret;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;            /* let signals interrupt cleanly */

    /* Blocking read with proper wait: condition re-checked under the lock. */
    while (ring_empty(dev)) {
        mutex_unlock(&dev->lock);
        if (filp->f_flags & O_NONBLOCK)
            return -EAGAIN;
        if (wait_event_interruptible(dev->readq, !ring_empty(dev)))
            return -ERESTARTSYS;
        if (mutex_lock_interruptible(&dev->lock))
            return -ERESTARTSYS;
    }

    count = min(count, ring_available(dev));
    if (copy_to_user(buf, ring_head_ptr(dev), count)) {
        ret = -EFAULT;
        goto out;
    }
    ring_consume(dev, count);
    ret = count;
out:
    mutex_unlock(&dev->lock);
    return ret;
}
```

The `wait_event_interruptible` loop is the canonical blocking pattern. Points a
reviewer checks: the condition is re-tested after waking (spurious wakeups are
real), `O_NONBLOCK` is honoured, and `-ERESTARTSYS` is returned on signal so the
syscall restarts or reports `EINTR` correctly.

## ioctl, done properly

```c
/* Define the ABI in a header shared with userspace. */
#define RK_IOC_MAGIC  'r'
#define RK_GET_STATS  _IOR(RK_IOC_MAGIC, 1, struct rk_stats)
#define RK_SET_DEPTH  _IOW(RK_IOC_MAGIC, 2, __u32)
#define RK_IOC_MAXNR  2

static long rk_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct rk_dev *dev = filp->private_data;
    struct rk_stats stats;
    __u32 depth;

    /* Validate the command encoding before anything else. */
    if (_IOC_TYPE(cmd) != RK_IOC_MAGIC) return -ENOTTY;
    if (_IOC_NR(cmd) > RK_IOC_MAXNR)    return -ENOTTY;

    switch (cmd) {
    case RK_GET_STATS:
        rk_snapshot_stats(dev, &stats);
        if (copy_to_user((void __user *)arg, &stats, sizeof(stats)))
            return -EFAULT;
        return 0;

    case RK_SET_DEPTH:
        if (copy_from_user(&depth, (void __user *)arg, sizeof(depth)))
            return -EFAULT;
        if (depth < 64 || depth > 8192 || !is_power_of_2(depth))
            return -EINVAL;             /* validate AFTER copying in */
        return rk_set_depth(dev, depth);
    }
    return -ENOTTY;
}
```

ABI rules worth stating, because they are what a senior reviewer looks for:

- Use the `_IO`/`_IOR`/`_IOW`/`_IOWR` macros; they encode direction and size, so
  a mismatched userspace fails cleanly instead of corrupting memory.
- **Fixed-width types only** (`__u32`, `__s64`) in structs crossing the boundary.
- **Pad explicitly** and check the padding is zero, so you can use it later.
- No pointers inside ioctl structs unless you handle 32-on-64 compat.
- Provide `.compat_ioctl` if 32-bit userspace must work.

## The device model: platform driver and devicetree

```c
#include <linux/platform_device.h>
#include <linux/of.h>

static const struct of_device_id rk_of_match[] = {
    { .compatible = "acme,rkdma-1.0" },
    { }
};
MODULE_DEVICE_TABLE(of, rk_of_match);      /* enables autoloading via udev */

static int rk_probe(struct platform_device *pdev)
{
    struct rk_dev *dev;
    struct resource *res;

    /* devm_* allocations are freed automatically when probe fails or the
     * device is removed. Use them for everything you can — they eliminate an
     * entire class of unwind bugs. */
    dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    dev->regs = devm_ioremap_resource(&pdev->dev, res);
    if (IS_ERR(dev->regs))
        return PTR_ERR(dev->regs);

    dev->irq = platform_get_irq(pdev, 0);
    if (dev->irq < 0)
        return dev->irq;

    /* Optional properties from devicetree. */
    if (of_property_read_u32(pdev->dev.of_node, "acme,ring-depth", &dev->depth))
        dev->depth = 256;                 /* documented default */

    platform_set_drvdata(pdev, dev);
    return rk_register_chardev(dev);
}

static void rk_remove(struct platform_device *pdev)
{
    struct rk_dev *dev = platform_get_drvdata(pdev);
    rk_unregister_chardev(dev);
    /* devm_ resources released automatically after this returns. */
}

static struct platform_driver rk_driver = {
    .probe  = rk_probe,
    .remove = rk_remove,
    .driver = {
        .name           = "rkdma",
        .of_match_table = rk_of_match,
        .pm             = &rk_pm_ops,
    },
};
module_platform_driver(rk_driver);
```

The matching devicetree node:

```dts
rkdma@ff200000 {
    compatible = "acme,rkdma-1.0";
    reg = <0x0 0xff200000 0x0 0x1000>;
    interrupts = <GIC_SPI 42 IRQ_TYPE_LEVEL_HIGH>;
    dma-coherent;
    acme,ring-depth = <1024>;
};
```

**`devm_*` is the closest thing the kernel has to RAII**, and using it
consistently is a marker of a modern driver. A probe function that returns an
error with `devm_` allocations outstanding leaks nothing.

## Register access

```c
/* Always use the accessors — never a raw volatile pointer dereference.
 * These include the barriers the architecture needs, which matters enormously
 * on ARM (see 04_computer_organization/31_arm_vs_x86_and_putting_it_together, section 6). */
u32 status = readl(dev->regs + RK_REG_STATUS);
writel(RK_CMD_START, dev->regs + RK_REG_CMD);

/* The _relaxed variants skip the barriers. Faster, and correct only when you
 * are providing ordering yourself. Used heavily in hot paths. */
u32 v = readl_relaxed(dev->regs + RK_REG_COUNT);

/* Read-back to flush a posted write when you must guarantee it landed: */
writel(val, dev->regs + RK_REG_CMD);
(void)readl(dev->regs + RK_REG_CMD);
```

The `readl` versus `readl_relaxed` distinction is a genuinely good interview
question for an embedded role, and the answer is about ordering against *other*
memory accesses, not about speed.

## Power management

```c
static int rk_suspend(struct device *d) { /* quiesce DMA, save state */ return 0; }
static int rk_resume(struct device *d)  { /* restore, restart */ return 0; }
static SIMPLE_DEV_PM_OPS(rk_pm_ops, rk_suspend, rk_resume);
```

Mentioning PM unprompted in an embedded interview is a differentiator — it is the
part people forget until a device fails to come back from suspend in the field.

## Interview angles

**"Walk me through writing a character driver."**
`alloc_chrdev_region`, `cdev_init`/`cdev_add`, `file_operations`, `class_create`
for udev, and the goto-ladder cleanup. Then mention `devm_`.

**"How do you pass a struct between userspace and a driver?"**
ioctl with `_IOR`/`_IOW` encoding, fixed-width types, explicit padding, validate
after copy. Mention compat for 32-on-64.

**"What is `container_of` for?"**
Recovering the enclosing struct from an embedded member. The kernel's answer to
inheritance.

**"How does the driver find its hardware?"**
Bus matching: devicetree `compatible` for platform devices, vendor/device IDs for
PCI, and `MODULE_DEVICE_TABLE` for autoloading.

**"readl vs readl_relaxed?"**
Barriers. `_relaxed` is correct only when you order things yourself.

## Exercises

1. Write a complete character driver with open/read/write/ioctl/poll backed by a
   kernel FIFO. Load it, and exercise every op from a userspace test program.
2. Add a blocking read with `wait_event_interruptible` and verify that Ctrl-C
   during a blocked read returns cleanly rather than hanging.
3. Convert every allocation in it to `devm_*` and delete the unwind path.
   Confirm with KASAN that nothing leaks across 100 bind/unbind cycles.
4. Write the devicetree overlay and bind the driver as a `platform_driver` in
   QEMU. Getting this working end to end is the real Project 02 (`rkdma`) milestone.
5. Implement `.compat_ioctl` and test with a 32-bit userspace binary. Most
   candidates have never done this; it is a good story.
