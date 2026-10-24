# Embedded Systems and Edge AI: Track Plan

> **Track code:** EMB · **Weeks:** 23 for topic 01; everything else is stretch · **Budget:** ~4 hours core, ~25 hours with stretch · **Prerequisites:** OS 04–06 (scheduling, synchronisation); DRV 05–08 for the stretch topics

This is a small, targeted track. Topic 01 (RTOS design constraints) is core because real-time scheduling and priority inversion come up in network-equipment, platform and embedded-adjacent loops, and because it sharpens your device-platform stories. Topics 02 and 03 support edge-AI roles and Staff-level hardware/software discussions. Topics 04–08 build embedded firmware from zero and are worth doing only if you target firmware roles.

---

## 1. Why this track matters

| Audience | What they ask |
|---|---|
| **Network-equipment and platform teams** | Real-time constraints on network devices, priority inversion, interrupt latency, why a watchdog fired. |
| **Edge AI (Jetson-class boards, automotive)** | Deploying models on constrained devices: quantisation, TensorRT, zero-copy camera pipelines, power budgets. |
| **Staff-level loops everywhere** | Hardware/software co-design: "should this be in silicon, firmware or software?", argued with numbers. |

---

## 2. Topics

| # | File | Topic | Key subtopics | Status | Week |
|---|---|---|---|---|---|
| 01 | [`01_rtos_design_constraints.md`](01_rtos_design_constraints.md) | RTOS design constraints | Predictability over speed, priority inversion and its protocols, worst-case execution time, schedulability (rate-monotonic, EDF), static memory, interrupt latency, RTOS vs Linux with PREEMPT_RT | Exists, rewrite due | 23 |
| 02 | [`02_edge_ai_acceleration.md`](02_edge_ai_acceleration.md) | Edge AI acceleration | Quantisation, graph optimisations, ONNX Runtime execution providers, TensorRT build vs runtime, zero-copy input pipelines, measuring honestly | Exists, rewrite due | Stretch |
| 03 | [`03_hw_sw_codesign.md`](03_hw_sw_codesign.md) | Hardware/software co-design | Criteria for moving work into silicon, programmable offload as a hedge, Wi-Fi functions as a worked example, interfaces, power | Exists, rewrite due | Stretch |
| 04 | `04_mcu_vs_mpu_and_bare_metal.md` | MCUs, MPUs and bare metal | Cortex-M vs Cortex-A, memory maps, the vector table, startup code and linker scripts, a bare-metal blink | Stretch | — |
| 05 | `05_peripherals_gpio_uart_spi_i2c.md` | Peripherals on bare metal | GPIO, UART, SPI and I2C at the register level, interrupts and DMA on a microcontroller | Stretch | — |
| 06 | `06_freertos_in_practice.md` | FreeRTOS in practice | Tasks, queues, semaphores and mutexes, tick vs tickless, stack sizing, tracing | Stretch | — |
| 07 | `07_bootloaders_and_firmware_update.md` | Bootloaders and firmware update | Boot ROM to bootloader to application, A/B partitions, secure boot, rollback protection | Stretch | — |
| 08 | `08_low_power_design.md` | Low-power design | Sleep states, clock gating, measuring current, duty cycling | Stretch | — |

Stretch labs use QEMU's Cortex-M machines or an inexpensive board (an STM32 Nucleo or a Raspberry Pi Pico).

---

## 3. Representative interview questions

1. What does "real-time" mean? Is a faster system more real-time?
2. Explain priority inversion and two protocols that prevent it.
3. How do you know a set of periodic tasks will always meet their deadlines?
4. Why do safety-critical systems avoid dynamic memory allocation after start-up?
5. What contributes to interrupt latency, and how do you measure it?
6. When would you choose an RTOS over Linux, and when Linux with PREEMPT_RT?
7. How do you make a model fast on an edge device? What does INT8 quantisation cost in accuracy, and how do you check?
8. A Wi-Fi feature could be done in firmware or in host software. How do you decide?

---

## 4. How to study this track

1. Topic 01: the five-step method in [`../README.md`](../README.md), with OS 04–06 fresh in mind.
2. Topics 02–03: read before an edge-AI or Staff-level hardware interview.
3. Topics 04–08: only for firmware roles, after week 26.

---

## 5. Resources

| Resource | Use it for |
|---|---|
| *Real-Time Systems*, Jane W. S. Liu | Scheduling theory |
| FreeRTOS documentation and *Mastering the FreeRTOS Real Time Kernel* (free) | RTOS practice |
| *Making Embedded Systems* (2nd ed.), Elecia White | Embedded design, practical and readable |
| Interrupt blog (memfault.com/interrupt) | Firmware engineering in depth |
| NVIDIA TensorRT and Jetson documentation; ONNX Runtime documentation | Edge inference |

---

## 6. Progress

| # | Topic | Status |
|---|---|---|
| 01 | RTOS design constraints | Rewritten before week 23 |
| 02–08 | Others | Stretch |

Cross-track view: [`../00_MASTER_PLAN.md`](../00_MASTER_PLAN.md).
