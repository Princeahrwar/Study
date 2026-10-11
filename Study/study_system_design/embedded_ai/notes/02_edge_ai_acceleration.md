# 02 — Edge AI acceleration

> Prerequisite: `os_and_arch/02_cache_hierarchy.md`, `os_and_arch/05_simd_and_vectorization.md`.

## The constraint that defines edge inference

**Edge inference is memory-bound, not compute-bound.** Say this first in any
discussion and the rest follows.

Apply the roofline model. Arithmetic intensity is FLOPs per byte moved:

```
  Dense layer (matrix-vector):  ~2 FLOPs per weight byte read
                                 -> bandwidth-bound, always
  3x3 conv, many channels:      ~10-100 FLOPs per byte, with good tiling
                                 -> can be compute-bound IF the tiling is right
  Depthwise separable conv:     very low intensity by design
                                 -> almost always bandwidth-bound
```

A 30 TOPS NPU fed by 10 GB/s of memory bandwidth can only sustain 30 TOPS if the
workload achieves 3000 operations per byte. Almost nothing does. In practice the
accelerator idles waiting for weights, which is why **quantization is the single
highest-leverage optimisation** — it is a 4x bandwidth reduction, not merely a
compute reduction.

That reframing (quantization as bandwidth, not arithmetic) is the thing that
sounds like experience rather than reading.

## Quantization

```
  FP32 -> INT8:  4x smaller weights, 4x less bandwidth, integer MACs
                 typically <1% accuracy loss with proper calibration
  FP32 -> INT4:  8x smaller, needs quantization-aware training to hold accuracy
  FP16 / BF16:   2x smaller, minimal accuracy work, widely supported
```

The mechanics:

```
  real = scale * (quantized - zero_point)

  Per-tensor:   one scale for the whole tensor. Simple, less accurate.
  Per-channel:  one scale per output channel. Standard for weights, because
                channel dynamic ranges differ a lot.
  Symmetric:    zero_point = 0. Cheaper arithmetic; standard for weights.
  Asymmetric:   arbitrary zero_point. Better for activations, which are often
                one-sided after ReLU.
```

Two approaches, and knowing when each applies is the practical knowledge:

- **Post-training quantization (PTQ).** Calibrate with a few hundred
  representative samples to find activation ranges. Fast, no retraining. Usually
  sufficient for INT8.
- **Quantization-aware training (QAT).** Simulate quantization during training so
  the model learns to tolerate it. Needed for INT4, or when PTQ loses too much.

**Calibration data must be representative.** Calibrating on clean daylight images
and deploying at night produces a model that quantizes badly exactly where it
matters. This is a real and common failure.

## Graph-level optimisations

These happen before any kernel runs and often matter more:

```
  Operator fusion     Conv + BatchNorm + ReLU -> one kernel.
                      BN folds into the conv weights at build time — it is pure
                      arithmetic on the weights and costs nothing at runtime.
                      Avoids writing and re-reading the intermediate tensor,
                      which is the whole point: it is a BANDWIDTH optimisation.
  Constant folding    Evaluate static subgraphs once at build time.
  Layout transform    NCHW <-> NHWC to match what the hardware wants.
                      A mismatched layout inserts transposes everywhere.
  Memory planning     Reuse buffers across layers whose lifetimes do not overlap,
                      so peak memory is far below the sum of all tensors.
```

Fusion is the clearest example of the memory-bound theme: the win is not fewer
operations, it is fewer round trips to DRAM.

## ONNX Runtime

The portable path. One model, many backends.

```python
import onnxruntime as ort

# Execution providers are tried in order; ORT partitions the graph and assigns
# each subgraph to the first provider that supports it.
sess = ort.InferenceSession(
    "model.onnx",
    providers=[
        ("TensorrtExecutionProvider", {"trt_fp16_enable": True}),
        ("CUDAExecutionProvider", {}),
        "CPUExecutionProvider",              # always last, always works
    ],
)

# Check what actually happened — this is the step people skip:
print(sess.get_providers())
```

**Graph partitioning is the thing to understand.** If one operator in the middle
is unsupported by your accelerator, the graph splits into three parts and the
tensors bounce between devices twice. A single unsupported op can cost more than
everything the accelerator saved. The diagnostic is to dump the partitioning and
count the subgraphs — if there are more than a handful, fix the model rather than
tuning the kernels.

## TensorRT

Nvidia's ahead-of-time compiler. The key distinction:

```
  BUILD TIME (offline, slow: seconds to minutes)
     parse -> fuse layers -> select kernel/tactic per layer by BENCHMARKING
           -> apply precision (FP16/INT8) -> serialise a .engine

  RUNTIME (fast)
     deserialise the engine -> allocate -> execute
```

Consequences you must know:

- **Engines are not portable.** They are tied to the GPU architecture, TensorRT
  version and often the exact driver. Build on the target, or build per target in
  CI. This bites people constantly in deployment.
- **Build time is significant**, so never build at application startup. Ship the
  serialised engine.
- Tactic selection literally benchmarks alternative kernels on the real device,
  which is why the engine is fast and why it is device-specific.

Interview-relevant: being able to explain *why* engines are non-portable — because
tactic selection measured this specific device — rather than just stating it.

## Making the accelerator actually fast

The failures are almost never in the model:

1. **The copy.** Host-to-device transfer often costs more than inference. Use
   pinned memory, or better, a unified/shared memory architecture where the NPU
   reads the same physical buffer the camera wrote. This is `kernel_drivers/04`
   applied to AI.
2. **Batch size 1.** Edge inference is usually single-sample, which underutilises
   wide hardware. Batching adds latency; the trade must be made explicitly.
3. **Preprocessing on the CPU.** Resize, normalise and layout conversion can
   exceed inference time. Fuse them into the graph or do them on the accelerator.
4. **Synchronisation.** Blocking on completion serialises the pipeline. Overlap
   the next frame's preprocessing with the current inference, with
   double-buffering — the same pipelining idea as everywhere else in this tree.
5. **Thermal throttling.** Sustained inference on a fanless AP heats up and the
   clock drops. **Benchmark sustained, not burst.** A 10-minute run tells you
   what a 10-second run cannot, and quoting burst numbers for a fanless device is
   a mistake interviewers notice.

## Measuring honestly

```
  Latency p50 / p99 / p99.9     not mean
  Sustained throughput          over 10+ minutes, at temperature
  Memory: peak and steady       peak decides whether it fits at all
  Power / energy per inference  the real currency on a PoE-powered AP
  Accuracy after quantization   on a held-out set, not the calibration set
```

Power deserves a mention in your context: a PoE-powered AP has a hard power
budget shared with the radios. An inference workload that pushes the device past
its budget does not just run slowly — it can affect radio performance. That
system-level framing is a strong thing to raise.

## Interview angles

**"How do you speed up inference on an edge device?"**
Lead with memory-bound and the roofline. Then quantization as a bandwidth
optimisation, then fusion, then the copy/preprocessing/thermal list.

**"What does quantization cost you?"**
Accuracy, and the amount depends on PTQ vs QAT and on calibration
representativeness. Give the <1% figure for well-calibrated INT8.

**"Why is a TensorRT engine not portable?"**
Tactic selection benchmarked that specific device at build time.

**"Your NPU shows 10% utilisation. Why?"**
Data movement, unsupported ops splitting the graph, batch size 1, or CPU
preprocessing. Name the diagnostic for each.

**"Where would you put the model — device or cloud?"**
Latency budget, bandwidth cost, privacy, and connectivity. For a Wi-Fi AP doing
per-packet classification, the cloud round trip alone exceeds the budget by
orders of magnitude — say the arithmetic.

## Exercises

1. Take a small vision model, quantize it to INT8 with PTQ, and measure accuracy,
   latency and model size against FP32. Then deliberately calibrate on
   unrepresentative data and measure the accuracy loss.
2. Compute the arithmetic intensity of three layers in that model and predict
   which are bandwidth-bound. Verify with a profiler.
3. Run it under ONNX Runtime with two execution providers and dump the graph
   partitioning. Then remove one unsupported op and measure the difference.
4. Measure host-to-device copy time against inference time. Then implement
   double-buffering and measure the pipeline throughput improvement.
5. Run sustained inference for 15 minutes on a fanless device while logging
   clock frequency and temperature. Plot throughput over time. The gap between
   the first and last minute is the number nobody reports.
