# Toolchain: Machines, VMs and Tools for Every Track

Most of this program runs on your Mac. Some of it cannot: Linux system calls and `/proc`, ELF tools, `perf`, kernel modules, and anything CUDA. This file lists the environments you need, when each one is first needed, and how to check that it works. Set each one up in the week shown, not earlier.

| Environment | Needed for | First needed |
|---|---|---|
| **1. Mac native** | DSA, C++, LLD (CMake project), CO benchmarks | Week 1 (already done for LLD) |
| **2. Linux VM on the Mac** | C labs with ELF tools, OS, Linux, networking labs, Project 01 `rkv` | Week 3 |
| **3. QEMU kernels** | Kernel and driver tracks, Project 02 `rkdma`, Project 06 `tinyos` | Week 14 (K 01) |
| **4. A machine with hardware counters** | `perf stat` cache and branch counters, `perf c2c`, Project 03 `pfilt` measurements | Week 20 (optional earlier) |
| **5. A GPU in the cloud** | GPU track, Project 04 `nanoinfer` | Week 21 |

---

## 1. Mac native

Already installed and verified for the LLD track (see [`../10_lld/00_plan.md`](../10_lld/00_plan.md), section 8): Xcode command-line tools, Homebrew, `cmake`, `ninja`, `googletest`, `pandoc`.

```bash
clang++ --version
cmake --version
```

Build any DSA or C++ companion file with:

```bash
clang++ -std=c++20 -O1 -g -Wall -Wextra -fsanitize=address,undefined file_demo.cpp -o demo && ./demo
```

**What does not work on the Mac:** LeakSanitizer and Valgrind on Apple Silicon, `/proc`, `epoll`, `perf`, ELF binaries and kernel modules. Those go to the Linux VM.

---

## 2. Linux VM on the Mac (week 3)

An ARM64 Ubuntu LTS VM runs at near-native speed on Apple Silicon. Any of these works; pick one:

| Tool | Why choose it |
|---|---|
| OrbStack | Fastest to set up, low overhead, good file sharing with the Mac |
| Lima (`brew install lima`) | Free and open source, command-line driven |
| UTM | Graphical, also runs x86-64 guests (slowly, by emulation) |

Install the development packages inside the VM:

```bash
sudo apt update
sudo apt install -y build-essential clang lldb gdb cmake ninja-build valgrind \
    strace ltrace linux-tools-common linux-tools-generic bpftrace \
    iproute2 tcpdump ethtool iperf3 netcat-openbsd redis-server redis-tools \
    python3-pip git
```

Check it:

```bash
uname -m                 # aarch64
gcc --version && clang --version
valgrind --version
redis-server --version   # the baseline for Project 01
```

For **x86-64** comparisons (struct layout, assembly, memory ordering on TSO), use Rosetta-backed x86-64 user-space support in OrbStack or Lima, or the cross compiler `gcc-x86-64-linux-gnu` together with godbolt.org for reading assembly.

---

## 3. QEMU kernels (week 14, built in K 01)

K 01 creates the scripts in this folder:

```text
99_toolchain/
├── README.md               this file
├── kernel/
│   ├── build_kernel.sh     fetch a 6.x kernel, apply the debug config, build
│   ├── debug.config        KASAN, lockdep, kmemleak, DEBUG_INFO, KGDB, DEBUG_KOBJECT
│   └── run_qemu.sh         boot with an initramfs, gdb stub on :1234
├── initramfs/              BusyBox-based root file system
└── riscv/                  toolchain notes for Project 06 (tinyos)
```

The plan:

1. **Build** the kernel inside the Linux VM (native ARM64 builds take minutes).
2. **Boot** it with `qemu-system-aarch64 -M virt` inside the VM (software emulation, simple and reliable) or on the Mac with `-accel hvf` (much faster).
3. For x86-64 kernels, cross-compile with `gcc-x86-64-linux-gnu` and boot under `qemu-system-x86_64` (emulated, slower but fine for a BusyBox system).
4. Attach `gdb` (or `gdb-multiarch`) to QEMU's stub, load `vmlinux`, and use the kernel's `scripts/gdb` helpers (`lx-symbols`, `lx-dmesg`).

Packages inside the VM:

```bash
sudo apt install -y qemu-system-arm qemu-system-x86 qemu-system-misc gdb-multiarch \
    flex bison bc libelf-dev libssl-dev libncurses-dev dwarves cpio busybox-static \
    gcc-x86-64-linux-gnu gcc-riscv64-linux-gnu
```

---

## 4. A machine with hardware performance counters (week 20)

Hardware counters (`cycles`, `cache-misses`, `branch-misses`, and `perf c2c`) are usually **not available inside VMs on Apple Silicon**, because the hypervisor does not expose the CPU's performance-monitoring unit to the guest. Check in the VM:

```bash
perf stat -e cycles,instructions true
```

If the counters show `<not supported>`, use one of these for the measurements that need them (CO 14 and 19 measurements, `pfilt`):

| Option | Notes |
|---|---|
| A spare x86-64 PC or laptop running Linux natively | Best: full counters, `perf c2c`, and you control it |
| A cloud bare-metal instance (for example AWS `*.metal`) | Full PMU access; pay by the hour, stop it when done |
| A large cloud VM instance | Some providers expose a subset of counters on larger sizes; check with the command above |

Everything else in the program works without counters, and timing-based measurements (latency, throughput) work in the VM.

---

## 5. A GPU in the cloud (week 21)

Your Mac has no NVIDIA GPU. Use:

| Option | Notes |
|---|---|
| **Google Colab** | Free tier usually gives a T4; paid tiers give newer GPUs. Enough for the GPU labs and `nanoinfer` |
| A cloud GPU by the hour (for example AWS `g4dn`/`g5`, Lambda, RunPod) | For Nsight Compute sessions and longer runs |

In a Colab notebook:

```bash
!nvidia-smi                              # which GPU you got
!nvcc --version
!nvcc -O3 -arch=sm_75 reduce.cu -o reduce && ./reduce   # sm_75 = T4; match your GPU
```

Record the GPU model, driver and CUDA version beside every result, because Colab assigns different GPUs.

---

## 6. Status

| Environment | Status |
|---|---|
| Mac native | Done (verified with the LLD project) |
| Linux VM | Set up in week 3 |
| QEMU kernels | Built in K 01, week 14 |
| Counters machine | Decide by week 20 |
| Cloud GPU | Week 21 |
