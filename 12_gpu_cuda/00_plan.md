# GPU Architecture and CUDA: Track Plan

> **Track code:** GPU · **Weeks:** 21–22 (topic 04 is stretch) · **Budget:** ~12 hours plus Project 04 · **Prerequisites:** CO 13–14, 19 and 28 (caches, coherence, performance laws); CO 22–23 (GPU architecture); CO 25–26 (PCIe and DMA)

Roles that touch GPUs assume you can reason about how a GPU executes code and where its performance comes from. An afternoon of reading is not preparation. This track builds the model properly: thousands of threads in warps, a memory hierarchy that must be managed by hand, and a system around the GPU (PCIe, drivers, the IOMMU) that decides how fast data gets in and out. Project 04 (`nanoinfer`) turns it into measured numbers.

Your Mac has no NVIDIA GPU. Labs run on Google Colab (the free tier usually provides a T4; paid tiers offer newer GPUs) or a cloud GPU instance; `99_toolchain/README.md` has the setup.

---

## 1. Why this track matters

| Team type | What they ask |
|---|---|
| **CUDA libraries, compilers, deep-learning frameworks** | Coalescing, shared memory and bank conflicts, occupancy, warp divergence; write a parallel reduction or a tiled matrix multiply on the whiteboard. |
| **System software, drivers** | How work reaches the GPU: command buffers, doorbells, interrupts, the IOMMU, pinned memory, PCIe bandwidth; the user/kernel boundary in the driver. |
| **Inference and AI platforms** | Batching, the KV cache, memory-bandwidth-bound vs compute-bound kernels, multi-GPU communication. |
| **Everyone** | "Why is this kernel slow?" answered with the roofline model and numbers, not adjectives. |

---

## 2. What you will be able to do after this track

1. Explain SMs, warps, SIMT execution and divergence, and why GPUs hide latency with parallelism instead of caches.
2. Write CUDA kernels with correct grid and block indexing, and explain coalescing, shared-memory tiling and bank conflicts.
3. Write a parallel reduction and a tiled matrix multiply, and explain each optimisation step with its speedup.
4. Use the roofline model to decide whether a kernel is memory- or compute-bound, and what to change.
5. Explain how data and work move between CPU and GPU: PCIe and NVLink, pinned and unified memory, streams, and what the driver does on a kernel launch.
6. Profile with Nsight Compute and Nsight Systems and read the key metrics.

---

## 3. Topics

| # | File | Topic | Key subtopics | Status | Week |
|---|---|---|---|---|---|
| 01 | `01_gpu_architecture.md` | GPU architecture | CPU vs GPU design goals, SMs, warps and SIMT, divergence, the memory hierarchy (registers, shared memory, L1, L2, HBM), occupancy and latency hiding, tensor cores at a glance, the roofline model | To write | 21 |
| 02 | `02_cuda_programming.md` | CUDA programming | Kernels, grids, blocks and threads, memory allocation and copies, coalescing, shared-memory tiling and bank conflicts, synchronisation, parallel reduction (seven steps from naive to warp shuffles), scan (prefix sum) and histogram, tiled matrix multiply, atomics, streams and asynchronous copies | To write | 21 |
| 03 | `03_gpu_system_view_pcie_drivers_profiling.md` | The system view | PCIe and NVLink bandwidth, pinned vs pageable vs unified memory, what a kernel launch does (user-mode driver, command buffers, doorbells, interrupts), the IOMMU and GPU virtual memory, multi-GPU basics, profiling with Nsight Systems and Nsight Compute | To write | 22 |
| 04 | `04_inference_systems.md` | Inference systems | Batching and continuous batching, the KV cache and paged attention, quantisation, kernel fusion and FlashAttention-style tiling, KV-cache sizing, TensorRT, NCCL collectives (all-reduce), tensor and pipeline parallelism | Stretch | — |

---

## 4. Labs

All labs are notebooks that run on Colab.

| Lab | Topic | What you measure |
|---|---|---|
| `labs/vector_add_and_bandwidth/` | 01, 03 | Achieved device bandwidth vs the GPU's peak; host-to-device bandwidth with pageable vs pinned memory |
| `labs/reduction_steps/` | 02 | The classic reduction optimisation sequence, with time and bandwidth at each step |
| `labs/coalescing_and_transpose/` | 02 | Naive vs shared-memory matrix transpose, with and without bank-conflict padding |
| `labs/tiled_gemm/` | 02 | Naive vs tiled matrix multiply vs cuBLAS, in GFLOP/s, placed on a roofline |
| `labs/streams_overlap/` | 03 | Overlapping copies with compute using streams, shown on an Nsight Systems timeline |

These labs are the first milestones of Project 04, [`nanoinfer`](../14_projects/04_nanoinfer_cuda_kernels/SPEC.md).

---

## 5. Representative interview questions

1. Why does a GPU have so many threads? How does it hide memory latency without large caches?
2. What is a warp? What happens when threads in a warp take different branches?
3. What is memory coalescing? Show an access pattern that coalesces and one that does not.
4. Write a parallel sum reduction. How do you avoid divergence and bank conflicts? How do warp shuffles help?
5. Explain shared-memory tiling for matrix multiplication. How much does it reduce global-memory traffic?
6. What limits occupancy, and is higher occupancy always better?
7. A kernel reaches 30% of peak FLOPs. How do you decide whether it is memory-bound or compute-bound?
8. Why is copying from pageable host memory slower than from pinned memory?
9. What happens, from the CPU's side, when you launch a CUDA kernel?
10. How would you overlap data transfer with computation for a stream of inputs?

---

## 6. How to study this track

1. Follow the five-step method in [`../README.md`](../README.md).
2. Write the reduction and the tiled matrix multiply from memory on paper; both are whiteboard questions.
3. For every optimisation, predict the speedup before measuring.
4. Connect to your drivers track: GPU 03 is DRV 07 and DRV 09 applied to a GPU.

---

## 7. Resources

| Resource | Use it for |
|---|---|
| NVIDIA CUDA C++ Programming Guide and Best Practices Guide | The primary references |
| *Programming Massively Parallel Processors* (4th ed.), Hwu, Kirk & El Hajj | The textbook for GPU architecture and CUDA patterns |
| Mark Harris, "Optimizing Parallel Reduction in CUDA" (NVIDIA) | The classic reduction optimisation walkthrough |
| Simon Boehm, "How to Optimize a CUDA Matmul Kernel" (blog) | Step-by-step GEMM optimisation with measurements |
| GPU MODE lectures (YouTube) | Practical CUDA and kernel-writing talks |
| Nsight Compute and Nsight Systems documentation | Profiling |

---

## 8. Progress

| # | Topic | Status |
|---|---|---|
| 01–03 | Core topics | 01 starts week 21 |
| 04 | Inference systems | Stretch |

Cross-track view: [`../00_MASTER_PLAN.md`](../00_MASTER_PLAN.md).
