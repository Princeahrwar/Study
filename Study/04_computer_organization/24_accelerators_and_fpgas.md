# CO 24: Accelerators and FPGAs

*Part of the [Computer Organization plan](00_plan.md) · Skim tutorial · Time: about 3 hours (2 reading, 1 problems) · You need: [tutorial 23](23_gpu_memory_tensor_cores_and_scaling.md) (the roofline in section 7 and when a GPU loses to a CPU in section 9.6), [tutorial 21](21_simd_and_vector_processing.md) (SIMD) and [tutorial 03](03_boolean_logic_and_combinational_circuits.md) (muxes, and why a lookup table can compute any function)*

By the end of this tutorial you can:

- explain why accelerators exist using energy numbers, and name the four tricks almost all of them use;
- show why a convolution and an attention layer become matrix multiplications, work out the matrix shape of a convolution, and say what is left over for the vector unit;
- step a systolic array through a small multiplication by hand, use the cycle formula `tiles × max(M, P) + 2P − 2`, and say why the array needs a large `M` to be efficient;
- describe the original TPU (block diagram, 700 MHz, 65,536 multiply-adders, 34 GB/s of weight bandwidth), place its six benchmark networks on its roofline, and say why four of the six were memory-bound;
- say what an NPU adds to a microcontroller or phone chip, and why one unsupported operator in the middle of a network is expensive;
- contrast wafer-scale and compiler-scheduled dataflow chips with a GPU, using capacity and bandwidth arithmetic;
- explain fixed-function engines, the "first packet slow, rest of the flow fast" split (with Linux's flowtable as the example), what a DPU is, and budget cycles per packet at 100 and 400 Gb/s;
- explain what is inside an FPGA (lookup tables, carry chains, flip-flops, DSP and RAM blocks), read real synthesis output, and quote the FPGA-versus-ASIC gap;
- decide what to offload with a break-even formula, and say what goes wrong in a chip that shares one DRAM among many accelerators;
- place neuromorphic and quantum hardware in one honest paragraph each.

Tutorials 22 and 23 explained one accelerator in depth, the GPU. This tutorial steps back. A GPU is the *general* accelerator: thousands of programmable threads, a deep memory system, a mature software stack. Everything else in this tutorial gives up some of that generality for efficiency: a systolic array that can only multiply matrices, a wafer of SRAM that only holds what fits, a network card that only parses packets, an FPGA whose logic you rewire. The reason to learn them is partly that you will meet them (every phone, router and cloud server now contains several), and partly that the same four ideas explain all of them, so a new accelerator in an interview stops being new. This is a skim tutorial: it gives you the model, the numbers worth remembering and the questions to ask of any accelerator, not a design manual.

**Where the numbers come from.** **Nothing here was timed on an accelerator.** There is no TPU, NPU, DPU or FPGA board in the virtual machine used for tutorials 14 to 23. Four kinds of evidence are used. (1) **Published papers and vendor documents**, linked where they are used: Google's [TPU paper](https://arxiv.org/pdf/1704.04760) (Jouppi et al., ISCA 2017), Google's [TPU architecture](https://docs.cloud.google.com/tpu/docs/system-architecture-tpu-vm) and [v5p](https://docs.cloud.google.com/tpu/docs/v5p) pages, the Hot Chips slides of [Cerebras](https://www.hc2024.hotchips.org/assets/program/conference/day2/72_HC2024.Cerebras.Sean.v03.final.pdf) and [Groq](https://hc34.hotchips.org/assets/program/conference/day2/Machine%20Learning/HotChips34%20-%20Groq%20-%20Abts%20-%20final.pdf), the [BlueField-3 data sheet](https://resource.naddod.com/files/2025-06-12/nvidia-bluefield-3-dpu-datasheet-011657.pdf) (a reseller's copy of NVIDIA's), the [AWS Nitro security whitepaper](https://docs.aws.amazon.com/whitepapers/latest/security-design-of-aws-nitro-system/the-components-of-the-nitro-system.html), the Linux [flowtable documentation](https://docs.kernel.org/networking/nf_flowtable.html), the Microsoft [Catapult paper](https://www.cs.princeton.edu/courses/archive/spring16/cos598F/Catapult_ISCA_2014.pdf), and Kuon and Rose's [FPGA-versus-ASIC study](https://www.eecg.utoronto.ca/~jayar/pubs/kuon/kuontcad06.pdf). The energy table of section 1 is Horowitz's ISSCC 2014 table as reproduced in a [university lecture](https://www.cs.sfu.ca/~ashriram/Courses/CS7ARCH/assets/lectures/09_DSA_Metrics.pdf), not read from the original talk. Vendor performance claims (tokens per second, "times faster") are marked as claims. (2) **A register-level simulation of a small systolic array**, which I wrote and ran to check the cycle formula of section 3; the result is shown, the program is not part of the tutorial. (3) **Real output of a synthesis tool**: Yosys 0.70 (open source, `pip install yowasp-yosys`, or `brew install yosys`) mapped three small Verilog modules onto Xilinx 7-series cells; section 7 shows what it produced. (4) **Small Python and NumPy checks** of arithmetic: the convolution-to-matrix conversion, the roofline numbers, the packet budgets and the break-even points. Where a figure is my derivation from published numbers rather than a published figure, the text says so.

> **The one-sentence model.** An accelerator wins by spending its energy on arithmetic instead of on moving data and running an instruction stream: it uses many simple units driven by few instructions, keeps operands in nearby memory and reuses them (a systolic array reuses each value 128 or 256 times as it flows past), uses narrow numbers (8-bit arithmetic is about 15 times cheaper than FP32 in the same circuit family), and fixes the dataflow in advance (by hardware wiring, as in a TPU or a packet engine, or by a compiler, as in dataflow chips); the price is generality, so what to offload comes down to whether the work per byte moved across the link beats the break-even (`ops per byte > CPU rate ÷ link bandwidth`, plus a latency floor) and whether the fast path is hit often enough that the slow path does not dominate; FPGAs sit in the middle (about 35 times the area and 14 times the dynamic power of the same logic as an ASIC, in exchange for rewiring after manufacture), and neuromorphic and quantum chips are research directions, not part of today's stack.

---

## 1. Why accelerators exist

### 1.1 The bill is energy, not arithmetic

Tutorial 12 showed an out-of-order core: fetch, decode, rename, schedule, a register file and several cache levels, all to execute one simple operation. Horowitz's well-known table puts numbers on what operations cost in a 45 nm process (energy per operation, as reproduced [here](https://www.cs.sfu.ca/~ashriram/Courses/CS7ARCH/assets/lectures/09_DSA_Metrics.pdf); the last column is my arithmetic):

| Operation | Energy (pJ) | In 8-bit adds |
|---|---|---|
| 8-bit integer add | 0.03 | 1 |
| 32-bit integer add | 0.1 | 3 |
| 16-bit float add | 0.4 | 13 |
| 32-bit float add | 0.9 | 30 |
| 8-bit integer multiply | 0.2 | 7 |
| 32-bit integer multiply | 3.1 | 103 |
| 16-bit float multiply | 1.1 | 37 |
| 32-bit float multiply | 3.7 | 123 |
| read 32 bits from an 8 kB SRAM | 5 | 167 |
| read 32 bits from DRAM | 640 | 21,333 |

Read it as ratios, because the absolute values belong to 2014 and to one process (they shrink with each node, and HBM reduced the DRAM figure; the *ordering* of the rows has survived). Three consequences drive everything in this tutorial:

1. **Data movement is the bill.** One DRAM read costs as much as **173 FP32 multiplications** (640 ÷ 3.7). A multiply-add whose two operands come from DRAM costs 1,280 pJ of reading for 4.6 pJ (3.7 + 0.9) of arithmetic, **278 times** more. The arithmetic is nearly free; keeping operands close, and using each fetched value many times, is the whole game. An SRAM read is 128 times cheaper than a DRAM read, and a wire to a neighbouring cell is cheaper again.
2. **Precision is energy.** An 8-bit multiply costs 18.5 times less than an FP32 multiply. An INT8 multiply-accumulate with a 32-bit accumulator costs 0.2 + 0.1 = 0.3 pJ against 4.6 pJ for the FP32 one: **15 times** less, and an 8-bit value also moves a quarter of the bytes. This is why the first TPU used 8-bit integers and why tutorial 23 spent a section on formats.
3. **The overhead of a general core is a large multiple of the arithmetic it supports.** A 32-bit integer add is 0.1 pJ in the adder; the fetch, decode, rename, scheduling, register-file and cache machinery that surrounds it costs far more per instruction than the adder does. SIMD (tutorial 21) amortises that overhead over 4 to 16 lanes; a matrix instruction amortises it over thousands of multiply-adds; a fixed-function engine removes the instruction stream altogether.

### 1.2 Why now: the power wall

For decades each smaller process node let transistors switch faster at the same power per unit of area (*Dennard scaling*), so every new chip could use all its transistors at full speed. That stopped in the mid-2000s: leakage and voltage limits mean a chip can no longer afford to switch everything at once within its power and cooling budget, the situation called **dark silicon** ([tutorial 28](28_performance_and_power.md) section 1 covers the technology trends). Transistors are still cheap; *power* is the limit. If you cannot make the same circuit faster you must make the same joule do more work, and the way to do that is to specialise the circuit to the job. Hennessy and Patterson's *domain-specific architectures* chapter (chapter 7 of *Computer Architecture: A Quantitative Approach*, in the plan's resources) makes the same argument.

### 1.3 The four tricks

Look inside almost any accelerator and you find the same four ideas, each a response to a row of the energy table:

| Trick | What it does | Which cost it removes |
|---|---|---|
| **Many simple units, few instructions** | one instruction launches thousands of multiply-adds; or no instructions at all, the datapath is wired | instruction fetch, decode, rename: the overhead of point 3 |
| **Keep operands near and reuse them** | scratchpads managed by software, or values passed between neighbouring units, instead of caches and DRAM trips | the 640 pJ DRAM read; replaced by 5 pJ SRAM reads or wires |
| **Narrow arithmetic** | 8-bit (or 4-bit, or block-floating-point) multiplies, accumulating in 32 bits | the 3.7 pJ FP32 multiply and 4× the bytes moved |
| **Fix the dataflow in advance** | the hardware or the compiler knows what is needed next, so there is no speculation, no miss handling, no arbitration at run time | the caches, branch predictors and schedulers of a CPU, which exist because a CPU does not know |

The original TPU (section 3) uses all four. A GPU uses the first (SIMT, tensor cores) and part of the second (shared memory, registers) but keeps run-time scheduling (warps), which is why it stays programmable.

### 1.4 The spectrum

Choosing hardware is choosing a point on a line from flexible to efficient:

| | CPU core | GPU | Dataflow / systolic chip | FPGA | Fixed-function ASIC |
|---|---|---|---|---|---|
| **Programmed by** | any software | CUDA-style kernels | a compiler for a graph of tensor operations | a hardware description or high-level synthesis, then a bitstream | nothing; the function is the silicon |
| **Handles** | anything, branchy code | massively parallel, regular work | one family of computations (matrices, tensors) | anything you can draw as logic, at a low clock rate | exactly one job |
| **Efficiency on its best case** | lowest | high | highest for its family | middle; typically one to two orders of magnitude below an ASIC (section 7.6) | highest |
| **Cost to change the function** | recompile | recompile | recompile (new operator may need new hardware) | reprogram the bitstream | a new chip |
| **Where it pays** | always | dense parallel numerics | steady, large AI workloads | low volume, low latency, changing standards | huge volume, stable standards |

The cost-of-change row is the one interviews turn on. Silicon is only worth fixing when the function is stable and the volume is high: a TLS cipher suite or an Ethernet checksum is; a new neural-network layer type or a changing protocol may not be.

---

## 2. Everything becomes a matrix multiply

Most accelerators in this tutorial are matrix engines, so the first question about a workload is how much of it is a matrix multiplication. For neural networks the answer is nearly all of the arithmetic.

### 2.1 Fully connected layers

A fully connected layer with `K` inputs and `N` outputs computes `y = x·W`, where `W` is `K × N`: `K·N` multiply-adds. For one input vector that is a matrix-vector product; for a batch of `M` input vectors it is `Y = X·W`, a matrix multiplication of shapes `M × K` times `K × N`, with `M·K·N` multiply-adds. Section 3 will show that `M` (the batch, or the number of rows) is the number that decides how well a matrix engine is used.

### 2.2 Convolution is a matrix multiply in disguise

A convolution slides a small filter over an image. To turn it into a matrix multiplication, lay out each *receptive field* (the patch of input under one output position) as one row of a matrix; this is called **im2col** (image to column). The filters become the columns of the other matrix:

```text
 input  C×H×W        filters F×C×k×k              output F×Ho×Wo

 each output pixel      im2col matrix A            weight matrix B         result
 reads a C·k·k          (Ho·Wo) × (C·k·k)    ×     (C·k·k) × F       =     (Ho·Wo) × F
 patch of input         one row per output         one column per
                        position                   filter
```

A small example, checked in NumPy: 3 input channels, an `8 × 8` image, four `3 × 3` filters, no padding, stride 1. The output is `6 × 6`:

```python
import numpy as np
C, H, W, F, K = 3, 8, 8, 4, 3
x = np.random.randint(-3, 4, (C, H, W)).astype(float)
w = np.random.randint(-3, 4, (F, C, K, K)).astype(float)
Ho, Wo = H - K + 1, W - K + 1
cols = np.stack([x[:, i:i+K, j:j+K].reshape(-1) for i in range(Ho) for j in range(Wo)])
out = cols @ w.reshape(F, -1).T            # the matrix multiply
print(cols.shape, w.reshape(F, -1).T.shape, out.shape)   # (36, 27) (27, 4) (36, 4)
```

The matrix multiplication has `M = 36` (output positions), `K = 27` (3 channels × 3 × 3) and `N = 4` (filters): `36 × 27 × 4 = 3,888` multiply-adds, exactly the `4 × 6 × 6 × 3 × 3 × 3` of the direct convolution. (I checked that the two methods give identical results.) The price of the trick is memory: the im2col matrix has 972 elements against 192 for the input, **5.06 times** more, because neighbouring patches overlap (a `k × k` filter repeats each interior pixel `k²` = 9 times). Real accelerators avoid building that matrix and generate the addresses on the fly, but the *shape* `M × K × N` is what you size the hardware against.

A real layer, ResNet-50's first convolution (`7 × 7`, 3 channels in, 64 out, stride 2, `224 × 224` input, `112 × 112` output): `M = 112 × 112 = 12,544`, `K = 7 × 7 × 3 = 147`, `N = 64`, so `12,544 × 147 × 64 =` **118 million** multiply-adds. Remember the shape: section 3.3 shows that `K = 147` and `N = 64` fit a 256-wide array badly.

### 2.3 Attention is two matrix multiplies and a softmax

For one attention head with sequence length `L` and head width `d`: scores `S = Q·Kᵀ` is `L × d` times `d × L` (`L²·d` multiply-adds); a softmax turns each row of `S` into probabilities; the output `O = P·V` is `L × L` times `L × d` (another `L²·d`). For `L = 4,096` and `d = 128`, the two matrix multiplications are `2 × 4096² × 128 =` 4.29 billion multiply-adds and the softmax is 16.8 million exponentials: **256 multiply-adds per exponential** (`2d`). So nearly all the arithmetic is matrix work, but every row needs a maximum, an exponential, a sum and a division, which a multiply-add array cannot do. Tutorial 23 section 8 showed how FlashAttention fuses this to keep the `L × L` matrix out of HBM.

### 2.4 What is left over

Elementwise operations (ReLU, GELU, residual adds), normalisation (layer norm), softmax, pooling and embedding lookups are **not** matrix multiplications. They do a handful of operations per element, so their arithmetic intensity is far below 1 operation per byte: they are bandwidth-bound anywhere. So every neural-network accelerator has the same three parts, and Google's [documentation of a TPU TensorCore](https://docs.cloud.google.com/tpu/docs/system-architecture-tpu-vm) names them:

- **matrix units (MXUs)**: the systolic arrays, where almost all the multiply-adds happen;
- a **vector unit**, for general computation such as activations and softmax;
- a **scalar unit**, for control flow, address calculation and other housekeeping.

The ratio matters: the matrix unit is far faster than the vector unit, so a model whose time is dominated by non-matrix operations (some normalisation-heavy or gather-heavy models) cannot use the machine. This is Amdahl's law (tutorial 18) applied inside the chip.

### 2.5 Layer fusion

The result of a matrix multiplication leaves the array into *accumulators* (32-bit registers that hold partial sums). Before it is written anywhere, the next few cheap operations can be applied as the numbers pass: add the bias, apply ReLU, round back to 8 bits. That is **layer fusion**. Without it, each of those steps writes a whole tensor out and reads it back. Take a convolution output of `56 × 56 × 128` INT8 values, 401,408 bytes (392 KiB), followed by bias and ReLU as separate steps: the two intermediate tensors are each written once and read once, **1.6 MB** of extra traffic per layer that fusion removes. The TPU's `Activate` instruction is exactly this: it reads the accumulators, applies the nonlinear function (ReLU, sigmoid, and pooling) and writes the result to the activation buffer ([TPU paper](https://arxiv.org/pdf/1704.04760), section 2). The same idea in software is kernel fusion (tutorial 23 section 8.1), and the graph-level version for edge deployment is in [EMB 02](../13_embedded/02_edge_ai_acceleration.md).

---

## 3. Systolic arrays and the TPU

### 3.1 The idea

A **systolic array** is a grid of identical multiply-accumulate cells in which data pulse from cell to neighbour, like blood through a heart (hence the name). In the form used by the TPU, each cell holds one **weight** and stays put (a *weight-stationary* array). Activations enter from the left and move right one cell per cycle; partial sums move down one cell per cycle:

```text
                  partial sums flow DOWN
                    ↓        ↓        ↓
   x0 ──────►  [ w00 ]─►[ w01 ]─►[ w02 ]      activations flow RIGHT
                    ↓        ↓        ↓
   x1 ──────►  [ w10 ]─►[ w11 ]─►[ w12 ]      (row k is delayed by k cycles,
                    ↓        ↓        ↓         so that x_k meets the partial
   x2 ──────►  [ w20 ]─►[ w21 ]─►[ w22 ]        sum coming from above)
                    ↓        ↓        ↓
                   y0       y1       y2       collected in the accumulators
```

Each cell does one thing per cycle: `psum_out = psum_in + w × activation_in`, then passes the activation right and the new partial sum down. Down a column, the partial sums add up `x0·w0j + x1·w1j + x2·w2j`, which is output `y_j` of the vector-matrix product `y = x·W`. Feed in a new input vector every cycle and the array produces a new result row every cycle.

Why this is efficient, in terms of section 1:

- **Reuse.** An activation fetched once from the buffer is used by every cell in its row as it passes; a weight, loaded once, is used for every input vector in the stream. A `P × P` array does `P²` multiply-adds per cycle while reading only `P` new values per cycle: a reuse of `P` (128 or 256).
- **No instruction stream.** A cell has a multiplier, an adder and a couple of registers. There is no fetch, decode or register file per multiply-add.
- **Short wires.** Each cell talks only to its neighbours, so wires are short, which keeps energy low and allows a high clock rate for the area.

### 3.2 A hand trace

Take a `2 × 2` array holding `W = [[1, 2], [3, 4]]` (`w00 = 1`, `w01 = 2`, `w10 = 3`, `w11 = 4`) and two input vectors `x(0) = [1, 2]` and `x(1) = [3, 4]`. The answers should be `x(0)·W = [1·1 + 2·3, 1·2 + 2·4] = [7, 10]` and `x(1)·W = [15, 22]`. Cell `(k, j)` (row `k`, column `j`) handles vector `v` at cycle `t = v + k + j`:

| Cycle | Cell (row, col) | Vector | Computation | Result |
|---|---|---|---|---|
| 0 | (0,0) | x(0) | 0 + 1·1 | 1 |
| 1 | (0,1) | x(0) | 0 + 2·1 | 2 |
| 1 | (1,0) | x(0) | 1 + 3·2 | **7** leaves: y0 of x(0) |
| 1 | (0,0) | x(1) | 0 + 1·3 | 3 |
| 2 | (1,1) | x(0) | 2 + 4·2 | **10** leaves: y1 of x(0) |
| 2 | (0,1) | x(1) | 0 + 2·3 | 6 |
| 2 | (1,0) | x(1) | 3 + 3·4 | **15** leaves: y0 of x(1) |
| 3 | (1,1) | x(1) | 6 + 4·4 | **22** leaves: y1 of x(1) |

Four cycles, and the four outputs are 7, 10, 15 and 22. (A register-level simulation of the same array gives the same answers.) The pattern generalises:

- The first result emerges after `P` cycles (the first vector must cross `P` rows); after that, **one row of `P` results per cycle**.
- The last result of a stream of `M` vectors emerges at cycle `M + 2P − 2`. With `M = 2` and `P = 2` that is 4, as above. The `2P − 2` is **fill and drain**: at the start and end only part of the array is busy.
- So the busy fraction of one stream is `M / (M + 2P − 2)`: 50% here, 80% for `M = 1,000` and `P = 128`.

### 3.3 Big matrices: tiles, and why `M` matters

A real weight matrix `B` (`K × N`) is bigger than `P × P`, so it is cut into `P × P` **tiles**. All `M` input rows stream past each tile. Tiles along `K` produce partial sums for the same outputs, which the **accumulators** add together; tiles along `N` produce different outputs. The number of tiles is `T = ⌈K/P⌉ × ⌈N/P⌉`.

Loading a new tile takes about `P` cycles (one row of weights per cycle; the TPU paper gives 256 cycles for its 256 × 256 array). That load is hidden by **double buffering**, a second set of weight registers that fills while the first is in use, as long as the stream over the current tile lasts at least `P` cycles, that is `M ≥ P`. The paper describes the new weights taking effect as the wave of data advances, alongside the first data of the next block, so consecutive tiles flow back to back and the fill and drain are paid once (my simulation of two tiles streamed back to back confirms `2M + 2P − 2` cycles). That gives the model to remember:

```text
 cycles       ≈ T × max(M, P) + 2P − 2          T = ceil(K/P) × ceil(N/P)
 utilisation  = (M × K × N) / (cycles × P²)
```

```python
import math
def cycles(M, K, N, P):
    T = math.ceil(K / P) * math.ceil(N / P)          # weight tiles
    return T * max(M, P) + 2 * P - 2
def utilisation(M, K, N, P):
    return M * K * N / (cycles(M, K, N, P) * P * P)
```

Run it on some shapes (the first six on a 128 × 128 array, the last on a 256 × 256 one like the first TPU's):

| Multiplication `M × K × N` | Tiles | Cycles | Utilisation | Why |
|---|---|---|---|---|
| 512 × 1024 × 768 | 48 | 24,830 | 99.0% | large `M`; `K`, `N` multiples of 128 |
| 1 × 4096 × 4096 (LLM decode, batch 1) | 1,024 | 131,326 | **0.8%** | each weight used once; time is the tile loads |
| 16 × 4096 × 4096 | 1,024 | 131,326 | 12.5% | same time, 16× the work |
| 128 × 4096 × 4096 | 1,024 | 131,326 | 99.8% | `M = P`: the loads are just hidden |
| 1,024 × 4096 × 4096 | 1,024 | 1,048,830 | 100% | compute-bound |
| ResNet-50 first convolution (12,544 × 147 × 64), `P = 128` | 2 | 25,342 | 28.4% | `K = 147` fills 2 tiles of 128 only 57%; `N = 64` fills 50% of the columns |
| the same, `P = 256` | 1 | 13,054 | 13.8% | `K` fills 57% of the rows, `N` only 25% of the columns |

Three lessons:

1. **A matrix engine needs `M ≥ P`.** The second row is tutorial 23's decode story seen from the matrix engine: at batch 1 each weight is used once, so the array idles at under 1% and the weights must be streamed from memory anyway. Batching, or a convolution's thousands of output pixels, is what gives `M` its size.
2. **Shape waste is real.** `K` and `N` are rounded up to multiples of `P`. A layer with 64 output channels fills only a quarter of a 256-wide array's columns. Compilers pad, pack several small layers together, or special-case the first layer (3 input channels is the worst case).
3. **A bigger array is not always better.** The last two rows run the same layer: the array with four times the multipliers finishes only 1.9 times sooner (13,054 against 25,342 cycles), so each multiplier does half the work. Narrow layers favour several smaller arrays, which is one reason later TPUs have several 128 × 128 units per core rather than one huge one.

### 3.4 The first TPU

Google's first TPU, in service in 2015 and described in the [ISCA 2017 paper](https://arxiv.org/pdf/1704.04760), is the textbook example. It is a PCIe card, a **coprocessor**: the host sends it instructions and it does not fetch its own.

```text
   host CPU ── PCIe Gen3 x16 ──►  instruction buffer
                                       │
   8 GiB DDR3 weight memory ──34 GB/s─► Weight FIFO ──┐
                                                      ▼
   host memory ◄─►  Unified Buffer ──────►  Matrix Multiply Unit
                    24 MiB (activations)    256 × 256 = 65,536 8-bit MACs
                         ▲                         │
                         │                         ▼
                         └──── Activate  ◄──── Accumulators
                           (ReLU, sigmoid,      4 MiB = 4096 × 256 × 32 bit
                            pooling)
```

| Property | Value |
|---|---|
| Matrix unit | 256 × 256 8-bit multiply-adders = 65,536 |
| Clock | 700 MHz |
| Peak | 65,536 × 700 MHz = 45.9 T multiply-adds/s = **91.75 TOPS** (the paper says 92; a multiply-add counts as two operations) |
| On-chip memory | 24 MiB Unified Buffer (activations) + 4 MiB accumulators |
| Weight memory | 8 GiB DDR3, **34 GB/s** |
| Process, power | 28 nm; 75 W TDP, 28 W measured idle, 40 W busy per die |
| Instructions | about a dozen; five matter: `Read_Host_Memory`, `Read_Weights`, `MatrixMultiply/Convolve`, `Activate`, `Write_Host_Memory`; CISC style with a repeat field, 10 to 20 cycles per instruction |

One `MatrixMultiply` instruction streams many rows through the array, so the instruction overhead per multiply-add is negligible. Compare with the four tricks of section 1.3: the single matrix instruction and the wired array are trick 1; the Unified Buffer and the systolic flow are trick 2; 8-bit arithmetic is trick 3; and the fixed pipeline (weights in, matrix multiply, activate, write back) is trick 4.

What it was built for matters. In the paper's table of Google's datacentre inference workload (July 2016), multilayer perceptrons were 61% of TPU use, LSTMs 29% and convolutional networks only 5%. The chip was designed for the workload that existed, not the one that was fashionable. The headline result: against the Haswell CPU and K80 GPU of its time, the paper reports the TPU as 15 to 30 times faster and 30 to 80 times better in TOPS per watt (the abstract's summary; the tables behind it give per-die geometric means of 14.5× over the CPU and 13.2× over the GPU, and workload-weighted means of 29.2× and 15.3×). Those are Google's numbers for Google's six networks, against contemporaries, not a statement about modern parts.

### 3.5 The TPU roofline

Apply the roofline of tutorial 23 section 7. The paper counts one multiply-add as one operation here, so peak is 45.9 T operations/s and the bandwidth is 34 GB/s:

```text
 ridge = 45.9e12 / 34e9 = 1,349  ≈  1,350 multiply-adds per weight byte      (the paper: 1,350)
```

Each network's intensity (multiply-adds per weight byte, from the paper's Table 1) against that ridge:

| Network | Type | MACs per weight byte | Roofline upper bound |
|---|---|---|---|
| MLP0 | MLP | 200 | 14.8% of peak |
| MLP1 | MLP | 168 | 12.5% |
| LSTM0 | LSTM | 64 | 4.7% |
| LSTM1 | LSTM | 96 | 7.1% |
| CNN0 | CNN | 2,888 | 100% |
| CNN1 | CNN | 1,750 | 100% |

Four of the six lie far left of the ridge: **the matrix unit mostly waits for weights.** The reason is the `M` of section 3.3: an MLP or LSTM layer uses each weight once per example in the batch, so its intensity is about the batch size (200 ≈ a batch of 200), while a convolution reuses each weight at every output position, thousands of times. The roofline is an upper bound (it ignores everything else that can go wrong), so the real fractions are lower. The paper's own what-if: replace the DDR3 with GDDR5, which moves the ridge to about 250 (a bandwidth of 45.9e12 ÷ 250 ≈ 184 GB/s, my arithmetic), and MLP0's bound rises from 14.8% to 80%. The lesson for any accelerator data sheet: **TOPS without the memory bandwidth is half a number.**

### 3.6 Later TPUs

The architecture scaled rather than changed. Google's [documentation](https://docs.cloud.google.com/tpu/docs/system-architecture-tpu-vm) describes a TensorCore as one or more MXUs, a vector unit and a scalar unit (section 2.4). A 128 × 128 MXU (used up to TPU v5p) does 16K multiply-accumulates per cycle; TPU v6e and TPU7x have 256 × 256 MXUs. Multiplications take bfloat16 inputs (8-bit integer or FP8 on some generations) and **accumulate in FP32**, the same recipe as tensor cores (tutorial 23 section 5.5). The [v5p page](https://docs.cloud.google.com/tpu/docs/v5p):

| TPU v5p, per chip | |
|---|---|
| TensorCores, MXUs | 2 TensorCores, 4 MXUs each (8 MXUs) |
| Peak | 459 TFLOP/s (bf16) |
| HBM | 95 GiB, 2,765 GB/s |
| Inter-chip links | 1,200 GB/s per chip (bidirectional) |
| Pod | up to 8,960 chips, 3D torus |

Two derivations of mine: the implied clock is `459e12 ÷ (2 × 8 × 16,384) = 1.75 GHz` (the page does not state the clock), and the ridge is `459e12 ÷ 2.765e12 = 166 flop/B`, against 295 for an H100's dense BF16 (989 ÷ 3.35). The TPU is balanced towards bandwidth and needs less reuse to become compute-bound. Chips are joined by a torus of direct links rather than the switched NVLink domain of tutorial 23 section 10, so collectives are mapped onto the torus.

### 3.7 A matrix engine inside the CPU: Intel AMX

Not every matrix engine is an accelerator card. Intel's Advanced Matrix Extensions put one **inside** the core: 8 **tile registers**, each up to 16 rows × 64 bytes (1 KB, 8 KB in all), and a **TMUL** (tile matrix multiply) unit working on INT8 and BF16 data ([Intel's documentation](https://www.intel.com/content/www/us/en/developer/articles/code-sample/advanced-matrix-extensions-intrinsics-functions.html)). Software configures the tiles (`_tile_loadconfig`), loads them (`_tile_loadd`), multiplies (`_tile_dpbssd` for signed INT8) and stores (`_tile_stored`). One detail worth knowing: a process must ask the Linux kernel for permission (`arch_prctl` with `ARCH_REQ_XCOMP_PERM` and `XFEATURE_XTILEDATA`), because the tile registers add 8 KB of state that the kernel would have to save and restore, so it does not enable them for every process by default. The reason to care: an engine inside the core shares the caches and has no PCIe trip, so it pays off at far smaller matrices than a discrete accelerator (section 8.3 gives the break-even arithmetic).

---

## 4. NPUs for edge AI

### 4.1 A TPU in miniature

An **NPU** (neural processing unit) is the same design shrunk onto a system-on-chip beside the CPU cores: an INT8 multiply-accumulate array, local SRAM, a DMA engine that streams layers between DRAM and the SRAM, a small vector unit for activations, hardware to decompress weights, and a command queue driven by a kernel driver. The budget is milliwatts to a few watts instead of tens of watts.

Arm's Ethos-U55 microNPU, designed to sit next to a Cortex-M microcontroller, shows the range ([WikiChip's write-up](https://fuse.wikichip.org/?p=3306)): the MAC array is configurable at 32, 64, 128 or 256 MACs (64 to 512 operations per clock). On a mature 55 or 40 nm process at 100 to 400 MHz that is 6.4 to 25.6 GOPS for the smallest and 51.2 to 205 GOPS for the largest; on 7 or 5 nm near 1 GHz, 0.25 TOPS (128 MACs) to 0.5 TOPS (256 MACs). It has only a small internal SRAM, fetches each layer by DMA, and reads **compressed weights** that it decodes on the fly just before use, which is the bandwidth trick again. Arm claims, for a voice-assistant workload, up to 6× the inference performance of a Cortex-M7 for a Cortex-M55 alone and up to 50× with the Ethos-U55 added (code must be recompiled to use the vector and NPU hardware); treat that as a vendor claim. The scale difference is the point: the first TPU delivered 91.75 TOPS; this is 0.0064 to 0.5 TOPS, two to four orders of magnitude lower, with the same internal structure.

### 4.2 Still bandwidth-bound

Edge NPUs sit on small memory buses, so everything in section 1 applies harder. [EMB 02](../13_embedded/02_edge_ai_acceleration.md) works the arithmetic (a 30 TOPS NPU on 10 GB/s needs 3,000 operations per byte, which almost nothing delivers) and the cures: INT8 or INT4 quantisation as a bandwidth cut, fusing layers so activations stay in the on-chip SRAM, and tiling feature maps. This tutorial does not repeat them; use that note for the toolchain (ONNX Runtime, TensorRT) and quantisation details.

### 4.3 The operator-support cliff

An NPU implements a fixed list of operators (convolution, depthwise convolution, pooling, add, ReLU and a few more). If a network contains one operator the NPU lacks, the compiler cuts the graph: NPU segment, then the CPU for that operator, then another NPU segment. Each cut costs a transfer of the activation tensor (or cache maintenance on a shared-memory chip), a synchronisation, and a slow CPU implementation of the operator. How much? Take the `56 × 56 × 128` INT8 activation of section 2.5, 401,408 bytes. A 3×3 convolution from 128 to 128 channels on that map is 462 million multiply-adds, which a 4 TOPS NPU does in **231 µs**. Moving the 401,408 bytes out to the CPU and back over a shared 4 GB/s path costs 2 × 100 µs = **201 µs**: one fallback adds 87% to that layer's time before the CPU has computed anything.

Practical consequences: check the NPU's supported-operator list before picking or exporting a model; prefer the NPU-friendly form of an operator (plain ReLU rather than an exotic activation); look in the profiler for CPU segments between NPU segments; and remember that an unsupported *data type* also triggers fallback, so quantise the whole graph, not part of it.

---

## 5. Dataflow and wafer-scale designs: the contrast with a GPU

### 5.1 What a GPU pays for its generality

A GPU keeps thousands of threads in flight so that some are always ready while others wait for memory (tutorial 22). That costs a huge register file, a cache hierarchy, run-time arbitration for shared resources and, for weights, a trip to HBM on every pass. The cost is worth it because the machine can run any kernel. If you know in advance that you will run *one family* of computation, a fixed graph of tensor operations, you can drop the run-time machinery and place the computation in space instead of scheduling it in time. That is **spatial dataflow**.

### 5.2 The idea

Map the layers of the model onto different regions of the chip. Each region keeps its own weights in local SRAM permanently. Activations stream from region to region like items on a production line. If all the weights fit on chip, **no weight is ever read from DRAM**: the memory wall of tutorial 23 section 7.4 (every token streams all the weights) disappears, because the weights stay beside the arithmetic.

The price is capacity. SRAM takes far more chip area per bit than DRAM, so the capacity per chip is small and the design lives or dies on whether the model fits.

### 5.3 Wafer scale: Cerebras WSE-3

Cerebras makes the chip as large as the wafer. From its [Hot Chips 2024 slides](https://www.hc2024.hotchips.org/assets/program/conference/day2/72_HC2024.Cerebras.Sean.v03.final.pdf) (the company's figures):

| WSE-3 | |
|---|---|
| Area, process | 46,225 mm², 5 nm TSMC (a whole wafer used as one chip) |
| Transistors | 4 trillion |
| Cores | 900,000, in 84 dies of about 10.7k cores each |
| Memory per core | 48 kB SRAM (plus 512 B of local cache) |
| On-wafer SRAM | **44 GB** (check: 900,000 × 48 KiB = 44.2 GB) |
| Memory bandwidth | **21 PB/s** |
| Fabric bandwidth | 214 Pbit/s |
| Peak | 125 PFLOPS AI compute |

Put it next to an H100's HBM with one useful number, the **sweep time**, how long it takes to read the whole memory once (capacity ÷ bandwidth):

```text
 H100:    80 GB / 3.35 TB/s = 23.9 ms
 WSE-3:   44 GB / 21 PB/s   =  2.1 µs          about 11,000 times faster
```

That is the bandwidth win: 21 PB/s against 3.35 TB/s is 6,300 times. The capacity side is the loss: 44 GB against 80 GB. Llama 3.1 70B in FP16 is 140 GB, so it does not fit on one wafer; Cerebras' slides split it across **four** wafers (176 GB of SRAM) and send only activations between them, which the slides say needs a small fraction of the available external bandwidth. They claim 1,800 tokens/s for Llama 3.1 8B and 450 tokens/s for Llama 3.1 70B for a single user (figures the slides attribute to Artificial Analysis; vendor-reported, so ask about batch, context length and precision before quoting them).

A sanity check of what those claims mean, my arithmetic. A single GPU holding the 70B model in FP8 (70 GB, which fits in 80 GB) cannot beat `3.35 TB/s ÷ 70 GB = 47.9 tokens/s` for one stream, because each token reads every weight. Four wafers' weight-bandwidth bound is `4 × 21 PB/s ÷ 140 GB = 600,000 tokens/s`, 1,300 times above the claimed 450. So on this design memory bandwidth is no longer what limits a single stream; the chain of dependent layers, the communication between regions and the arithmetic are. For *training*, the slides use a different mode, **weight streaming**: an external memory box streams weights to the wafer (external memory options of 1.5 TB, 12 TB or 1.2 PB are listed in Cerebras' [launch announcement](https://www.cerebras.ai/press-release/cerebras-announces-third-generation-wafer-scale-engine)).

### 5.4 Compiler-scheduled: the Groq chip

Groq's tensor streaming processor takes a different route to the same end. The [Hot Chips 34 slides](https://hc34.hotchips.org/assets/program/conference/day2/Machine%20Learning/HotChips34%20-%20Groq%20-%20Abts%20-%20final.pdf) describe a chip with about 220 MB of on-chip SRAM, roughly 750 INT8 TOPS at 900 MHz, and **no reactive hardware**: no caches managed at run time, no arbiters or queues; the compiler knows where every tensor sits on the chip and schedules every instruction and data movement cycle-accurately (traffic between chips is statically scheduled too), so latency is deterministic by construction. (The slides give the SRAM bandwidth as 80 TB/s on some slides and 55 TB/s on another, so do not quote it.) Fixed latency is the product feature: the answer arrives at the same cycle every time. The capacity limit is brutal: 220 MB per chip means a 70-billion-parameter model in INT8 (70 GB) needs `70e9 ÷ 220e6 ≈ 318` chips for the weights alone, before the KV cache and activations (my arithmetic, not a deployment figure).

### 5.5 The trade, side by side

| | GPU (H100) | Wafer-scale (WSE-3) | Compiler-scheduled chip (Groq) |
|---|---|---|---|
| Where weights live | 80 GB HBM3 | 44 GB SRAM on the wafer | 220 MB SRAM per chip |
| Weight bandwidth | 3.35 TB/s | 21 PB/s | tens of TB/s per chip (slides disagree) |
| Scheduling | hardware: warps, caches, run-time arbitration | compiler places layers on regions of the wafer | compiler schedules every cycle |
| Strength | flexible, large memory, mature software | no weight traffic; low single-stream latency (claimed) | predictable latency |
| Limit | memory wall at low batch | capacity: several wafers for a 70B model | capacity: hundreds of chips for a 70B model |
| New operator | write a kernel | compiler support | compiler support |

Where each wins follows: dataflow designs shine on **latency for one stream** of a model that fits, and on predictability; GPUs win when memory capacity, batch throughput per dollar or generality matters. In an interview the right answer to "why not build everything this way?" is capacity, cost per byte of SRAM, compiler maturity and flexibility, not "it is slower".

### 5.6 Compute near memory

The same instinct, applied to DRAM, is **processing in memory** (tutorial 16 section 10): put simple compute units next to the DRAM banks, whose internal bandwidth is far higher than the narrow channel leaving the chip, so scans, reductions and matrix-vector products run where the data sit. Prototypes and early products exist; the unsolved parts are the software model, what operations fit in a DRAM-process logic budget, and the coherence with the CPU. Treat it as a direction, not a default.

---

## 6. Fixed-function engines, SmartNICs and DPUs

### 6.1 A datapath wired for one job

A **fixed-function engine** is a datapath hard-wired to one operation (AES-GCM encryption, compression, a checksum, regular-expression matching, a video codec, packet parsing and lookup) with a standard interface around it. You do not run instructions on it; you hand it **descriptors**: "encrypt these 4 KB at this address with this key, put the result there", queued in a ring in memory, fetched by DMA, with a completion written back and perhaps an interrupt raised (the descriptor rings and DMA of [tutorial 26](26_interrupts_exceptions_and_dma.md)). Because there is no instruction stream and no cache miss to handle, the energy per byte is close to that of the logic itself. The cost is that the algorithm is frozen: a new cipher or protocol version needs new silicon (or a fallback to software). So engines go where the function is stable, specified, and used in volume: encryption, compression, checksums, codecs, packet forwarding.

### 6.2 The packet pattern: slow path once, fast path after

Network devices (routers, firewalls, NAT boxes, Wi-Fi access points, load balancers) all follow one pattern:

- the **first packets of a flow** go the **slow path**: the CPU evaluates the full rule set, makes a decision and creates a per-flow entry (a *session* or *connection-tracking* entry);
- later packets of the flow go the **fast path**: hash the headers, look up the entry, rewrite the headers (NAT, TTL), send. This needs no rule evaluation and can be done by a simplified software path or by hardware (an engine using a hash table or the TCAM of tutorial 13).

Linux implements this in the open: the [flowtable](https://docs.kernel.org/networking/nf_flowtable.html). After a flow's first packets have been forwarded normally, a rule adds the flow to the flowtable (`flow add`); later packets that match skip the classic forwarding path and go straight to the output device (`neigh_xmit()`), so Netfilter hooks after ingress no longer see them. Entries are keyed on the layer-2 encapsulation (VLAN, PPPoE), layer-3 addresses, layer-4 ports and the input interface, and store the NAT settings; the TTL is decremented on the way out. Fragments, TCP RST and FIN packets and packets over the MTU are sent back to the normal path. With the entry in the kernel's software table, conntrack marks it `[OFFLOAD]`; if the flowtable is declared with `flags offload` and the device and driver support it, the entry is pushed to the hardware and marked `[HW_OFFLOAD]` (a few packets may still take the software path while it is installed). The configuration, adapted from the kernel document (check the syntax against your nftables version):

```text
table inet x {
    flowtable f {
        hook ingress priority 0; devices = { eth0, eth1 };
        flags offload;                      # ask the driver to program the hardware
    }
    chain y {
        type filter hook forward priority 0; policy accept;
        ip protocol tcp flow add @f         # after the normal path has forwarded the first packets
    }
}
```

This is the shape of "hardware NAT" or "packet processing engine" blocks in router and access-point chips: the CPU runs the slow path (association, DHCP, new flows), the engine forwards established flows. How to decide which functions belong in silicon is the subject of [EMB 03](../13_embedded/03_hw_sw_codesign.md).

**The miss path decides the average.** A fast path that hits is cheap; a slow path costs maybe 100 times more. With illustrative costs (assumptions for the arithmetic, not measurements) of 20 cycles for a hit and 2,000 for a miss:

| Hit rate | Average cycles per packet | Share of cycles spent on the slow path |
|---|---|---|
| 90% | 218 | 92% |
| 99% | 39.8 | 50% |
| 99.9% | 22.0 | 9% |

Even at a 99% hit rate, half the work is still the 1% of packets that miss. A flow of 50 packets has a hit rate of at most 98% (only its first packet misses), so short flows (DNS lookups, small HTTP requests) keep the CPU busy however fast the engine is. This is why a firewall data sheet quotes two numbers, throughput (fast path) and **new connections per second** (slow path), and why a benchmark with a few long flows flatters an offload.

### 6.3 A packet budget

Why offload at all? Arithmetic. On Ethernet each frame carries 20 extra bytes on the wire (7 bytes preamble, 1 byte start-of-frame delimiter, 12 bytes inter-frame gap). A 64-byte minimum frame occupies 84 bytes of wire time; a 1,518-byte maximum frame occupies 1,538. Packets per second is `rate ÷ (wire bytes × 8)`:

| Link | Frame | Packets per second | Cycles per packet on one 3 GHz core |
|---|---|---|---|
| 100 Gb/s | 64 B | 148.8 M | 20 |
| 100 Gb/s | 1,518 B | 8.13 M | 369 |
| 400 Gb/s | 64 B | 595 M | **5** |
| 400 Gb/s | 1,518 B | 32.5 M | 92 |

At 400 Gb/s and minimum-size frames a core has **5 cycles** per packet, fewer than a single L2 cache hit takes (tutorial 13). No software path does that on one core, so line rate is reached by spreading flows over many cores (receive-side scaling, [tutorial 26](26_interrupts_exceptions_and_dma.md)), by bypassing the kernel with polled drivers, or by doing the work in hardware. The numbers also explain why packets per second, not gigabits, is what limits a software forwarding path.

### 6.4 SmartNICs and DPUs

The network card has grown through three stages:

1. **NIC with fixed offloads**: checksum, segmentation offload, multiple queues with RSS ([tutorial 26](26_interrupts_exceptions_and_dma.md)).
2. **SmartNIC**: a programmable hardware datapath with match-action flow tables, so whole forwarding decisions (virtual-switch rules, tunnel encapsulation) run on the card.
3. **DPU** (data processing unit): a SmartNIC that is also a small **computer**. NVIDIA's BlueField-3 ([data sheet](https://resource.naddod.com/files/2025-06-12/nvidia-bluefield-3-dpu-datasheet-011657.pdf)) has up to 16 Armv8.2+ A78 cores, 16 GB of DDR5, up to 400 Gb/s of Ethernet or InfiniBand, 32 lanes of PCIe Gen 5, hardware for switching (ASAP²), MACsec/IPsec/TLS encryption, NVMe-oF and NVMe/TCP acceleration, a regular-expression matching processor and IEEE 1588v2 time stamping. Its Arm cores run software of their own, separate from the host's operating system.

Why put a computer on the network card? Two reasons. **Cores:** every cycle the host spends running the virtual switch, firewall, storage client or telemetry is a cycle not sold to a tenant or used by the application; moving that infrastructure software to the DPU hands the host's cores back. **Isolation:** the infrastructure runs on a separate, trusted processor, so policy can still be enforced when the host OS is compromised or is a customer's bare-metal machine.

AWS built this idea into its EC2 servers and called it Nitro. According to AWS's [security whitepaper](https://docs.aws.amazon.com/whitepapers/latest/security-design-of-aws-nitro-system/the-components-of-the-nitro-system.html), dedicated **Nitro Cards** implement all the outward-facing control interfaces and all I/O: a Nitro Card for VPC networking, one for EBS storage and one for local NVMe storage, with hardware encryption engines, plus a Nitro Controller that is the hardware root of trust and the only gateway between the physical server and the EC2, EBS and VPC control planes. A **Nitro Security Chip** controls the main board's reset pins and intercepts the operations on its firmware storage. The **Nitro Hypervisor** left on the host has, by design, no networking stack, no general-purpose file systems and no peripheral device drivers, and the hypervisor is no longer needed for I/O virtualisation, system management or monitoring, which is what makes bare-metal instances possible. [Tutorial 27](27_hardware_virtualization.md) shows the host-side mechanisms (SR-IOV virtual functions, the IOMMU) that make a device on a card look like a device to a VM.

**Interview framing.** "Why a DPU rather than more host cores?" Because the host's cores are the product, infrastructure work scales with line rate rather than with tenant work, and a separate processor is a separate trust boundary. Palo Alto and Cisco style questions on firewalls and routers sit right here: what runs in hardware, what runs on the CPU, and what the miss path costs.

---

## 7. FPGAs

### 7.1 Hardware you can rewire

A **field-programmable gate array** is a chip whose logic is not fixed at manufacture. It is a large array of small configurable logic blocks, a programmable routing network between them, and **hard blocks** (multipliers, RAM, high-speed transceivers, sometimes processor cores) placed in columns among them. Configuration bits, held in SRAM cells, decide what each block computes and how the routing connects them; loading a different *bitstream* turns the same silicon into a different circuit.

### 7.2 The lookup table

Tutorial 3 noted that a mux can build any function: wire the data inputs to constants and the select inputs become the function's inputs. A **lookup table (LUT)** is exactly that with the constants held in memory: a `k`-input LUT is a `2^k`-bit memory addressed by the `k` inputs, read through a mux tree, and it can compute **any** function of `k` variables because you choose the stored bits. The full adder of tutorial 3 needs two 3-input functions:

| Index `c b a` | 000 | 001 | 010 | 011 | 100 | 101 | 110 | 111 |
|---|---|---|---|---|---|---|---|---|
| sum (`a ^ b ^ c`) | 0 | 1 | 1 | 0 | 1 | 0 | 0 | 1 |
| carry (majority) | 0 | 0 | 0 | 1 | 0 | 1 | 1 | 1 |

Read each row from index 7 down to index 0 and you get the 8-bit LUT contents: sum = `10010110` = `0x96`, carry = `11101000` = `0xE8`. Those are not my numbers: they are what a synthesis tool chose. I gave Yosys this Verilog,

```verilog
module fa(input a, b, cin, output s, cout);
  assign s    = a ^ b ^ cin;
  assign cout = (a & b) | (a & cin) | (b & cin);
endmodule
```

and asked it to map the design to Xilinx 7-series cells (`yosys -p "read_verilog fa.v; synth_xilinx -top fa -family xc7 -nodsp; stat"`). The result is two `LUT3` cells, one with `.INIT(8'he8)` and one with `.INIT(8'h96)`, plus input and output buffers. A LUT is just a memory you read, which you can check:

```python
SUM, CARRY = 0x96, 0xE8                                     # LUT3 contents chosen by the tool
lut = lambda init, a, b, c: (init >> (a + 2*b + 4*c)) & 1   # address the 8-bit memory
for a in (0, 1):
    for b in (0, 1):
        for c in (0, 1):
            assert lut(SUM, a, b, c) == a ^ b ^ c
            assert lut(CARRY, a, b, c) == (a + b + c >= 2)
```

### 7.3 What a fabric tile contains

In AMD's UltraScale devices ([user guide](https://docs.amd.com/r/en-US/ug574-ultrascale-clb/CLB-Overview)) a configurable logic block (CLB) holds one slice of **eight 6-input LUTs and sixteen flip-flops**. A 6-input LUT stores 64 bits and can instead be split into two 5-input LUTs that share inputs. Dedicated **carry logic** runs through the slice. In some slices the LUTs can also act as small memories (a LUT as a `64 × 1` RAM, "distributed RAM") or as a 32-bit shift register. The flip-flops give the registers of tutorial 4, so a synchronous design maps naturally: LUTs for the combinational logic between registers. The **routing** between blocks is a network of programmable switches; it adds delay and takes a large share of the area, one reason an FPGA runs at hundreds of MHz rather than several GHz.

Hard blocks are what keep FPGAs competitive. A **DSP slice** (the DSP48E2 in UltraScale, [documented here](https://docs.amd.com/r/en-US/ug974-vivado-ultrascale-libraries/DSP48E2)) is a fixed 27 × 18 multiplier with a 48-bit adder/accumulator that can split into two 24-bit or four 12-bit adders when the multiplier is unused. **Block RAMs** are dedicated memories. Both are hard-wired and much denser and faster than the same function built from LUTs.

### 7.4 Real synthesis output

I synthesised three small designs with Yosys 0.70 for Xilinx 7-series (`synth_xilinx -family xc7`). The counts exclude the input and output buffers (`IBUF`/`OBUF`), which belong to the chip's pins:

| Design | LUTs | Carry cells | DSP |
|---|---|---|---|
| 1-bit full adder | 2 × `LUT3` | 0 | 0 |
| 32-bit adder (`s = a + b`) | 32 × `LUT2` | 8 × `CARRY4` | 0 |
| 8 × 8 multiplier (`p = a * b`), DSP use disabled (`-nodsp`) | **114** (27 `LUT2`, 15 `LUT3`, 13 `LUT4`, 39 `LUT5`, 20 `LUT6`) | 4 × `CARRY4` | 0 |
| the same multiplier, DSP allowed | **0** | 0 | 1 × `DSP48E1` |

What to read from it:

- **The adder uses LUTs for the propagate bit and a carry chain for the rest.** Each `LUT2` computes `a ^ b` for one bit; the eight `CARRY4` cells (four bits each, chained so each takes the previous one's carry out) are dedicated fast carry hardware outside the general routing. That is how a 32-bit add reaches a useful clock rate on a fabric made of 6-input memories: the carry chain of tutorial 3's ripple-carry adder, rebuilt as dedicated hardware (AMD's documentation describes dedicated carry-lookahead gates and multiplexers).
- **A multiplier in LUTs is expensive**: 114 LUTs and 4 carry cells for 8 × 8 bits, against one hard DSP block. This is why the FPGA's multipliers are hard blocks, and why the gap to an ASIC narrows when a design uses them (section 7.6).
- **What you write decides what you get.** The same `a * b` became 114 LUTs or one DSP depending on a tool option. Reading the utilisation report is part of the job.

To reproduce it, put the full adder above in `fa.v` and these two modules in `add32.v` and `mul8.v`:

```verilog
module add32(input [31:0] a, b, output [31:0] s);  assign s = a + b;  endmodule
module mul8 (input [7:0]  a, b, output [15:0] p);  assign p = a * b;  endmodule
```

then run `yosys -p "read_verilog mul8.v; synth_xilinx -top mul8 -family xc7 -nodsp; stat"` (drop `-nodsp` to allow the DSP block), and the same for the others. Install with `brew install yosys` (command `yosys`) or `pip install yowasp-yosys` (command `yowasp-yosys`; its first run takes about a minute while it prepares itself).

### 7.5 How an FPGA is programmed

The flow: write the design as **RTL** (Verilog or VHDL, describing registers and the logic between them) → **synthesis** (turn it into LUTs, flip-flops and hard blocks) → **place and route** (assign every cell to a location and wire it through the routing network) → **timing closure** (check that the slowest register-to-register path fits within the clock period; if not, change the design or the constraints and repeat) → generate the **bitstream**, which an SRAM-based FPGA loads into its configuration cells at power-up. Place and route on a large design commonly takes from minutes to hours, which is the main cost of iteration compared with compiling software.

**High-level synthesis (HLS)** lets you write C or C++ and have a tool produce the RTL. Pragmas steer the hardware. An illustration (I did not run this through an HLS tool):

```c
int32_t dot(const int8_t a[1024], const int8_t b[1024]) {
    int32_t acc = 0;
    for (int i = 0; i < 1024; i++) {
#pragma HLS PIPELINE II=1          // start one new iteration every clock cycle
        acc += a[i] * b[i];
    }
    return acc;
}
```

With `II=1` (initiation interval of one) the loop becomes a pipelined multiply-accumulate that takes about 1,024 cycles plus the pipeline depth: 3 µs at 333 MHz. To go faster you unroll the loop by `U`, which needs `U` multipliers and `2U` operand bytes per cycle, and the RAMs must have enough ports: the bandwidth problem of section 1 again, now visible in the pragma list. C in this style is a *hardware description*: loops become pipelines, arrays become RAMs, and straight-line "software" habits (dynamic allocation, recursion, pointer chasing) do not synthesise.

### 7.6 The cost of generality

Kuon and Rose's study ([IEEE TCAD 2007](https://www.eecg.utoronto.ca/~jayar/pubs/kuon/kuontcad06.pdf)) measured the gap by building the same benchmark circuits on a 90 nm Altera Stratix II FPGA and in 90 nm standard cells:

| FPGA relative to ASIC | Logic only | Using DSP and block RAM |
|---|---|---|
| Area | about **35×** larger | about 18× (below 5× estimated for very heavy hard-block use) |
| Critical-path delay | about 3.4× slower (4.6× against the slowest FPGA speed grade) | still about 3 to 4× |
| Dynamic power | about **14×** higher | 7.1× with both hard-block types |

Caveats (the paper states them): 90 nm, core logic only (I/O excluded), power measured at 33 MHz with assumed toggle rates, results varied widely between circuits (the logic-only area gap ran from 17× to 54×), and the study is two decades old. Modern FPGAs have far more hard blocks, which narrows the gap, but the structure remains: **an ASIC beats an FPGA by one to two orders of magnitude in efficiency on the same function.** An FPGA therefore wins only when an ASIC is not an option, which is when volume is too low, the standard keeps changing, or time to market matters.

### 7.7 When an FPGA wins

- **Deterministic low latency.** A pipeline that handles a packet in a fixed number of clock cycles, with no operating system, no cache and no scheduling jitter: market-data processing, packet inspection, radio signal processing.
- **Odd data widths and bit-level work.** CRCs, compression, parsing, custom number formats (a 12-bit sample, a 3-bit weight) that a CPU or GPU wastes a full word on.
- **Low volume, changing requirements.** A new protocol revision is a new bitstream, not a new chip.
- **Glue and bridging.** Sensor interfaces, bus bridges, custom I/O.
- **Prototyping ASICs.**

It loses to a GPU on dense floating-point or tensor throughput at scale (the GPU has hard tensor cores at GHz clock rates), and to a CPU on branchy code and on development speed.

**A real deployment: Microsoft Catapult.** Microsoft's ISCA 2014 [paper](https://www.cs.princeton.edu/courses/archive/spring16/cos598F/Catapult_ISCA_2014.pdf) put an Altera Stratix V D5 FPGA with 8 GB of DDR3 on a PCIe card in each of 1,632 servers, with the FPGAs of 48 servers wired into a 6 × 8 torus (20 Gb/s links, sub-microsecond latency). For Bing's page-ranking stage the result was **95% higher throughput at the same latency distribution**, or **29% lower tail latency at the same throughput**, for about a 10% increase in power and within a 30% cost limit. The lessons the paper lists are good interview material: one FPGA per server (rather than a shared appliance) keeps the servers identical; reconfiguring an FPGA can disrupt its host and its neighbours, so the host had to mask non-maskable interrupts and send halt messages to the neighbouring links first; an always-on flight recorder is needed to debug rare failures at scale; and programmability was the main open problem.

### 7.8 How an FPGA connects to a CPU

| Style | How | Latency and bandwidth | Example |
|---|---|---|---|
| **PCIe card** | the FPGA is a PCIe device with BARs and DMA descriptor rings ([tutorial 25](25_buses_pcie_and_io_mapping.md) and [tutorial 26](26_interrupts_exceptions_and_dma.md)) | microseconds; PCIe bandwidth | Catapult |
| **Bump in the wire** | the FPGA sits between the NIC and the network (or switch) and processes packets as they pass | no host in the path | SmartNIC-style FPGAs |
| **SoC FPGA** | hard Arm cores and the fabric on one die, joined by on-chip buses | lowest latency; shared DRAM | embedded designs |
| **Coherent attach** | a cache-coherent link (CXL, [tutorial 25](25_buses_pcie_and_io_mapping.md)) so the FPGA reads and writes the CPU's memory without DMA staging | between the two | newer designs |

From Linux the first is a PCIe driver, the third a platform device described in the device tree. In every case the questions of section 8 apply: granularity, data movement and the fast path.

### 7.9 FPGA, GPU or ASIC?

| | GPU | FPGA | ASIC |
|---|---|---|---|
| Best for | dense parallel arithmetic, ML training and inference | streaming dataflow with tight latency, odd widths, changing standards | stable, high-volume functions |
| Clock | GHz | hundreds of MHz | GHz |
| Peak arithmetic | very high (tensor cores) | modest (DSP slices) | as designed |
| Latency | variable (kernel launch, scheduling) | deterministic, low | deterministic, low |
| Typical time to first result | days (CUDA) | weeks to months (RTL, timing closure) | years and a large up-front cost |
| Change after deployment | new software | new bitstream | none |

---

## 8. Heterogeneous SoCs and the offload decision

### 8.1 A phone, a router and a car computer are all heterogeneous

Almost every modern chip that is not a plain server CPU is a **system-on-chip** with many different processors on one die sharing one memory:

```text
 +---------------------------------------------------------------+
 |  CPU clusters     GPU     NPU     camera/ISP    video codec    |
 |  (big + little)                   display       crypto, secure |
 |       \            |       |          |              |      /  |
 |        +-------- on-chip interconnect (some blocks coherent) -+|
 |                            |                                   |
 |                    memory controller ──► shared LPDDR / DDR     |
 +---------------------------------------------------------------+
```

Each block is a separate piece of intellectual property with its own clock, power domain, firmware, driver and command queue. A phone chip pairs CPU cores with a GPU, an NPU, an image-signal processor and codecs; a Wi-Fi access-point or router chip pairs CPU cores with radio MAC hardware, a packet-forwarding engine and a crypto engine; a robotics or automotive chip adds vision and DSP blocks. The architectural questions are the same everywhere.

### 8.2 What goes wrong when blocks share a chip

- **DRAM bandwidth.** All the blocks stream through one memory controller. A 4K display at 60 Hz with 4-byte pixels reads `3840 × 2160 × 4 × 60 = 1.99 GB/s` just to refresh the screen (my arithmetic), before the camera, GPU or NPU move a byte. Real-time blocks (display, camera, audio) cannot tolerate being starved, so the interconnect and memory controller offer quality-of-service controls (tutorial 16 on scheduling). An NPU that is fast in isolation can glitch the video under load.
- **Coherence.** A block may be **I/O-coherent** (its accesses snoop the CPU caches, tutorial 19, so software need not maintain caches) or **non-coherent** (software must clean the CPU cache before the block reads a buffer and invalidate it before the CPU reads what the block wrote; [tutorial 26](26_interrupts_exceptions_and_dma.md)). Reading a stale cache line after a device's DMA is the classic bug of this world.
- **Translation and protection.** An **IOMMU** (the SMMU on Arm) translates the device's addresses and confines a buggy or hostile device to the buffers it was given; it also lets a device use scattered user pages as if they were contiguous (tutorials 15 and 25).
- **Power and heat.** Not every block can run flat out together (dark silicon, section 1.2); each domain has its own frequency and voltage control, and the thermal limit decides what stays on ([tutorial 28](28_performance_and_power.md) section 9).
- **Software hand-off.** A camera frame passes through the ISP, then the NPU, then the display, each with its own driver. The buffer is shared, and the blocks synchronise with explicit fences (on Linux, DMA-BUF buffers carry the fences). Most real SoC bugs live in these hand-offs, not in any block.

### 8.3 The offload decision

Let `W` be the work (operations), `D` the bytes that must cross the link, `B` the link bandwidth, `L` the fixed cost to launch and synchronise, and `R_c`, `R_a` the CPU's and accelerator's rates in operations per second. Then:

```text
 T_cpu = W / R_c
 T_acc = L + D / B + W / R_a

 offload wins when   W / R_c  >  L + D / B + W / R_a
```

When the accelerator is much faster (`R_a ≫ R_c`) the last term is small and two conditions remain:

```text
 bandwidth condition:   W / D  >  R_c / B        operations per byte moved must exceed
                                                  the CPU's rate divided by the link's bandwidth
 latency condition:     W      >  L × R_c        the work must outlast the launch cost
```

With a 1 Tflop/s CPU and a 63 GB/s PCIe Gen5 link (one way, tutorial 23) the first needs **15.9 flop per byte moved**; with a 10 µs launch the second needs **10 Mflop** of work.

*Example 1: SAXPY.* `y = a·x + y` does 2 flops per element and moves 12 bytes (read `x` and `y`, write `y`): 0.17 flop/B, a hundred times short of 15.9. For 10⁸ elements the CPU, bound by an assumed 100 GB/s of DRAM bandwidth, takes 12 ms; the accelerator needs 19.4 ms, dominated by the 19 ms of PCIe transfers. It **loses**, and no accelerator speed fixes it. But if `x` and `y` already live in the accelerator's 3.35 TB/s HBM, the same operation takes 10 µs + 0.36 ms = 0.37 ms, 33 times faster than the CPU. Residency is what changes the answer.

*Example 2: matrix multiplication.* An `N × N` FP32 multiplication does `2N³` flops and moves `12N²` bytes (two matrices in, one out): `N/6` flop/B. The bandwidth condition holds for `N > 96`, the latency condition for `N > 171`. A direct calculation (CPU 1 Tflop/s, accelerator 100 Tflop/s, 10 µs launch) gives the break-even at **N = 211** with all transfers, **172** with the data already resident, and 196 if one operand stays resident and only `A` goes in and `C` comes out. The launch latency, not the transfer, sets the break-even for matrix multiplication, because the work grows as `N³` and the data only as `N²`. At `N = 512` with the weight matrix kept resident across calls, a call costs 46 µs against 268 µs on the CPU: 5.8 times faster (the first call, which also moves the weights, costs 63 µs).

*Amdahl's law still applies.* If a fraction `p` of the run time is accelerated by `s`, the overall speed-up is `1 / ((1 − p) + p/s + overhead)`: `p = 0.9` and `s = 100` give 9.2 times, `p = 0.5` gives under 2 times, however fast the accelerator is. In an integrated design (AMX in the core, an NPU sharing the SoC's DRAM) the `D/B` term shrinks or vanishes, but `L`, cache maintenance and the unaccelerated fraction remain.

### 8.4 A checklist before you offload

1. Is the work **regular** (the accelerator's pattern) and **large** enough to pass both thresholds?
2. Can the data **stay resident**? Where does each tensor or buffer live between steps? Do not ping-pong results back to the CPU after every operation.
3. What is the **fallback path** (unsupported operator, flow-table miss, buffer too large), and how often will it be taken?
4. Can copying **overlap** with computing (double buffering, tutorial 23 section 9.4)?
5. Is the **function stable**? Fixing a fast-changing algorithm in silicon is a bet.
6. Can you **observe** it? Counters for hits and misses, queue depths, per-operator timings: an offload you cannot measure is one you cannot debug.

---

## 9. Neuromorphic and quantum: one page

These two are research directions that interviewers sometimes use to see whether you can place a new idea in the model above. They are not part of today's production stack.

**Neuromorphic.** Conventional chips separate memory from compute and run on a clock. A neuromorphic chip is built from many small *neurons*, each holding its own state (and the synapse weights next to it) and communicating by short messages, *spikes*, only when something happens. Work therefore scales with activity rather than with the clock, and the memory wall is avoided by placing memory beside the compute. Intel's Loihi 2 ([WikiChip's description](https://fuse.wikichip.org/?p=6383)) has about one million neurons and 120 million synapses in 128 neuron cores, made on Intel 4 in 31 mm², with a programmable neuron model and spikes that can carry integer payloads; Intel claims more than 2× faster neuron-state updates, 5× faster synaptic operations and 10× faster spike generation than the first Loihi. The programming models and software are far from the maturity of a GPU's, and today's transformer language models do not run on it; the credible niche is sparse, event-driven, always-on sensing at very low power. In an interview: know the idea (event-driven, memory beside compute), contrast it with a dense, clocked matrix engine, and do not claim production use.

**Quantum.** A quantum processor is an accelerator in the architectural sense: it sits behind a classical host that compiles circuits, generates control pulses, reads out measurements and, crucially, runs error decoding in real time. Qubits are fragile, so a useful computation needs **error correction**, in which many physical qubits encode one logical qubit. Google's Willow chip (105 qubits, December 2024) showed logical errors roughly halving each time the code grid grew from 3 × 3 to 5 × 5 to 7 × 7, which Google calls operating "below threshold" ([announcement](https://blog.google/technology/research/google-willow-quantum-chip/)); Google names a first useful, beyond-classical computation of a real-world problem as the next milestone. The algorithms with proven speed-ups (factoring, unstructured search, simulating quantum systems) are narrow. Nothing in the CPU and GPU stack of this track changes because of today's quantum hardware. In an interview: say where it would sit (an accelerator behind a classical host), why error correction dominates the engineering, and that it is not a general speed-up.

---

## 10. What this means for your code

| Symptom | Likely cause | Try |
|---|---|---|
| Matrix engine at a few percent of peak with small batches | `M < P`: each weight used too few times; also memory-bound on weight loads | batch requests, enlarge `M` (merge sequences), keep weights resident, quantise |
| Layer with few channels uses the array badly | `K` or `N` far below `P`; padding to tile size | choose channel counts that are multiples of the tile width; fuse or pack small layers |
| Model "runs on the NPU" but is slow; the profile shows CPU segments between NPU segments | an unsupported operator or data type splits the graph | replace the operator, quantise the whole graph, check the supported-operator list |
| Offloaded function is slower than on the CPU | transfer plus launch cost exceed the work (section 8.3) | larger granularity, keep data resident, overlap copies, or do not offload |
| Firewall or NAT throughput is fine but connections per second are low, or CPU is high | short flows miss the fast path; the slow path dominates | measure hit rate and new-connection rate separately; tune session setup |
| Hardware offload counters show misses or flows stay in software | rule not offloadable, table full, fragments or RST/FIN returned to the normal path | check `[HW_OFFLOAD]` against `[OFFLOAD]` in conntrack, table size, which match fields the device supports |
| FPGA design misses timing | a long combinational path or poor routing | add pipeline registers, reduce fan-out, constrain the critical path, change the HLS pragma |
| FPGA design far larger than expected | multipliers built from LUTs, or memories from flip-flops | write or constrain the design to infer DSP and block-RAM blocks; read the utilisation report |
| Video or audio glitches when the NPU or GPU runs | DRAM bandwidth contention between blocks | use quality-of-service regulation, reduce traffic, size buffers for the worst case |
| Stale data after a DMA on a non-coherent accelerator | missing cache clean or invalidate | use the platform's DMA synchronisation calls around each transfer |

---

## 11. Mistakes that cost points

1. **Saying "accelerators are faster than CPUs."** They are more *efficient on one pattern*. The first question is which pattern and what happens when the work does not fit it.
2. **Quoting TOPS without precision, sparsity or bandwidth.** The first TPU's 91.75 TOPS could reach at most 15% on its MLPs (the roofline bound), because 34 GB/s of weight bandwidth sets the roof. TOPS without the memory bandwidth is half a number.
3. **Counting a multiply-add as one operation in one place and two in another.** 65,536 multiply-adds at 700 MHz is 45.9 T multiply-adds/s or 91.75 TOPS; the TPU paper's 1,350 ridge uses the first.
4. **Believing a systolic array is efficient at batch 1.** At `M = 1` the model of section 3.3 gives 0.8%.
5. **Ignoring padding.** `K = 147` and `N = 64` use 14% of a 256 × 256 array on the first ResNet layer. A bigger array is not automatically better.
6. **Treating a LUT count as a gate count**, or ignoring routing and the clock. An FPGA runs at hundreds of MHz and pays about 35× the area of the same logic as an ASIC.
7. **Claiming an FPGA beats a GPU on dense floating-point throughput.** It wins on latency, odd widths and flexibility, not on peak arithmetic.
8. **Building a multiplier from LUTs.** 114 LUTs against one DSP block in the section 7.4 listing.
9. **Judging an offload by its hit path.** Average cost is dominated by the miss path; a 99% hit rate still leaves half the cycles in the slow path.
10. **Mixing up NIC offloads, SmartNICs and DPUs.** Fixed offloads (checksum, RSS), a programmable flow-table datapath, and a card with its own Arm cores and operating system are three different things.
11. **Copying data to the accelerator and back for every small operation.** Residency is the difference between losing (0.17 flop/B SAXPY over PCIe) and winning by 33×.
12. **Quoting a vendor's tokens per second as a general result.** Ask for batch size, context length and precision; the wafer-scale numbers above are single-stream claims.
13. **Judging a dataflow chip by bandwidth alone.** 21 PB/s is for 44 GB of SRAM; a 70B model needs several wafers.
14. **Treating neuromorphic or quantum hardware as a drop-in accelerator.** Neither changes today's stack.

---

## 12. Questions you will be asked

**Q1. Why do accelerators exist?** Energy. In a 45 nm process a DRAM read costs 640 pJ against 3.7 pJ for an FP32 multiply and 0.2 pJ for an 8-bit one; a general core also spends a large multiple of the arithmetic on fetching, decoding, renaming and moving operands. Since Dennard scaling ended, a chip cannot run all its transistors flat out, so performance has to come from more work per joule. Accelerators get it by four tricks: many simple units driven by few instructions, operands kept close and reused (scratchpads, systolic flow), narrow arithmetic, and a dataflow fixed in advance.

**Q2. Explain a systolic array and why the TPU uses one.** A grid of multiply-accumulate cells, each holding a weight; activations move right, partial sums move down, one cell per cycle. Each activation fetched once is used by a whole row, so a `P × P` array does `P²` multiply-adds per cycle while reading `P` values: no register file or instruction stream per operation, short wires, high clock. A stream of `M` rows takes `M + 2P − 2` cycles (fill and drain), so it needs `M ≥ P` to be efficient; a matrix larger than the array is cut into tiles whose partial sums add in accumulators.

**Q3. Why are convolution and attention matrix multiplications, and what is left over?** A convolution becomes `(Ho·Wo) × (C·k·k)` times `(C·k·k) × F` once each receptive field is laid out as a row (im2col). Attention is `Q·Kᵀ`, a softmax, and `P·V`. The matrix engine does nearly all the multiply-adds (256 per exponential in attention for `d = 128`); the softmax, normalisation, activations and pooling are bandwidth-bound and go to a vector unit. Fusion applies the bias and activation while the result leaves the accumulators, saving a round trip to memory (1.6 MB for a 56 × 56 × 128 INT8 layer).

**Q4. Place the TPU's workloads on its roofline.** The ridge is `45.9e12 ÷ 34e9 ≈ 1,350` multiply-adds per weight byte. The MLPs (168 to 200) and LSTMs (64 to 96) sit far left, bounded at 5 to 15% of peak; the CNNs (1,750 and 2,888) are compute-bound. MLP and LSTM layers use each weight about once per batch element, so their intensity is the batch size. Faster weight memory (GDDR5, ridge about 250) is the fix; more multipliers would not help.

**Q5. A model is slow on an edge NPU despite a high TOPS rating. Why?** Check in this order: operator or data-type fallback splitting the graph into NPU and CPU segments (each cut costs a transfer and a sync: 87% extra on the example layer of section 4.3); memory bandwidth (the NPU is bandwidth-bound; quantise and fuse); array shape waste (few channels, small `M`); and the clock and power state. Peak TOPS assumes a perfectly fed array.

**Q6. Wafer-scale or dataflow chips against GPUs for LLM inference?** They remove the weight-bandwidth wall by keeping weights in SRAM (21 PB/s on a wafer against 3.35 TB/s of HBM: a sweep of memory in 2 µs against 24 ms), which gives low single-stream latency (vendor claims of hundreds of tokens per second for a 70B model). They pay in capacity (44 GB per wafer; hundreds of 220 MB chips for the same model), cost per byte, flexibility and compiler maturity. GPUs win on memory capacity, batch throughput per dollar and generality.

**Q7. What is a DPU and why use one?** A NIC that is also a computer: BlueField-3 has up to 16 Arm A78 cores, 16 GB of DDR5, 400 Gb/s ports, PCIe Gen5, and crypto, storage and flow-switching engines. It runs the infrastructure (virtual switch, firewall, storage client, telemetry) off the host, so the host's cores serve the application, and on a separate trust boundary that can stay in force when the host is compromised. AWS Nitro is the large-scale example: Nitro cards for networking and storage, a security chip for firmware, and a hypervisor with no networking stack.

**Q8. FPGA, GPU or ASIC for (a) microsecond-latency packet inspection, (b) a large ML inference service, (c) a protocol that changes yearly?** (a) FPGA: deterministic, pipelined, wire-speed, and reprogrammable, with an ASIC only if the volume justifies it. (b) GPU or a dedicated ML ASIC: dense arithmetic at high clocks and a mature software stack; an FPGA's peak arithmetic is too low. (c) FPGA (or a programmable network processor): a new bitstream is cheaper than a new chip, at the price of about 35× the area and 14× the dynamic power of an ASIC for the same logic.

**Q9. How do you decide whether to offload a function?** Compare `W/R_c` with `L + D/B + W/R_a`: the work per byte moved must exceed `R_c/B` (about 16 flop/B for a 1 Tflop/s CPU and a 63 GB/s link) and the work must outlast the launch cost. Then check residency (can the data stay put?), the fallback path and its frequency, overlap, and the stability of the function. For packet paths add the hit rate: average cost is dominated by the miss path.

**Q10. What is an FPGA and why is it slower and bigger than an ASIC?** A sea of configurable logic blocks (6-input LUTs, flip-flops, carry chains) joined by programmable routing, with hard multiplier and RAM blocks. A LUT is a memory read through a mux, so any `k`-input function fits; but every function carries its configuration memory and every wire passes through programmable switches. Measured at 90 nm: about 35× the area, 3 to 4× the delay and 14× the dynamic power for logic alone, falling to about 18× the area with hard blocks used.

**Q11. One line each on neuromorphic and quantum.** Neuromorphic: event-driven spiking neurons with memory beside the compute, so power follows activity; research chips (Loihi 2, a million neurons), niche for sparse always-on sensing. Quantum: an accelerator behind a classical host whose engineering is dominated by error correction (Willow showed errors halving as the code grew, with 105 qubits); no general speed-up and no effect on today's stack.

---

## 13. Problems

**How to use this section.** All four are solvable on paper; each says *Where* to check the answer and *How*. The checks use only Python or a calculator; nothing needs an accelerator. Do the paper part first, then run the check, and fix the paper answer if they disagree. Time: about 15 to 20 minutes each.

### Problem A (paper, then a script): size a systolic array

Use the cycle model of section 3.3: `cycles = T × max(M, P) + 2P − 2`, `T = ⌈K/P⌉ × ⌈N/P⌉`, utilisation = `M·K·N ÷ (cycles × P²)`.

(a) `C = A·B`, `A` is 2048 × 1024, `B` is 1024 × 512, on a 128 × 128 array: tiles, cycles, utilisation, time at 1 GHz against the ideal time.
(b) The same `B`, but `A` has only 8 rows: cycles and utilisation, and what you would change.
(c) A 3 × 3 convolution, 64 channels in and 64 out, a 56 × 56 output (stride 1, same-size output): the matrix shape `M × K × N`, the multiply-adds, and the utilisation on a 128 × 128 and a 256 × 256 array. Which is better per multiplier?
(d) How long does (c) take on the first TPU's array (256 × 256 at 700 MHz)?

*Where:* paper; check with the `cycles` and `utilisation` functions of section 3.3.
*How:* `K` for a convolution is `C_in × k × k`; `M` is the number of output pixels; take ceilings carefully.

### Problem B (paper): the TPU roofline

Use 65,536 multiply-adders, 700 MHz and 34 GB/s of weight bandwidth, counting a multiply-add as one operation.

(a) Peak in multiply-adds per second and in TOPS; the ridge point.
(b) The roofline bound, as a fraction of peak, for MLP1 (168 multiply-adds per weight byte), LSTM1 (96) and CNN1 (1,750).
(c) What weight bandwidth would make MLP1 compute-bound? How many times the 34 GB/s is that?
(d) If batching raised MLP0's intensity (200) by four, what fraction of peak would the roofline allow? Name one thing that limits how far you can batch in a service.

*Where:* paper and a calculator.
*How:* bound = `min(peak, intensity × bandwidth)`; the ridge is `peak ÷ bandwidth`.

### Problem C (paper, then a script): should it be offloaded?

CPU: 1 Tflop/s sustained FP32. Accelerator: 100 Tflop/s. Link: PCIe Gen5, 63 GB/s one way. Launch and synchronisation: 10 µs. All matrices are FP32 (4 bytes).

(a) For an `N × N` matrix multiplication with `A` and `B` going in and `C` coming out, give `T_cpu` and `T_acc` and find the smallest `N` for which the accelerator wins.
(b) The same when all three matrices already live in the accelerator's memory.
(c) `N = 512`, `B` stays resident across 50 calls, `A` goes in and `C` comes out on every call: the time per call and the speed-up over the CPU.
(d) SAXPY on 10⁸ floats (`y = a·x + y`, 12 bytes moved per element): the intensity, the CPU time (assume 100 GB/s of DRAM bandwidth) and the accelerator time including PCIe. Does it ever win? What if `x` and `y` are already in the accelerator's 3.35 TB/s HBM?

*Where:* paper; then a short loop in Python (try each `N` and print the first one where the accelerator wins).
*How:* `T_cpu = 2N³ / 1e12`; `T_acc = 10e-6 + bytes/63e9 + 2N³ / 100e12`; bytes = `12N²`, `0`, or `8N²`.

### Problem D (paper): packet budget and the fast path

Use 20 bytes of overhead per frame on the wire, 64-byte and 1,518-byte frames, and a 3 GHz core.

(a) For a 400 Gb/s link: packets per second and cycles available per packet on one core, for each frame size.
(b) A software fast path costs 150 cycles per packet (assumption). How many cores sustain line rate at each frame size?
(c) An offload engine handles a hit in 20 cycles and a miss goes to a 2,000-cycle slow path (assumptions). Give the average cycles per packet and the share of cycles spent on the slow path for hit rates of 90%, 99% and 99.9%. What hit rate gives an average of 30 cycles?
(d) A firewall sees 10,000 new connections per second and each connection carries 50 packets. Give the packets per second, the hit rate (only each connection's first packet misses), the cycles per second on each path, and the share spent on the slow path. What would you optimise?

*Where:* paper and a calculator.
*How:* packets/s = rate ÷ (wire bytes × 8); cycles per packet = 3e9 ÷ packets/s; average = `h × fast + (1 − h) × slow`.

---

## Answers

### Problem A

| | Tiles | Cycles | MACs | Utilisation | Time at 1 GHz (ideal) |
|---|---|---|---|---|---|
| (a) 2048 × 1024 × 512, `P = 128` | 8 × 4 = 32 | 32 × 2048 + 254 = 65,790 | 1,073,741,824 | **99.6%** | 65.8 µs (65.5 µs) |
| (b) 8 × 1024 × 512 | 32 | 32 × 128 + 254 = 4,350 | 4,194,304 | **5.9%** | 4.35 µs (0.26 µs) |
| (c) 3,136 × 576 × 64, `P = 128` | 5 × 1 = 5 | 5 × 3136 + 254 = 15,934 | 115,605,504 | **44.3%** | 15.9 µs (7.1 µs) |
| (c) same, `P = 256` | 3 × 1 = 3 | 3 × 3136 + 510 = 9,918 | 115,605,504 | **17.8%** | 9.9 µs (1.8 µs) |

(a) is the happy case: `K` and `N` are multiples of 128 and `M` is large, so utilisation is limited only by the 254 cycles of fill and drain. (b) with `M = 8 < P` each tile costs `P = 128` cycles (the weight load) instead of 8 cycles of work: the array idles 94% of the time. The fix is to batch: with `M = 128` the same time does 16 times the work. (The weights, 1024 × 512 bytes, also have to come from memory, so a small `M` is memory-bound as well.) (c) `K = 64 × 9 = 576`: on 128-wide tiles that is 4.5, rounded up to 5 tiles (90% filled), and `N = 64` fills half the columns, so about 45% at best, and 44.3% with the fill and drain. On the 256-wide array `K` needs 3 tiles (576 of 768) and `N = 64` fills a quarter of the columns: 17.8%. The 256 × 256 array is faster in cycles (9,918 against 15,934) but with four times the multipliers each is used 2.5 times less. (d) 9,918 cycles ÷ 700 MHz = **14.2 µs**.

### Problem B

(a) Peak = 65,536 × 700e6 = 45.9 T multiply-adds/s = 91.75 TOPS (two operations each). Ridge = 45.9e12 ÷ 34e9 = **1,349 ≈ 1,350** multiply-adds per weight byte.

(b) MLP1: `168 × 34e9 = 5.71 T/s`, **12.5%** of peak. LSTM1: `96 × 34e9 = 3.26 T/s`, **7.1%**. CNN1: `1,750 × 34e9 = 59.5 T/s`, above the 45.9 T peak, so it is compute-bound at **100%**.

(c) `45.9e12 ÷ 168 = 273 GB/s`, **8.0 times** the 34 GB/s (compare the paper's GDDR5 what-if, which reaches a ridge of 250 with about 184 GB/s: enough for MLP0 at 200, not for MLP1).

(d) Intensity 800 gives `800 × 34e9 = 27.2 T/s`, **59.3%** of peak. A latency target limits how far you can batch: requests wait for the batch to fill and the batch takes longer to run, and a user-facing service has a response-time budget.

### Problem C

(a) `T_cpu = 2N³ / 1e12`; `T_acc = 10e-6 + 12N² / 63e9 + 2N³ / 100e12`. The bandwidth condition alone (`N/6 > 15.9`) gives `N > 96` and the latency condition (`2N³ > 10 Mflop`) gives `N > 171`, but they act together: scanning `N` upward, the accelerator first wins at **N = 211** (at N = 128: CPU 4.2 µs against 13.2 µs; at N = 256: 33.6 µs against 22.8 µs).

(b) Resident data: `T_acc = 10e-6 + 2N³ / 100e12`, first win at **N = 172** (the latency condition). Keeping one operand resident (only `A` in and `C` out, `8N²` bytes) gives 196.

(c) `T_cpu = 2 × 512³ / 1e12 = 268.4 µs`. `T_acc = 10 µs + 8 × 512² / 63e9 (33.3 µs) + 2.7 µs = 46.0 µs`, so **5.8 times** faster per call. The first call also moves `B` (62.6 µs); amortised over 50 calls the weights are almost free.

(d) SAXPY: 2 flops per 12 bytes = **0.17 flop/B** against 15.9 needed. CPU: `12 × 10⁸ ÷ 100e9 = 12.0 ms`. Accelerator over PCIe: `10 µs + 1.2e9 ÷ 63e9 (19.05 ms) + 0.36 ms (HBM) = 19.4 ms`: it **loses**, and no faster accelerator helps because the time is the transfer. With `x` and `y` resident in HBM: `10 µs + 1.2e9 ÷ 3.35e12 = 0.37 ms`, **33 times** faster than the CPU. The operation did not change; where the data live did.

### Problem D

(a) Wire bytes are 84 and 1,538. At 400 Gb/s: 64 B → `400e9 ÷ (84 × 8) =` **595.2 M packets/s**, **5.0 cycles** per packet; 1,518 B → `400e9 ÷ (1538 × 8) =` **32.5 M packets/s**, **92.3 cycles** per packet.

(b) At 150 cycles per packet: 64 B needs `595.2e6 × 150 ÷ 3e9 =` 29.8, so **30 cores**; 1,518 B needs 1.63, so **2 cores**. Small packets are what break software forwarding.

(c) Average = `20h + 2000(1 − h)`:

| Hit rate | Average cycles | Slow-path share |
|---|---|---|
| 90% | 218 | 92% |
| 99% | 39.8 | 50% |
| 99.9% | 22.0 | 9% |

For an average of 30 cycles: `20h + 2000 − 2000h = 30` so `h = 1970 ÷ 1980 =` **99.49%**.

(d) Packets per second = 10,000 × 50 = **500,000**. Only the first packet of each connection misses: 10,000 misses per second, hit rate 490,000 ÷ 500,000 = **98%**. Cycles per second: slow path 10,000 × 2,000 = **20 M**; fast path 490,000 × 20 = **9.8 M**; the slow path is **67%** of the work although it handles 2% of the packets. Optimise connection setup (the slow path) and, if the hardware allows, cache or batch flow-entry installation; adding fast-path capacity would barely help.

---

## Cheat sheet

- **Why accelerators:** 45 nm energy, pJ: 8-bit add 0.03; 32-bit add 0.1; 8-bit multiply 0.2; FP32 add 0.9; FP32 multiply 3.7; 8 kB SRAM read 5; DRAM read 640 (173 FP32 multiplies). INT8 MAC with 32-bit accumulate 0.3 pJ against 4.6 for FP32: 15×. Dennard scaling ended, so more work per joule. Four tricks: many units / few instructions; operands near and reused; narrow arithmetic; fixed dataflow.
- **Matrices:** convolution = im2col GEMM `M = Ho·Wo`, `K = C·k·k`, `N = filters` (5× memory blow-up for 3 × 3); ResNet-50 conv1 = 12,544 × 147 × 64 = 118 M MACs. Attention = two GEMMs + softmax (256 MACs per exponential at `d = 128`). Leftovers (softmax, norm, activations) go to the vector unit. Fusion: apply bias and ReLU as results leave the accumulators.
- **Systolic array:** weight-stationary; activations right, partial sums down; stream of `M` rows takes `M + 2P − 2` cycles; `cycles ≈ T·max(M, P) + 2P − 2`, `T = ⌈K/P⌉⌈N/P⌉`; needs `M ≥ P`; padding waste when `K`, `N` are not multiples of `P`; batch-1 on a 128 array = 0.8%.
- **TPU v1:** 256 × 256 8-bit MACs, 700 MHz, 91.75 TOPS (45.9 T MAC/s), 24 MiB unified buffer, 4 MiB accumulators, 8 GiB DDR3 at 34 GB/s, 28 nm, 75 W TDP (40 W busy), PCIe coprocessor, about 12 CISC instructions (5 key). Ridge 1,350 MAC/weight byte; MLPs 168–200, LSTMs 64–96 (memory-bound, 5–15% of peak), CNNs 1,750–2,888 (compute-bound). Workload mix 61% MLP, 29% LSTM, 5% CNN. TPU v5p: 8 MXUs of 128 × 128, 459 TFLOP/s bf16, 95 GiB HBM at 2,765 GB/s, 8,960-chip 3D torus; ridge 166 flop/B (H100: 295). AMX: 8 tiles × 1 KB, INT8/BF16, needs `arch_prctl` permission.
- **NPUs:** same design in miniature; Ethos-U55 32–256 MACs, 6.4 GOPS to 0.5 TOPS depending on clock and process, compressed weights, DMA. Operator-support cliff: a fallback adds a transfer each way (87% on the example layer).
- **Dataflow chips:** WSE-3 44 GB SRAM, 21 PB/s, 900,000 cores, 125 PFLOPS (sweep time 2.1 µs against 24 ms for an H100); a 70B FP16 model needs 4 wafers. Groq: 220 MB SRAM per chip, statically scheduled, about 318 chips for 70B INT8. Win: single-stream latency; lose: capacity, cost per byte, flexibility.
- **Engines and DPUs:** fixed-function = descriptors in a ring + DMA; Linux flowtable: first packets slow path, later packets fast path (`[OFFLOAD]` software, `[HW_OFFLOAD]` hardware); average cost dominated by the miss path. Packet budget: wire bytes = frame + 20; 400 Gb/s at 64 B = 595 Mpps = 5 cycles at 3 GHz; at 1,518 B = 32.5 Mpps = 92 cycles. BlueField-3: up to 16 A78 cores, 16 GB DDR5, 400 Gb/s, PCIe Gen5. Nitro: cards for VPC, EBS, NVMe; security chip; minimal hypervisor.
- **FPGA:** LUT = 2^k-bit memory + mux (full adder = two LUT3 with INIT 0x96 and 0xE8); UltraScale CLB = 8 LUT6 + 16 flip-flops + carry logic; DSP48E2 27 × 18 multiplier, 48-bit accumulator. Yosys on 7-series: 32-bit adder = 32 LUT2 + 8 CARRY4; 8 × 8 multiplier = 114 LUTs or 1 DSP. Versus ASIC (90 nm): 35× area, 3.4× delay, 14× dynamic power (18× area with hard blocks). Catapult: +95% throughput or −29% tail latency for +10% power. Flow: RTL/HLS → synthesis → place and route → timing → bitstream.
- **Offload:** `T_acc = L + D/B + W/R_a` against `W/R_c`; need `W/D > R_c/B` (≈16 flop/B for 1 Tflop/s and 63 GB/s) and `W > L·R_c` (10 Mflop for 10 µs); GEMM break-even N = 211 (172 resident); SAXPY loses over PCIe, wins 33× if resident; Amdahl still applies.
- **SoCs:** shared DRAM (a 4K60 display = 2 GB/s), coherence (I/O-coherent or flush/invalidate), IOMMU/SMMU, power domains, buffer hand-off with fences.
- **Neuromorphic:** event-driven spiking, memory beside compute; Loihi 2 about 1 M neurons, 120 M synapses, research. **Quantum:** accelerator behind a classical host; error correction dominates; Willow 105 qubits, errors halving with code size; no general speed-up.
- **Everything here came from published papers, vendor documents, real synthesis output and short calculations; nothing was timed on an accelerator.**

**Next:** tutorial 25, Buses, PCIe and I/O mapping (system buses and arbitration, memory-mapped I/O and device registers, PCIe lanes, packets, BARs and enumeration, the IOMMU, SR-IOV, CXL in a paragraph). Say "next" when you are ready.
