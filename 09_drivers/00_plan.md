# Device Drivers: Track Plan

> **Track code:** DRV · **Weeks:** 15–20, interleaved with `08_kernel` · **Budget:** ~30 hours · **Prerequisites:** K 01 (your kernel in QEMU); CO 25–26 (buses and PCIe, interrupts and DMA); OS 01

A driver is where software meets hardware: it turns registers, interrupts and DMA into files, sockets and block devices. This track goes from your first loadable module to PCIe and network drivers, in the order a driver engineer learns them, with every topic built and loaded in QEMU. It is the track closest to the daily work of system-software and platform teams.

---

## 1. Why this track matters

Driver questions cover PCIe enumeration and BARs, MSI-X, DMA mapping and the IOMMU, `mmap` of device memory, interrupt handling and "DMA completed but I read stale data", plus platform drivers, device tree, I2C and SPI peripherals, network drivers, NAPI, interrupt coalescing, RSS and zero-copy paths. Your Wi-Fi platform background helps directly: mac80211 and cfg80211 drivers are network drivers, and DRV 10 connects your experience to the general model.
---

## 2. What you will be able to do after this track

1. Write, build and load a module, and manage its lifetime and reference counts correctly.
2. Explain the Linux device model (bus, device, driver, probe, remove) and how a device gets matched to a driver.
3. Write a character driver with `read`, `write`, `ioctl`, `poll` and `mmap`, and expose state through sysfs and debugfs.
4. Register an interrupt handler, split the work between top and bottom halves, and synchronise correctly with interrupt context.
5. Describe a board in a device tree and write a platform driver that binds to it; write an I2C client driver.
6. Use coherent and streaming DMA correctly, and explain the stale-data failure modes and the IOMMU.
7. Explain PCIe enumeration, configuration space, BARs and MSI-X, and the skeleton of a PCIe driver.
8. Explain a network driver end to end: `net_device`, TX and RX rings, NAPI, interrupt coalescing and `ethtool`.

---

## 3. Topics

Topics 01, 03, 04, 05 and 07 are older, Staff-level notes; each is rewritten in the same detailed style as the new tutorials before you study it.

| # | File | Topic | Key subtopics | Status | Week |
|---|---|---|---|---|---|
| 01 | [`01_kernel_modules.md`](01_kernel_modules.md) | Kernel modules and the build system | The kernel environment's rules, a minimal module, Kbuild, building against your own kernel, module parameters, memory allocation, reference counting, logging | Exists, rewrite due | 15 |
| 02 | `02_linux_device_model.md` | The Linux device model | Buses, devices and drivers, `probe` and `remove`, matching, kobjects and reference counting, classes, sysfs as a view of the model, uevents and udev, devres (`devm_*`) | To write | 16 |
| 03 | [`03_char_driver_and_model.md`](03_char_driver_and_model.md) | Character drivers | Registering a char device, `file_operations`, `ioctl` done properly, `poll`, `mmap`, register access with `ioread32`/`iowrite32`, runtime power management | Exists, rewrite due | 16 |
| 04 | [`04_sysfs_procfs_debugfs.md`](04_sysfs_procfs_debugfs.md) | sysfs, procfs, debugfs and tracepoints | Which interface for what, sysfs attributes, debugfs, defining tracepoints, observing a driver from outside with ftrace and bpftrace | Exists, rewrite due | 17 |
| 05 | [`05_interrupt_handling.md`](05_interrupt_handling.md) | Interrupt handling | The constraints of interrupt context, `request_irq` and threaded IRQs, top and bottom halves compared, interrupt storms and NAPI, synchronising with interrupt context, measuring | Exists, rewrite due | 17 |
| 06 | `06_platform_drivers_and_device_tree.md` | Platform drivers and the device tree | Why discoverable and non-discoverable buses differ, device-tree syntax and bindings, `compatible` matching, overlays, pinctrl and GPIO, clocks and regulators, a platform driver from DT to `probe` | To write | 18 |
| 07 | [`07_dma.md`](07_dma.md) | DMA | The three address spaces, coherent vs streaming mappings, the stale-data failure modes, DMA masks, the IOMMU, exposing a DMA ring to user space | Exists, rewrite due | 18 |
| 08 | `08_i2c_spi_uart_drivers.md` | I2C, SPI and UART drivers | How each bus works on the wire, the I2C and SPI subsystems (adapters vs clients), regmap, writing an I2C client driver for a sensor, the serial core and tty layer at a glance | To write | 19 |
| 09 | `09_pcie_drivers.md` | PCIe drivers | Enumeration and configuration space, BARs and `pci_iomap`, `pci_driver` and ID tables, MSI and MSI-X, bus mastering and DMA masks, AER, a skeleton PCIe driver against QEMU's `edu` device | To write | 19 |
| 10 | `10_network_drivers_and_napi.md` | Network drivers and NAPI | `net_device` and `net_device_ops`, TX and RX descriptor rings, NAPI polling and budgets, interrupt coalescing, multiqueue and RSS, `ethtool` ops and statistics, page-pool, how mac80211 and cfg80211 Wi-Fi drivers plug in | To write | 20 |

Diagram: [`diagrams/irq_dma_path.mmd`](diagrams/irq_dma_path.mmd). Related: NET 06, the Linux network stack (see [`../07_networking/00_plan.md`](../07_networking/00_plan.md)), and [`../07_networking/07_zero_copy_networking.md`](../07_networking/07_zero_copy_networking.md).

---

## 4. Labs

| Lab | Topics | What you build |
|---|---|---|
| `labs/hello_module/` | 01 | A module with parameters, loaded and unloaded 1,000 times under KASAN with zero leaks |
| `labs/ringbuf_chardev/` | 03, 04 | A character device backed by a kernel ring buffer with blocking `read`, `poll`, `ioctl`, sysfs counters and a tracepoint |
| `labs/fake_irq/` | 05 | An hrtimer standing in for a device interrupt, driving a threaded handler with a bounded budget |
| `labs/dt_platform/` | 06 | A device-tree overlay describing a virtual device and a platform driver that binds to it |
| `labs/i2c_sensor/` | 08 | An I2C client driver for a sensor emulated in QEMU, exposing readings through sysfs and IIO |
| `labs/pcie_edu/` | 07, 09 | A PCIe driver for QEMU's `edu` device: BAR access, MSI interrupt, and a DMA transfer |

Labs 03–07 are also the building blocks of Project 02, [`rkdma`](../14_projects/02_rkdma_dma_driver/SPEC.md).

---

## 5. Representative interview questions

1. What happens between `insmod` and your `init` function running? What can go wrong on unload?
2. How does the kernel match a device to a driver? What happens if the driver loads before the device appears?
3. What is the difference between a character, block and network driver?
4. Why do drivers use `devm_*` functions? What problem do they solve?
5. What can and cannot be done in an interrupt handler? How do you hand work to process context?
6. Coherent vs streaming DMA: when do you use each? A DMA transfer completes, but the CPU reads stale data. List the causes.
7. What does an IOMMU do, and what does it cost?
8. How does a PCIe device get its BAR addresses? What is MSI-X and why is it better than legacy interrupts?
9. What is NAPI, and why did Linux need it? What happens under a packet flood with and without it?
10. How would you expose a device's receive ring to user space without copies?
11. A device tree node has `compatible = "acme,sensor-v2", "acme,sensor"`. What does that order mean?
12. How do you debug a driver that hangs the system only under heavy load?

---

## 6. How to study this track

1. Follow the five-step method in [`../README.md`](../README.md).
2. Study alongside the kernel track in the interleaved order from [`../00_MASTER_PLAN.md`](../00_MASTER_PLAN.md): K 02 with DRV 01, K 03 with DRV 02–03, K 04–05 with DRV 04–05, K 06 with DRV 06–07.
3. Read one real driver per topic in the kernel source (suggestions in each tutorial), for example `drivers/misc/` for char drivers and `drivers/net/ethernet/intel/e1000e/` for NAPI.
4. Build everything against the kernel from K 01 and test in QEMU.

---

## 7. Resources

| Resource | Use it for |
|---|---|
| Bootlin "Linux kernel and driver development" training slides and labs (free) | The best current, practical driver course |
| kernel.org `Documentation/driver-api/` and `Documentation/devicetree/` | Primary sources for the device model, DMA, PCI and DT |
| *Linux Device Drivers* (3rd ed.), Corbet, Rubini & Kroah-Hartman (free on LWN) | Concepts are still right; many APIs are from the 2.6 era, so check against current source |
| *Linux Device Driver Development* (2nd ed.), John Madieu | Modern APIs: device tree, regmap, IIO, DMA engine |
| *Linux Kernel Programming* and *Part 2*, Kaiwan Billimoria | Current, lab-heavy treatment of modules and char drivers |
| QEMU `edu` device documentation (`docs/specs/edu.rst` in the QEMU source) | The virtual PCIe device used in the lab |

---

## 8. Progress

| # | Topic | Status |
|---|---|---|
| 01–10 | All topics | 01 is next (week 15); 01, 03, 04, 05, 07 are rewritten when reached |

Cross-track view: [`../00_MASTER_PLAN.md`](../00_MASTER_PLAN.md).
