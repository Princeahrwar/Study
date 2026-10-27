# CO 05: Computer Arithmetic and the ALU

*Part of the [Computer Organization plan](00_plan.md) · Core for performance, hardware and low-level roles, skim otherwise · Time: about 4 hours (2 reading, 2 problems) · You need: [tutorial 02](02_data_representation.md) (two's complement, IEEE-754) and [tutorial 03](03_boolean_logic_and_combinational_circuits.md) (full adder, first ALU)*

By the end of this tutorial you can:

- say what an add, multiply, divide, square root and fused multiply-add cost in cycles, and which of them to keep out of a hot loop;
- explain why a 64-bit add takes one cycle although a ripple chain would take over a hundred gate delays, and compare carry-lookahead, parallel-prefix, carry-skip, carry-select and carry-save adders;
- multiply the way hardware does (shift-and-add, Booth radix-2 and radix-4) and explain what a Wallace or Dadda tree adds to it;
- explain restoring, SRT and Newton-Raphson division, and how a compiler replaces a division by a constant with a multiplication;
- walk through floating-point add and multiply in hardware, with guard, round and sticky bits, and explain subnormal slowdowns, fused multiply-add and why `max` differs between x86 and Arm;
- derive the flags Z, N, C and V and the signed and unsigned comparisons from them, and say how x86, Arm and RISC-V differ.

**Where the numbers come from.** The cycle counts and the subnormal, `max` and `fma` results are **measured** on the virtual machine used in tutorials 14 to 31 (an Intel Xeon of the Emerald Rapids generation, 2 vCPUs, 3.1 to 3.3 GHz, derived below). Compiler output is from GCC 13 for x86-64 and AArch64 and clang 18 for RISC-V; the Arm and RISC-V code was only compiled, never timed. Every algorithm in sections 2 to 5 was run and compared with exact arithmetic (exhaustively for small widths, with millions of random inputs for 32 and 64 bits); the gate-level counts in section 2 are counts from a model (one gate is one unit), not timing from silicon. Facts I did not check myself are marked *standard*.

---

## 1. What an operation costs

Start with the numbers, because they decide how you write code. Two figures matter for each operation. **Latency** is how long one result takes when the next operation needs it. **Throughput** is how often a new operation can start when they are independent. I measured both with one program (listed at the end of this tutorial): a chain of dependent operations, and eight independent chains. A dependent integer add takes one cycle by definition, which also gives the clock: 0.30 to 0.32 ns, so 3.1 to 3.3 GHz depending on the run.

| Operation | Latency (cycles) | Throughput (cycles per operation) | Section |
|---|---|---|---|
| integer add, subtract, logic, shift | 1 | 0.25 (four per cycle) | 2 |
| integer multiply (`imul r64`) | 3 | 1 | 3 |
| 64 × 64 to 128-bit multiply, high half (`mul`) | 4 | 1 | 3 |
| `popcnt`, `lzcnt` | 3 | 1 | 6.3 |
| integer divide, 32-bit | 11 to 12 | 6 | 4 |
| integer divide, 64-bit | 14 to 15 | 10 | 4 |
| floating-point add (`addsd`) | 2 | 0.5 (two per cycle) | 5 |
| floating-point multiply | 4 | 0.5 | 5 |
| fused multiply-add | 4 | 0.5 | 5.6 |
| floating-point divide, `float` / `double` | 11 / 14 to 15 | 3 / 4 | 4.2 |
| floating-point square root, `float` / `double` | 13 / 19 | 3 / 6 | 4.6 |
| a floating-point operation that needs the **subnormal assist** | about 130 | not overlapped | 5.5 |

*Measured.* Each row repeated within about 10 % over three runs (the table gives the typical value; the integer divide rows include one extra cycle for the add that keeps the dividend large and the square-root rows two for the floating-point add that keeps the operand varied; I subtracted them).

Read the table three ways.

1. **Division is an order of magnitude more expensive than multiplication, and it is not pipelined.** A 64-bit divide occupies the divider for about 10 cycles, so two independent divides do not overlap much. A multiply takes 3 cycles but a new one can start every cycle.
2. **Latency and throughput tell you how many independent chains you need.** To keep the machine busy you need `latency × throughput` operations in flight: 2 × 2 = 4 for floating-point add, 4 × 2 = 8 for multiply and FMA. This is the reason a floating-point sum with one accumulator runs at a quarter of the speed of one with four or eight ([tutorial 21](21_simd_and_vector_processing.md) section 6.2 measured it).
3. **Everything above is a design choice.** An adder is 1 cycle because it is built as a log-depth tree (section 2). A multiplier is 3 cycles because it is a Booth encoder, a tree of compressors and a final adder, pipelined (section 3). A divider is slow because each quotient digit depends on the previous remainder (section 4). The rest of the tutorial explains why each row has the value it has.

The same table on a different core differs in the details (a small core may take 3 cycles for an FP add or 20 or more for a divide, *standard*), but the *ordering* does not: add < multiply < divide, and floating-point sits one pipeline stage or two above integer.

---

## 2. Adders: how a 64-bit add fits in one cycle

[Tutorial 03](03_boolean_logic_and_combinational_circuits.md) section 8 built the full adder, the ripple-carry adder and the generate/propagate idea. Recap in three lines: a full adder computes `Sum = A ^ B ^ Cin` and `Cout = A·B + Cin·(A ^ B)`; chaining *n* of them makes the carry travel through every stage, about 2 gate delays per stage, so 2n in total (128 for 64 bits); and `G = A·B` (this bit makes a carry by itself), `P = A ^ B` (this bit passes a carry on). A clock cycle at 3 GHz holds on the order of 20 to 30 simple gate delays (rough, depends on the process), and the add also has to fit in the loop that feeds its result back to the next instruction. So the carry must not ripple.

### 2.1 The carry is a prefix computation

Group two neighbouring bit positions. The pair behaves like one bigger "bit" with its own generate and propagate:

```text
(G, P) for the group (hi, lo):   G = G_hi + P_hi · G_lo        P = P_hi · P_lo
```

Read it: the group makes a carry if the high part makes one, or if the high part passes on a carry that the low part made. Write this as an operator, `(G_hi, P_hi) ∘ (G_lo, P_lo)`. The operator is **associative**: you may combine bits in any grouping and get the same answer, as with `+` on numbers. The carry into bit *i* + 1 is just the `G` of the group covering bits *i* down to 0, so computing all carries is computing **all prefixes** of the sequence (G₀,P₀), (G₁,P₁), ... under `∘`. That is the *parallel prefix* problem, and because `∘` is associative it can be solved by a tree of depth log₂ n instead of a chain of depth n.

A worked 8-bit example, chosen so that the carry travels the whole way: `10110111 + 01001010` (183 + 74 = 257). Bit 0 is on the right.

```text
bit                                                       7 6 5 4 3 2 1 0
a                                                         1 0 1 1 0 1 1 1
b                                                         0 1 0 0 1 0 1 0
G = a AND b                                               0 0 0 0 0 0 1 0
P = a XOR b                                               1 1 1 1 1 1 0 1
G after level 1 (combine with the span 1 to the right)    0 0 0 0 0 1 1 0
G after level 2 (combine with the span 2 to the right)    0 0 0 1 1 1 1 0
G after level 3 (combine with the span 4 to the right)    1 1 1 1 1 1 1 0
carry INTO bit i (the G of everything below bit i)        1 1 1 1 1 1 0 0
sum bit = P xor carry-in                                  0 0 0 0 0 0 0 1
```

The answer is carry-out 1 and sum bits `0000 0001`, which is 257. The rows after `P` show `G` only; the group `P` values are combined by AND at the same time. Three levels instead of seven, and each level is one AND-OR gate. This layout, where each position combines with the one `d` places to the right, with `d` = 1, 2, 4, is the **Kogge-Stone** network. I checked it, with the two other networks below, against ordinary addition for all 65,536 pairs of 8-bit inputs and 20,000 random 64-bit pairs.

### 2.2 The family of prefix adders

All prefix adders compute the same carries; they differ in how they spend wires, gates and levels. Counts for *n* = 64 from my model (one combine cell is one AND-OR gate pair):

| Network | Levels | Combine cells | Fan-out of a cell | Character |
|---|---|---|---|---|
| ripple (for comparison) | 63 | 63 | 1 | smallest, slowest |
| **Kogge-Stone** | 6 | 321 | 2 | fastest and the most wiring: long wires double at every level |
| **Sklansky** | 6 | 192 | up to 32 | fewer cells, but one output feeds many cells, which slows a real gate |
| **Brent-Kung** | 11 | 120 | 2 | fewest cells and wires, nearly twice the depth |
| Han-Carlson and other hybrids | 7 | roughly half of Kogge-Stone's | 2 | one more level than Kogge-Stone for far fewer cells (*standard*) |

General formulas (I verified them for 8 to 64 bits): Kogge-Stone has log₂ n levels and n log₂ n − n + 1 cells; Sklansky log₂ n levels and (n/2) log₂ n cells; Brent-Kung 2 log₂ n − 1 levels and 2n − 2 − log₂ n cells. The pattern to remember: **depth against area against fan-out, pick two.** Fast cores use Kogge-Stone-like or hybrid adders in the integer ALU, where each picosecond counts; low-power cores use Brent-Kung-like ones.

### 2.3 Older and simpler schemes

You will meet these in textbooks and in small designs:

- **Carry-lookahead (CLA).** The same idea in groups of four: each 4-bit group computes a group `G` and `P`, and a second-level lookahead unit combines groups. A 16-bit adder is two levels of 4-bit lookahead; a 64-bit adder is three. It is the prefix adder with a fixed radix of 4, and the name textbooks use for it.
- **Carry-skip.** Ripple inside a block of *k* bits, but a block whose bits all *propagate* (`P` = 1 everywhere) lets the carry skip over it with one multiplexer. The worst case is about 2k + n/k stages, best for k near √(n/2), so the delay grows like √n (*standard*).
- **Carry-select.** Compute each block twice, once assuming carry-in 0 and once assuming 1, and pick with a multiplexer when the real carry arrives. Delay again about √n for equal blocks, at twice the adder area. It is the standard trick in FPGAs and small cores.
- **Dedicated carry chains.** An FPGA does not build prefix adders out of LUTs. It has a hard-wired fast carry path next to each LUT, so `a + b` in Verilog uses it automatically ([tutorial 24](24_accelerators_and_fpgas.md) section 7).

### 2.4 Carry-save addition: postpone the carry

Sometimes you must add three or more numbers, and only the final total is needed. Adding them one pair at a time pays a full carry propagation each time. A **carry-save adder (CSA)**, also called a **3:2 compressor**, avoids that. It is a row of independent full adders that takes three numbers and produces two: a *sum* word (the XOR of the three bits in each column) and a *carry* word (the majority of the three bits, shifted left one column). No carry travels sideways, so the delay is **one full adder, whatever the width**.

```text
  0101   (5)
+ 0110   (6)
+ 0111   (7)
------
  00100  sum bits   (bitwise XOR of the three)
 01110   carry bits (bitwise majority, shifted left one place)
------
  10010  (18)   one ordinary add finishes the job: 00100 + 01110
```

Two numbers out for three in; feed those two and another number to the next CSA and repeat; add the last two with one fast prefix adder. This is the engine of the multiplier in section 3 and of any multiply-accumulate unit.

### 2.5 In a real core

The integer add in the measured core takes **one cycle** for 64 bits, and the core can start about four per cycle. That cycle includes the adder, the **bypass** network that hands the result straight to the next instruction ([tutorial 09](09_pipelining_and_hazards.md) section 6.2), and the flag logic. The same adder is reused for subtraction (invert B, carry-in 1), for comparisons (a subtraction whose result is thrown away), for address arithmetic (the address-generation unit has its own small adders; `lea` is "an add that is not an add instruction"), and for branch targets. The program counter's `+4` is a simpler *incrementer*, because one operand is a constant.

---

## 3. Multiplication

### 3.1 Shift and add

A binary product is the primary-school method with no multiplication table: each bit of the multiplier selects either the multiplicand (shifted into place) or zero.

```text
        1101   (13, multiplicand M)
      x 1011   (11, multiplier Q)
      ------
        1101   Q0 = 1: add M
       1101    Q1 = 1: add M << 1
      0000     Q2 = 0: add nothing
     1101      Q3 = 1: add M << 3
   ----------
   10001111    (143)
```

The small, slow hardware does this with **one adder and n cycles**: a product register holds the running sum `A` on the left and the multiplier `Q` on the right. Each cycle: if `Q0` is 1, add `M` into `A`; then shift the pair `A:Q` one place right (the shifted-out bit of `A` becomes the top of `Q`, which frees one multiplier bit per step).

| Step | `A` (5 bits) | `Q` | Action |
|---|---|---|---|
| start | 00000 | 1011 | `M` = 1101 |
| 1 | 00110 | 1101 | `Q0` = 1: `A` += `M` (01101), shift right |
| 2 | 01001 | 1110 | `Q0` = 1: `A` += `M`, shift right |
| 3 | 00100 | 1111 | `Q0` = 0: no add, shift right |
| 4 | 01000 | 1111 | `Q0` = 1: `A` += `M`, shift right |

`A:Q` = 1000 1111 = 143. Small microcontrollers do it this way (a Cortex-M0 can be built with a 1-cycle or a 32-cycle multiplier, *standard*), and software without a multiply instruction runs this loop. A big core cannot afford *n* cycles.

### 3.2 Doing all the rows at once

The combinational version makes all *n* partial products at once (an AND gate for every pair of bits: n² gates) and adds them. Adding *n* rows one after another with ripple adders gives the **array multiplier**: regular, easy to lay out, but the delay grows linearly in *n*. The improvement is to reduce the pile of rows with carry-save adders in a tree and do a single carry-propagate add at the end.

### 3.3 Signed multiplication

For an *n*-bit by *n*-bit multiply the product has 2n bits. Two facts about two's complement save a lot of confusion:

- **The low n bits of the product are the same for signed and unsigned operands.** (0xFFFFFFFF × 0xFFFFFFFF: unsigned `0xFFFFFFFE00000001`, signed (−1) × (−1) = `0x0000000000000001`; the low 32 bits are both `0x00000001`.) That is why x86 has a single two- and three-operand `imul` for `int` and `unsigned`, and why RISC-V `mul` returns only the low half.
- **The high half differs**, so a widening multiply needs a signed and an unsigned form: x86 `mul` and `imul` (one-operand), Arm `umull`/`smull` and `umulh`/`smulh`, RISC-V `mulhu`, `mulh` and `mulhsu`. In C you ask for it with a cast: `(int64_t)a * b`.

### 3.4 Booth recoding

A run of 1s in the multiplier, such as `0111100` = 60, would cost four additions. But 60 = 64 − 4, two operations: one add and one subtract. **Booth's idea** is to scan the multiplier and recode it so that a run of ones becomes "subtract at the start of the run, add just after its end". Look at each bit together with the bit to its right (a 0 is placed to the right of bit 0):

| Bit pair (y_i, y_i−1) | Digit | Meaning |
|---|---|---|
| 0 0 | 0 | middle of a run of zeros |
| 0 1 | **+1** | end of a run of ones: add M·2^i |
| 1 0 | **−1** | start of a run of ones: subtract M·2^i |
| 1 1 | 0 | middle of a run of ones |

Example 7 × (−3), with the 4-bit multiplier `1101` (−3), bit 0 first:

```text
bit 0: pair (1,0) -> -1 -> -7
bit 1: pair (0,1) -> +1 -> +14
bit 2: pair (1,0) -> -1 -> -28
bit 3: pair (1,1) ->  0 ->   0          total: -7 + 14 - 28 = -21  (= 7 x -3)
```

It handles negative multipliers with no special case, because the digits are computed straight from the two's-complement bits (the top bit has weight −2^(n−1), which is what the digit formula assumes).

**A common misconception: Booth does not make hardware skip the zeros.** The hardware has a fixed number of rows whatever the data, so radix-2 Booth gains nothing in time. The useful version is **radix-4 (modified) Booth**, which looks at overlapping groups of *three* bits and produces one digit per *two* multiplier bits, from {−2, −1, 0, +1, +2}:

| y_2i+1 y_2i y_2i−1 | 000 | 001 | 010 | 011 | 100 | 101 | 110 | 111 |
|---|---|---|---|---|---|---|---|---|
| Digit (weight 4^i) | 0 | +1 | +1 | +2 | −2 | −1 | −1 | 0 |

Each digit needs a partial product that is 0, ±M or ±2M: a shift and a conditional invert (a negative one is the inverted bits plus a 1 that is fed in as a correction bit). That is a small multiplexer per bit. The multiplier now has **n/2 rows instead of n**, so the tree below it has half the work. Example: 60 = `00111100`; its radix-4 digits, low pair first, are 0, −1, 0, +1, meaning 0·1 − 1·4 + 0·16 + 1·64 = 60: only two non-zero partial products of the four. (Radix-8 would halve again but needs 3M, a carry-propagate add on the critical path, so it is rarely used.) I checked both recodings against ordinary multiplication for all 65,536 pairs of signed 8-bit numbers.

### 3.5 Wallace and Dadda trees

Take the (up to) n/2 + 1 partial-product rows and reduce them with 3:2 compressors: every three rows become two, in parallel across the whole width. Repeat until two rows remain, then add them with a fast adder from section 2.2.

```text
 rows:  64 -> 43 -> 29 -> 20 -> 14 -> 10 -> 7 -> 5 -> 4 -> 3 -> 2         10 stages (plain 64 x 64)
        33 -> 22 -> 15 -> 10 -> 7 -> 5 -> 4 -> 3 -> 2                      8 stages (64 x 64 with radix-4 Booth)
```

The depth grows like log₁.₅ n. For widths 8, 16, 32, 64 the plain AND-array needs 4, 6, 8, 10 stages; radix-4 Booth needs 3, 4, 6, 8. (Counts from my model, checked by simulation.)

- **Wallace tree:** compress as early as possible. Fewest stages but more adders.
- **Dadda tree:** compress as late as possible, only as much as the next stage requires. Same number of stages, fewer adders: an *N* × *N* array needs N² − 4N + 3 full adders and N − 1 half adders (35 and 7 for 8 × 8; my greedy Wallace variant used 36 full and 25 half adders).
- **4:2 compressors** (two full adders chained so that the carry does not depend on the other's output) reduce four rows to two with three XOR delays instead of four, and give a more regular layout. Most real multipliers are built from them.

Put the parts together and you have the whole multiplier:

```text
 multiplier --> Booth encoders --+
                                 +--> partial products (n/2 rows) --> compressor tree --> final prefix adder --> product
 multiplicand --> select 0,±M,±2M +
   (stage 1: encode+select)         (stage 2: tree)                      (stage 3: carry-propagate add)
```

Three pipeline stages: that is the measured 3-cycle latency of `imul`, and because each stage is separate hardware a new multiply can enter every cycle (throughput 1). The high half of the 128-bit result of `mul` settles one cycle later (measured 4). A floating-point multiply uses the same tree for the 24 or 53-bit significands, plus exponent logic and rounding (section 5), which is why it takes 4.

### 3.6 Multiplying by a constant

If one operand is a compile-time constant, the compiler does not need a tree. It uses the cheap one-cycle operations (shift, add, `lea`, which on x86 adds `base + index × 1, 2, 4 or 8` in one instruction). From the compiler's output for `x * 10`:

```text
x86-64:     lea eax, [rdi+rdi*4]     ; x + 4x = 5x
            add eax, eax             ; 10x
AArch64:    add w0, w0, w0, lsl 2    ; 5x
            lsl w0, w0, 1            ; 10x
```

Two single-cycle instructions (2 cycles of latency) against one `imul` (3). The compiler weighs that against the number of set bits in the constant, and uses `imul` when the shift-add sequence would be longer than about three instructions. You rarely need to do this by hand; knowing it exists explains why `x * 10`, `x * 5` and `x * 8 + 3` never show up as multiplications in a profile.

---

## 4. Division

### 4.1 Why division is a loop

Multiplication has independent parts (every partial product exists at once), so a tree can add them in parallel. Division does not: each quotient bit depends on the remainder left by the previous bit. That one dependence makes dividers slow, and it explains the table in section 1.

Binary long division is "shift the next dividend bit into the remainder; if the remainder is at least the divisor, subtract it and write a 1 in the quotient, otherwise write a 0". With only two outcomes per step it is easy to build. **Restoring division** does it literally: subtract, and if the result is negative, put the old value back (restore). 13 ÷ 3 in 4 bits (`1101` ÷ `0011`):

| Step | Remainder after shifting in the next dividend bit | Compare with 3 | New remainder | Quotient bit |
|---|---|---|---|---|
| 1 | 0 → 1 (bit 3 of 1101 is 1) | 1 < 3 | 1 | 0 |
| 2 | 1 → 11 = 3 (bit 2 is 1) | 3 ≥ 3: subtract | 0 | 1 |
| 3 | 0 → 0 (bit 1 is 0) | 0 < 3 | 0 | 0 |
| 4 | 0 → 1 (bit 0 is 1) | 1 < 3 | 1 | 0 |

Quotient `0100` = 4, remainder 1: 13 = 4 × 3 + 1. Four steps for four bits, so *n* steps for *n* bits.

**Non-restoring division** removes the restore. Suppose a step ends with the negative value `r' = r - d`, where `r` is the shifted remainder. Restoring would put `r` back, double it for the next step and subtract `d`, giving `2r - d`. Non-restoring skips the put-back: it doubles `r'` and **adds** `d`, giving `2(r - d) + d = 2r - d`, the same number from one operation instead of two. The rule is therefore: if the remainder is negative, add the divisor in the next step; if it is not, subtract it. Every step does exactly one add or subtract, there is no restore multiplexer, and one correction is needed at the end if the final remainder is negative. Both versions return the same answers as `/` and `%`; I checked all 65,280 divisions of 8-bit numbers. Either way each step needs a **full carry-propagate add** (a prefix adder, section 2) before the next step can start, which is slow.

### 4.2 SRT division: guess the digit, allow a correction

The speed-up comes from *not* waiting for the exact comparison. **SRT division** (named after Sweeney, Robertson and Tocher) allows the quotient digit to be wrong in a controlled way by using a **redundant digit set**: in radix 2 the digits are {−1, 0, +1} instead of {0, 1}. A guess that was a little too big is repaired by a −1 in a later position. The benefit is that the guess needs only the **top three bits** of the remainder, not the whole value, so the remainder can be kept in carry-save form (section 2.4) and the loop contains one compressor delay and a tiny table instead of a full adder.

With the divisor normalised into [1/2, 1) (shifted left until its top bit is 1), the radix-2 rule is the same for every divisor:

```text
r' = 2·r                      (shift the remainder)
digit = +1  if r' >= +1/2        r = r' - D
digit =  0  if -1/2 <= r' < 1/2   r = r'
digit = -1  if r' < -1/2         r = r' + D
```

An example: N = 5/8 divided by D = 3/4, true quotient 5/6 = 0.8333.

| Step | 2r | digit | new r = 2r − digit · D | quotient so far |
|---|---|---|---|---|
| 1 | 5/4 | +1 | 1/2 | 0.5 |
| 2 | 1 | +1 | 1/4 | 0.75 |
| 3 | 1/2 | +1 | −1/4 | 0.875 |
| 4 | −1/2 | 0 | −1/2 | 0.875 |
| 5 | −1 | −1 | −1/4 | 0.84375 |
| 6 | −1/2 | 0 | −1/2 | 0.84375 |
| 7 | −1 | −1 | −1/4 | 0.8359375 |
| 8 | −1/2 | 0 | −1/2 | 0.8359375 |

The digit string is `1 1 1 0 −1 0 −1 0`. To turn it into an ordinary binary number, write the +1 positions as one binary word and the −1 positions as another and subtract: `11100000 − 00001010` (as fractions of 2⁻⁸), which is 0.8359375. The final remainder is negative, which tells the hardware the true quotient is slightly smaller (5/6 = 0.8333, within one unit of the last place, 2⁻⁸ = 0.0039); a final step corrects it when an exact quotient is needed. I checked the identity (quotient × divisor + remainder = dividend, with the remainder staying bounded by the divisor) for every 8-bit pair with dividend smaller than divisor.

Higher radices retire more bits per step. **Radix-4 SRT** uses digits {−2, ..., +2}, two quotient bits per step, with a small lookup table (a few bits of divisor × a few bits of remainder → digit). Real dividers often chain two such stages in one cycle (radix 16, *standard*). Intel does not publish the design, but a 53-bit `double` quotient in about 14 to 15 cycles (section 1) is consistent with roughly four quotient bits per cycle. The lookup table is the part verification teams worry about: an entry that is wrong for a rare combination of divisor bits yields a wrong quotient only for rare operands, which is hard to find with ordinary tests.

Two consequences you can see in the measurements. First, a 64-bit integer divide (14 cycles) costs about 3 cycles more than a 32-bit one (11 to 12): more bits to retire. Second, on this core the latency did not depend on the operands (the program's "small operands" row, dividends around 100, took the same time as dividends near 2⁶²); older and smaller cores *terminate early* when the quotient is short, so their divide time depends on the data (*standard*).

### 4.3 Division by multiplication: Newton-Raphson

A multiplier already exists, so another way to divide is to compute the reciprocal of the divisor with multiplications only and multiply by the dividend. To find *x* = 1/*d*, apply Newton's method to f(x) = 1/x − d:

```text
x_next = x · (2 − d · x)            two multiplies and a subtract per step
```

If x = (1/d)(1 − e), where e is the relative error, then x_next = (1/d)(1 − e²): **the error is squared, so the number of correct bits doubles each step.** For d = 0.75 (so 1/d = 1.3333...) starting from the usual linear guess `x0 = 48/17 − (32/17)·d`:

| Step | x | Correct bits |
|---|---|---|
| 0 | 1.4117647 | 4.1 |
| 1 | 1.3287197 | 8.2 |
| 2 | 1.3333174 | 16.3 |
| 3 | 1.3333333331 | 32.7 |
| 4 | 1.3333333333333 (a double) | 53 or more |

A `float` (24 bits) needs three steps from this start, or one step from a 12-bit table lookup. The x86 instruction `rcpps` returns such a 12-bit approximation of 1/x for four floats at once (*standard*); one Newton step gives about 23 bits, close enough for graphics and signal processing, and it is used when throughput matters more than the last bit. **Goldschmidt's variant** multiplies numerator and divisor by the same factor in each step until the divisor reaches 1; the two multiplies are independent, so they overlap in a pipelined multiplier. GPUs and some CPUs divide this way, finishing with a fused multiply-add (section 5.6) to correct the last bit so that the quotient is the correctly rounded one (*standard*).

For your own code: if one divisor is used for many divisions (`for (...) y[i] = x[i] / d`), compute `r = 1.0f / d` once and multiply. The compiler is allowed to do it only with `-ffast-math` or `-freciprocal-math`, because `x * (1/d)` is not always the same last bit as `x / d`. Whether you can accept that is your decision.

### 4.4 Dividing by a constant: the compiler's trick

When the divisor is known at compile time the compiler does not emit `div` at all. It multiplies by a precomputed "magic number" and shifts. The idea: x / d = x · (1/d), and 1/d can be approximated by a fixed-point fraction m / 2ᵏ. Choose

```text
m = ceil(2^k / d),     e = m·d - 2^k   (0 <= e < d: how much too big m is)
x · m / 2^k  =  x/d  +  x·e / (d · 2^k)
```

The unwanted term `x·e / (d·2ᵏ)` is smaller than 1/d, and so does not change the integer part, whenever `x · e < 2ᵏ`. Take d = 10 and 32-bit unsigned x, k = 35: m = ceil(2³⁵/10) = 3,435,973,837 = `0xCCCCCCCD`, and e = 10 · m − 2³⁵ = 2. Then x · e < 2³³ < 2³⁵ for every x < 2³². So `x / 10 == (x * 0xCCCCCCCD) >> 35` for **every** 32-bit x. I tested all 4,294,967,296 values:

```c
/* Division by a constant becomes a multiply: check the 32-bit "divide by 10" trick for every input.   build: gcc -O2 magic.c -o magic */
#include <stdio.h>
#include <stdint.h>

int main(void) {
    const uint64_t m = 0xCCCCCCCDull;                 /* ceil(2^35 / 10) */
    printf("m = %llu, 10*m - 2^35 = %lld\n", (unsigned long long)m, (long long)(10 * m) - (1ll << 35));
    uint64_t bad = 0;
    for (uint64_t x = 0; x <= 0xFFFFFFFFull; x++)
        if (((x * m) >> 35) != x / 10) bad++;
    printf("all 2^32 inputs checked, mismatches: %llu\n", (unsigned long long)bad);
    return 0;
}
```

```text
m = 3435973837, 10*m - 2^35 = 2
all 2^32 inputs checked, mismatches: 0
```

It took 3.4 seconds, which is the exhaustive proof for 32 bits. Here is what GCC 13 generated at `-O2` for six one-line functions (`x / 10` as unsigned and signed, `x / 8` signed, `x % 10`, `x / 7`, and a 64-bit `x / 10`):

x86-64 (GCC 13, `-O2 -masm=intel`):

```text
udiv10:
    mov    eax, edi
    mov    edx, 3435973837
    imul   rax, rdx
    shr    rax, 35
    ret
sdiv10:
    movsx  rax, edi
    sar    edi, 31
    imul   rax, rax, 1717986919
    sar    rax, 34
    sub    eax, edi
    ret
sdiv8:
    test   edi, edi
    lea    eax, 7[rdi]
    cmovns eax, edi
    sar    eax, 3
    ret
umod10:
    mov    eax, edi
    mov    edx, 3435973837
    imul   rax, rdx
    shr    rax, 35
    lea    edx, [rax+rax*4]
    mov    eax, edi
    add    edx, edx
    sub    eax, edx
    ret
udiv10_64:
    movabs rax, -3689348814741910323
    mul    rdi
    mov    rax, rdx
    shr    rax, 3
    ret
udiv7:
    mov    eax, edi
    imul   rax, rax, 613566757
    shr    rax, 32
    sub    edi, eax
    shr    edi
    add    eax, edi
    shr    eax, 2
    ret
```

AArch64 (GCC 13, `-O2`):

```text
udiv10:
    mov    w1, 52429
    movk   w1, 0xcccc, lsl 16
    umull  x0, w0, w1
    lsr    x0, x0, 35
    ret
sdiv10:
    mov    w1, 26215
    movk   w1, 0x6666, lsl 16
    smull  x1, w0, w1
    asr    x1, x1, 34
    sub    w0, w1, w0, asr 31
    ret
sdiv8:
    cmp    w0, 0
    add    w1, w0, 7
    csel   w0, w1, w0, lt
    asr    w0, w0, 3
    ret
udiv10_64:
    mov    x1, -3689348814741910324
    movk   x1, 0xcccd, lsl 0
    umulh  x0, x0, x1
    lsr    x0, x0, 3
    ret
```

What to notice:

- **Unsigned /10** is one multiply (3 cycles) and one shift (1): about 4 cycles of latency against 11 to 12 for `div r32`, and the multiply is pipelined. For 64 bits the same trick uses the **high half** of a 64 × 64 multiply (`mul`, `umulh`), with `0xCCCCCCCCCCCCCCCD` and a shift of 3.
- **Signed /10** multiplies by about 2³⁴/10 and then subtracts the sign bit (`sar edi, 31`): C rounds toward zero, and the multiply rounds toward minus infinity, so a negative quotient is one too small without the fix.
- **Signed /8 is not a shift.** `x >> 3` rounds toward minus infinity, C's `x / 8` toward zero: for x = −7, `-7 >> 3` is −1 but `-7 / 8` is 0. The compiler adds 7 to a negative x before shifting (`lea eax, 7[rdi]; test; cmovns; sar`). This is the answer to "why is `x / 2` on an `int` more than one instruction, and why is the same on `unsigned` just one shift?".
- **`x % 10`** is `x − 10 · (x / 10)`, so it costs a multiply, a shift and a `lea`-add-subtract, not a divide.
- **/7 is harder** because ceil(2³⁵/7) needs 33 bits. The compiler uses a 32-bit magic number and a fix-up (`sub`, `shr`, `add`, `shr`) to emulate the extra bit.

Two practical rules. Put the constant in the source (`/ 10`, or a `constexpr` / `const` the compiler can see), not in a variable the compiler cannot see through. And when the divisor is fixed at run time but not at compile time (a table size chosen at start-up), libraries such as libdivide (*standard*) compute the magic number once and then do the multiply-shift for every division. For unsigned `x % 2ⁿ` and `x / 2ⁿ`, use `x & (2ⁿ − 1)` and `x >> n`.

### 4.5 What happens on divide by zero

The instruction sets disagree, and C says it is undefined behaviour either way ([tutorial 31](31_arm_vs_x86_and_putting_it_together.md) section 4 ran the experiment):

| Case | x86 `div`/`idiv` | AArch64 `udiv`/`sdiv` | RISC-V `div`/`rem` (*standard*) |
|---|---|---|---|
| divide by zero | traps (`#DE`, delivered to the process as SIGFPE) | returns 0, no trap | quotient all ones (−1), remainder = dividend, no trap |
| `INT_MIN / -1` | traps (the quotient does not fit) | returns `INT_MIN`, no trap | quotient `INT_MIN`, remainder 0, no trap |

On x86 the dividend is the register pair `rdx:rax` (or `edx:eax`) and the instruction traps if the quotient does not fit in one register, which is why compilers precede `idiv` with `cdq`/`cqo` (sign-extend into `rdx`) and why `div` with a garbage `rdx` crashes.


### 4.6 Square root, and the functions that are not in hardware

Square root is specified like division: the result must be the exact root, rounded once. It is computed by the same kind of digit recurrence as SRT division and, on most cores, by the same unit. Measured here (the program at the end): `sqrtsd` has a latency of about 19 cycles and starts one per 6; `sqrtss` about 13 and one per 3. Compare `divsd` at 14 and 4, `divss` at 11 and 3: a root costs roughly a divide and a half. The practical rule is to avoid it when a cheaper test exists: `x*x + y*y < r*r` instead of `sqrt(x*x + y*y) < r`.

Graphics, physics and normalising vectors need 1/√x, and it comes from Newton's method again: y_next = y · (3 − x·y²) / 2, which squares the error each step. The classic starting guess comes from the bits of the float: the exponent field is roughly the logarithm of the value, so halving the bit pattern and subtracting it from a constant halves and negates the exponent:

```text
i = 0x5f3759df - (i >> 1)        where i is the float's bits;  y = the float with bits i;  then y = y * (1.5 - 0.5 * x * y * y)
```

I checked every mantissa for both exponent parities (all 2²⁴ values that matter): the guess alone is within 3.4 % (about 5 correct bits), one Newton step gives 0.18 % (9 bits), two steps 4.7 × 10⁻⁶ (17.7 bits) and three reach the rounding noise of a `float` (1.5 × 10⁻⁷). Hardware does the same thing with a table: `rsqrtps` returns a 12-bit approximation for four floats at once (*standard*), and one step brings it near 23 bits. GPUs have approximate reciprocal and reciprocal-square-root instructions in their special-function units (*standard*).

Sine, cosine, exponential, logarithm and `pow` are not single instructions on SSE, AVX or Arm. The C library computes them in software with three parts: **range reduction** (map the argument into a small interval), a **table** lookup for the top bits, and a **polynomial** for the rest, evaluated by Horner's rule with one FMA per coefficient (section 5.6). That costs tens of cycles per call (not measured here), which is why `-ffast-math` versions and vectorised libraries (libmvec, SVML) exist. **CORDIC** is the hardware alternative where there is no multiplier: it rotates a vector by a fixed series of angles whose tangents are powers of two, so each step is a shift and an add (*standard*).

---

## 5. Floating-point arithmetic in hardware

[Tutorial 02](02_data_representation.md) section 9 gave the format: a sign, an exponent with a bias and a fraction with a hidden leading 1, so a `float` has a **24-bit significand** and a `double` 53 bits. This section is about the circuits. The IEEE-754 rule that drives all of them: addition, subtraction, multiplication, division, square root, fused multiply-add and conversions must return **the exact mathematical result rounded once**. The hardware therefore has to carry enough extra information to round correctly, and most of what follows is how little extra is enough.

### 5.1 Multiplication

Multiplication is the easy one, because the significands just multiply:

1. sign = sign A xor sign B;
2. exponent = exponent A + exponent B − bias (one bias too many after adding);
3. multiply the two 24-bit significands: a 24 × 24 → 48-bit product, with the tree of section 3;
4. **normalise**: each significand is in [1, 2), so the product is in [1, 4); if it is 2 or more, shift right one place and add 1 to the exponent;
5. **round** to 24 bits: look at the discarded bits (more than half: round up; less than half: down; exactly half: to even, section 5.4);
6. rounding can carry all the way out (1.111...1 rounds up to 10.000...0): shift once more and add 1 to the exponent;
7. check for overflow (infinity) and underflow (subnormal or zero).

An example worth keeping in mind: (1 + 2⁻²³) × (1 + 2⁻²³), the float `0x3F800001` squared. The significands are both 2²³ + 1, the product is (2²³ + 1)² = 2⁴⁶ + 2²⁴ + 1, a 47-bit number, so no normalising shift. Keep the top 24 bits (shift right 23): 2²³ + 2. The discarded part is 1, far below half (2²²), so round down. Result: 1 + 2⁻²², the float `0x3F800002`. The exact product had an extra 2⁻⁴⁶ that simply vanishes. Both the hardware and the integer-only function you will write in Problem D give `0x3F800002`.

### 5.2 Addition and subtraction

Addition is harder, because the two significands only line up after the exponents are made equal:

```text
 A, B ---> [compare exponents]      d = |exp A - exp B|; make A the operand with the larger exponent
                |
          [align]                   shift B's significand right by d (a barrel shifter, section 9 of tutorial 03);
                |                   the bits shifted out are kept as guard, round and sticky (section 5.3)
          [add or subtract]         the integer adder of section 2; the operation is "subtract" when
                |                   (operation XOR the two signs) says the magnitudes should be subtracted
          [normalise]               carry out -> shift right 1; cancellation -> count leading zeros, shift left
                |
          [round] --> [renormalise if the rounding carried out] --> result
```

Two situations behave very differently, and fast adders build two paths for them (*standard*):

- **Far (d ≥ 2).** The smaller operand is shifted a long way and loses low bits, but the result can lose at most one leading bit, so the normalising shift is 0 or 1 places.
- **Near (d = 0 or 1).** Little shifting is needed and no bits are lost, but subtracting nearly equal numbers can cancel many leading bits (`1.0000001 - 1.0000000`), and the result then needs a large left shift found by a **leading-zero counter**.

The subtraction in the near case is **exact**: no rounding error is introduced by it. That is catastrophic cancellation's cruel joke. The operands' own earlier rounding errors, which sat in their last bits, are suddenly in the leading bits of the result. `a*a - b*b` loses digits when a and b are close; `(a - b) * (a + b)` does not. The same effect makes the textbook quadratic formula inaccurate for roots that nearly cancel.

An add takes two cycles on this core, a multiply four, and an integer add one (section 1): more stages for the compare/align, add, normalise and round steps.

### 5.3 Guard, round and sticky bits

When B is shifted right to line up with A, bits fall off the end. To round correctly you need to know three things about what fell off, and no more:

- the **guard bit** G: the first bit below the last place we keep (it says whether the discarded part is at least half);
- the **round bit** R: the second bit below it (it is needed because a later left shift by one place after a cancellation promotes G to a kept bit, and R becomes the new guard);
- the **sticky bit** S: the OR of *all* the remaining bits, so S = 1 means "something non-zero is hiding below".

The rule for round-to-nearest-even, with L the last kept bit:

```text
round up  when  G = 1  and  (R = 1  or  S = 1  or  L = 1)
```

Said in words: more than half way, or exactly half way and the kept value is odd.

Work in a toy format so you can do it by hand: a significand of 5 bits, `1.xxxx`, so four fraction bits.

**Example 1: all three bits matter.** 8.5 + 0.96875. A = `1.0001 × 2³` (8.5), B = `1.1111 × 2⁻¹` (0.96875). The exponents differ by 4, so B shifts right by 4 places: `0.0001|1111 × 2³`. The four bits after the bar are G = 1, R = 1, S = 1 (the OR of the last two). Add the kept parts: `1.0001 + 0.0001 = 1.0010`, with GRS = 111. G = 1 and R = 1, so round up: `1.0011 × 2³` = 9.5. Check: the exact sum 9.46875 lies between 9.0 (`1.0010`) and 9.5 (`1.0011`) and is closer to 9.5.

**Example 2: why a sticky bit and not just "a few more bits".** 8.0 + 0.265625. A = `1.0000 × 2³`, B = `1.0001 × 2⁻²`. B shifts right by 5: `0.0000|10001 × 2³`. After the bar, G = 1, R = 0, and the remaining bits `01` make S = 1. The kept sum is `1.0000` with GRS = 101: more than half, round up to `1.0001 × 2³` = 8.5. The exact sum is 8.265625, nearer to 8.5 than to 8.0. **If the sticky bit were missing,** GRS would read 100, which looks like an exact tie, and round-to-even would pick 8.0: a wrong answer, from a pattern that is rare but real.

**Example 3: a genuine tie.** 8.0 + 0.25: B = `1.0000 × 2⁻²` shifted by 5 gives `0.0000|10000 × 2³`: G = 1, R = 0, S = 0. This is exactly half way between 8.0 and 8.5, and L = 0 is even, so the result stays 8.0.

I checked the claim "three extra bits are enough" the hard way. I wrote this adder for a 5-bit significand with exponents from −6 to 6, fed it **all 173,056 pairs** of values (both signs, including subtractions and cancellations) and compared with the exact sum rounded once: **0 wrong**. The same adder with the sticky bit removed (shifted-out bits just dropped) gave **2,292 wrong answers** of 173,056. The sticky bit is not decoration.

### 5.4 Rounding modes and ties to even

IEEE-754 defines five rounding rules. The default and the only one most code ever sees is **round to nearest, ties to even** (RNE). The others are round toward zero (truncate), toward +∞, toward −∞ (the three directed modes, used in interval arithmetic and in careful numerical code) and, in the decimal formats, ties away from zero.

*Why ties to even?* When the discarded part is exactly half, "round up" always errs the same way and the errors add up over a long sum; round-to-even rounds up half the time and down the other half. Ties are common whenever the data are small integers or sums of halves. Example from the previous tutorials: 2²⁴ = 16,777,216 is the first float where the spacing becomes 2, so `16777216.0f + 1.0f` is a tie between 16,777,216 and 16,777,218 and rounds to the even one, 16,777,216, while `16777216.0f + 3.0f` is a tie between 16,777,218 and 16,777,220 and rounds to 16,777,220 (checked; Problem C(c) has the program).

The mode is a bit-field in a control register: x86 `MXCSR` bits 14:13, Arm `FPCR` bits 23:22; C exposes it as `fesetround()`, and you need `-frounding-math` so the compiler does not fold constants in the default mode. Machine-learning training with very short formats sometimes uses **stochastic rounding** (round up with probability proportional to the discarded fraction), so that small gradients survive on average (*standard*).

### 5.5 Subnormal numbers are slow

A number with the smallest exponent and no hidden 1 is **subnormal** (tutorial 02). Normal arithmetic hardware assumes the hidden 1; a subnormal operand needs a leading-zero count to be normalised, and a result that is too small needs a variable right shift with sticky bits to be denormalised. These cases are rare, so the designers did not spend gates on them: on most x86 cores they take a **microcode assist**. I timed single `mulss` and `addss` instructions (`denorm2.c`, listed at the end), 20 million each:

| Operation | Default | Flush-to-zero and denormals-are-zero on |
|---|---|---|
| multiply, normal × normal → normal | 0.3 ns | 0.3 ns |
| multiply, normal × normal → **subnormal result** | **38 to 40 ns** | 0.3 ns |
| multiply, **subnormal operand** × normal → subnormal | **38 to 39 ns** | 0.3 ns |
| multiply, **subnormal operand** × large → normal | **38 to 40 ns** | 0.3 ns |
| add, normal + normal → normal | 0.6 ns | 0.6 ns |
| add, normal − normal → **subnormal result** | **39 to 45 ns** | 0.6 ns |
| add, subnormal + subnormal → subnormal | 0.6 ns | 0.6 to 0.7 ns |
| add, subnormal + normal → normal | 0.6 ns | 0.6 ns |

*Measured*, three runs, ranges across them. Each figure includes the loop overhead, so compare multiply rows with multiply rows and add rows with add rows. About 40 ns is 130 cycles: the same price [tutorial 21](21_simd_and_vector_processing.md) section 6.4 measured for a whole AVX2 instruction, and a slowdown of more than 100 times. The table shows which cases pay:

- **A multiply pays when its result is subnormal or when either input is.** The third and fourth multiply rows differ in the result (subnormal against normal) and cost the same, so the subnormal *input* alone is enough.
- **An add or subtract pays only when it turns normal operands into a subnormal result** (the subtraction of two nearby numbers, which is also how a decaying filter or recursion falls into the range). A subnormal operand added to a normal number was free, and so was adding two subnormals. Both need no normalising or denormalising shift (my explanation, not Intel's; Intel does not publish the rules).

The cure is the one in tutorial 21: set the flush-to-zero and denormals-are-zero bits (`MXCSR` bits 15 and 6) in every thread that does the maths, or keep values away from the tiny range.

### 5.6 Fused multiply-add

A **fused multiply-add** computes `a × b + c` with **one rounding** instead of two. It is built by not rounding the product: the multiplier's tree (section 3) delivers `a × b` as the two unrounded carry-save rows, the addend `c` is aligned against it by a shifter about three significands wide (*standard*), all three are summed, and the sum is normalised and rounded once. Its latency is that of a multiply (4 cycles here) and it issues twice per cycle (section 1), so `a × b + c` costs no more than `a × b`: **two floating-point operations per instruction**, the reason peak GFlop/s figures count an FMA as two.

Because the product is exact inside the unit, FMA does things two separate operations cannot:

```c
/* A fused multiply-add rounds once, so it can recover the rounding error of a product exactly.   build: gcc -O2 -ffp-contract=off -mfma fma_demo.c -o fma_demo -lm */
#include <stdio.h>
#include <math.h>

int main(void) {
    volatile double x = 1.0 + 0x1p-27;            /* 1 + 2^-27 */
    double p = x * x;                              /* exact value is 1 + 2^-26 + 2^-54; the 2^-54 does not fit */
    double two_roundings = x * x - p;              /* multiply, round, subtract: the error is rounded away */
    double one_rounding  = fma(x, x, -p);          /* x*x - p computed exactly, rounded once */
    printf("p             = %a\n", p);
    printf("x*x - p       = %a   (separate multiply and subtract)\n", two_roundings);
    printf("fma(x, x, -p) = %a   (= 2^-54, the exact error)\n", one_rounding);

    /* the same fact, from the other side: a*b + c with and without fusing */
    volatile double a = 0.1, b = 10.0, c = -1.0;
    printf("0.1*10 - 1: separate %a, fused %a\n", a * b + c, fma(a, b, c));
    return 0;
}
```

```text
p             = 0x1.0000004p+0
x*x - p       = 0x0p+0   (separate multiply and subtract)
fma(x, x, -p) = 0x1p-54   (= 2^-54, the exact error)
0.1*10 - 1: separate 0x0p+0, fused 0x1p-54
```

`x × x` for x = 1 + 2⁻²⁷ is 1 + 2⁻²⁶ + 2⁻⁵⁴, and the last term does not fit in 53 bits, so `p` rounds it away. `x*x - p` then subtracts two equal numbers and gives 0: the information is gone. `fma(x, x, -p)` computes `x*x - p` exactly and rounds once, so it returns **the rounding error of the multiplication itself**, 2⁻⁵⁴. That trick (an *error-free transformation*) underlies compensated summation, double-double arithmetic and correctly rounded math libraries. The last line is the same fact in another form: 0.1 × 10 − 1 is 0 with two roundings (the product rounds to exactly 1.0) and 2⁻⁵⁴ with one rounding, because the double 0.1 is slightly more than a tenth.

The price is **reproducibility**: the compiler is allowed to contract `a*b + c` into an FMA (GCC does by default when the target has one), so the same source gives different last bits on a machine with FMA and one without, or in a vector loop and its scalar tail. [Tutorial 21](21_simd_and_vector_processing.md) section 6.6 and [tutorial 31](31_arm_vs_x86_and_putting_it_together.md) section 4 show it; `-ffp-contract=off` restores identical results across machines. Where FMA shines: dot products and matrix multiplication (every tensor-core operation is a multiply-accumulate), polynomial evaluation by Horner's rule (one FMA per coefficient), Newton-Raphson steps (the residual `1 - d·x` is computed exactly), and software square root and division.

### 5.7 Special values, comparisons and `max`

The exceptional results are defined to propagate, not to crash. Five **exception flags** are raised and then stay set until cleared: *invalid* (0/0, ∞ − ∞, square root of a negative; the result is a NaN), *divide by zero* (result ±∞), *overflow*, *underflow* and *inexact* (the result was rounded). They are sticky bits in `MXCSR` or the Arm `FPSR`; trapping on them is possible but off by default.

Three consequences that appear in code reviews:

**`==` on floats must treat NaN as unequal.** A NaN compares unordered with everything, including itself. GCC's code for `a == b`:

```text
x86-64:    xor    eax, eax                  AArch64:    fcmp  d0, d1
           ucomisd xmm0, xmm1                           cset  w0, eq
           mov    edx, 0
           setnp  al                        ; x86 sets ZF, PF and CF all to 1 for "unordered",
           cmovne eax, edx                  ; so the compiler adds a test of the parity flag
           ret                              ; (setnp) to keep NaN from comparing equal
```

**Minimum and maximum are not one operation.** The program below computes `max` with the hardware instruction on each architecture (x86 `maxss`, Arm `fmax`) and with the C library's `fmaxf`. I ran the x86 version natively and the AArch64 version under QEMU's user-mode emulation (so this is the architecture's defined behaviour, not a timing):

```c
/* "max" is not one operation: the same name gives different answers for NaN and signed zero on x86 and AArch64.
   build: gcc -O2 nanmax.c -o nanmax_x86 -lm      and      aarch64-linux-gnu-gcc -O2 nanmax.c -o nanmax_a64 -lm
   run:   ./nanmax_x86                              and      qemu-aarch64 -L /usr/aarch64-linux-gnu ./nanmax_a64 */
#include <stdio.h>
#include <math.h>

#if defined(__x86_64__)
static float hw_max(float a, float b)   { float r; asm("maxss %2, %0"        : "=x"(r) : "0"(a), "x"(b)); return r; }   /* x86 MAXSS */
static const char *hw_name = "maxss  (x86: returns the 2nd operand if either is NaN or both are zero)";
#else
static float hw_max(float a, float b)   { float r; asm("fmax %s0, %s1, %s2"  : "=w"(r) : "w"(a), "w"(b)); return r; }  /* Arm FMAX */
static const char *hw_name = "fmax   (Arm: any NaN gives NaN; +0 is greater than -0)";
#endif

int main(void) {
    volatile float nan = NAN, one = 1.0f, pz = 0.0f, nz = -0.0f;
    float in[][2] = { {nan, one}, {one, nan}, {pz, nz}, {nz, pz} };
    const char *lab[] = { "max(NaN, 1)", "max(1, NaN)", "max(+0, -0)", "max(-0, +0)" };
    printf("%s\n%-14s %-12s %s\n", hw_name, "", "hardware", "fmaxf()");
    for (int i = 0; i < 4; i++)
        printf("%-14s %-12g %g\n", lab[i], hw_max(in[i][0], in[i][1]), fmaxf(in[i][0], in[i][1]));
    return 0;
}
```

```text
x86-64 (native):
maxss  (x86: returns the 2nd operand if either is NaN or both are zero)
               hardware     fmaxf()
max(NaN, 1)    1            1
max(1, NaN)    nan          1
max(+0, -0)    -0           0
max(-0, +0)    0            -0

AArch64 (QEMU user mode):
fmax   (Arm: any NaN gives NaN; +0 is greater than -0)
               hardware     fmaxf()
max(NaN, 1)    nan          1
max(1, NaN)    nan          1
max(+0, -0)    0            0
max(-0, +0)    0            0
```

x86's `maxss a, b` is literally `a > b ? a : b`: if either input is a NaN, or both are zeros, it returns the **second** operand. So `max(NaN, 1)` is 1 but `max(1, NaN)` is NaN, and `max(+0, -0)` depends on the order. Arm's `fmax` returns NaN if either input is NaN and orders +0 above −0; its sibling `fmaxnm` instead returns the number. The C library's `fmaxf` ignores a NaN (returns the other operand) and may return either zero. A vectorised or hand-written `max` that is correct on one machine can give a different answer for NaN input on the other. GCC compiles `a > b ? a : b` to a single `maxss` (that is its exact meaning), but `fmaxf(a, b)`, whose NaN rule is different, stays a library call.

**Narrow formats trade range and precision.** Tutorial 02 section 9 and [tutorial 23](23_gpu_memory_tensor_cores_and_scaling.md) section 5.4 list FP16, BF16, TF32 and the FP8 formats. The rule behind all of them, and behind the guard bits above, is the same: **carry extra precision inside and round once at the end.** Tensor cores multiply FP16, BF16 or FP8 inputs and accumulate in FP32; int8 dot-product instructions accumulate in int32.

### 5.8 Fixed point for machines without a floating-point unit

Many microcontrollers and DSPs have no FPU, or you want exact, fast, deterministic arithmetic. **Fixed point** is an integer with an implied binary point (tutorial 02 section 7). In **Q15**, a 16-bit signed value *v* means *v* / 2¹⁵, covering −1 to just under +1. Multiplying two Q15 numbers gives a 32-bit product with 30 fraction bits, so the result is shifted right by 15; add half a step first to round to nearest. The one overflow is (−1) × (−1) = +1, which Q15 cannot represent, so the result **saturates** at the largest value. DSP instruction sets have this built in (Arm `qadd`, `sqdmulh`, x86 `pmulhrsw`, accumulators with extra guard bits for long sums of products).

```c
/* Q15 fixed-point multiply: 16-bit values meaning value / 32768. Check every input pair against exact arithmetic.   build: gcc -O2 q15.c -o q15 -lm */
#include <stdio.h>
#include <stdint.h>
#include <math.h>

static inline int16_t q15_mul(int16_t a, int16_t b) {
    int32_t p = (int32_t)a * b;                 /* the product has 30 fraction bits (Q30) */
    p = (p + (1 << 14)) >> 15;                  /* add half an LSB, drop 15 bits: round to nearest, back to Q15 */
    return p > 32767 ? 32767 : (int16_t)p;      /* only (-1) * (-1) = +1.0 can overflow: saturate */
}

int main(void) {
    long saturated = 0; double worst = 0;
    for (int a = -32768; a < 32768; a++)
        for (int b = -32768; b < 32768; b++) {
            double exact = (double)a * b / 32768.0;                 /* in units of one Q15 step */
            int16_t r = q15_mul((int16_t)a, (int16_t)b);
            double err = fabs(r - exact);
            if (exact > 32767.0) saturated++;                          /* only +1.0 * 2^15 = 32768 */
            else if (err > worst) worst = err;
        }
    printf("0.5 * 0.5 = 0x%04x (0x2000 is 0.25)\n", (uint16_t)q15_mul(0x4000, 0x4000));
    printf("-1 * -1 = 0x%04x (saturated; +1.0 does not exist in Q15)\n", (uint16_t)q15_mul(-32768, -32768));
    printf("all 2^32 input pairs: largest rounding error %.3f of one step; pairs that needed saturation: %ld\n", worst, saturated);
    return 0;
}
```

```text
0.5 * 0.5 = 0x2000 (0x2000 is 0.25)
-1 * -1 = 0x7fff (saturated; +1.0 does not exist in Q15)
all 2^32 input pairs: largest rounding error 0.500 of one step; pairs that needed saturation: 1
```

I ran every one of the 2³² input pairs and compared with exact arithmetic: the rounding error is at most half a step (0.5 of one unit in the last place), and exactly one pair, (−32768, −32768), needed saturation.

---

## 6. The ALU: flags, comparisons and bit tricks

### 6.1 Four flags and what they decide

Tutorial 03 section 9 introduced the ALU result flags. They fall out of the adder for free:

| Flag | Meaning | Computed as |
|---|---|---|
| **Z** (zero) | result is 0 | NOR of all result bits |
| **N** (negative) | top bit of the result | the result's bit n − 1 |
| **C** (carry) | carry out of the top bit | adder's carry out (Arm: for subtraction, carry = 1 means *no borrow*) |
| **V** (overflow) | signed overflow | carry into the top bit XOR carry out of it |

A comparison `a ? b` is the subtraction `a − b = a + ~b + 1` with the result discarded and the flags kept. Every condition code is then a formula over the four flags. I checked each of them against ordinary comparison for all 256 pairs of 4-bit numbers (Arm convention for C):

| Test | Condition (Arm name) | Flags | x86 name |
|---|---|---|---|
| equal / not equal | `eq` / `ne` | Z = 1 / Z = 0 | `e` / `ne` |
| unsigned `a < b` | `lo` (also `cc`) | C = 0 | `b` (CF = 1: x86 stores a borrow) |
| unsigned `a >= b` | `hs` (`cs`) | C = 1 | `ae` |
| unsigned `a > b` | `hi` | C = 1 and Z = 0 | `a` |
| unsigned `a <= b` | `ls` | C = 0 or Z = 1 | `be` |
| signed `a < b` | `lt` | N ≠ V | `l` |
| signed `a >= b` | `ge` | N = V | `ge` |
| signed `a > b` | `gt` | Z = 0 and N = V | `g` |
| signed `a <= b` | `le` | Z = 1 or N ≠ V | `le` |

*Why N ≠ V means "less than".* If no overflow happened (V = 0), the sign of the difference is the answer: negative means a < b. If overflow happened (V = 1), the sign is wrong, so the answer is the opposite. Example with 4-bit values: `0111 - 1000`, that is 7 − (−8). The true difference 15 does not fit; the result bits are `1111`, so N = 1, and V = 1. N ≠ V is false, so the signed `a < b` is false: correct, since 7 > −8. The same bits read as unsigned are 7 and 8, C = 0 (a borrow was needed), `lo` is true: 7 < 8, also correct. **One subtraction, two answers**; the branch instruction chooses which interpretation to apply.

### 6.2 The flags are an architectural choice

x86 and Arm keep the flags in a special register; RISC-V has none. The same C compiled for each (GCC 13 for x86-64 and AArch64, clang 18 for RISC-V; compiled only, not run on RISC-V or Arm hardware):

```c
/* Comparisons and overflow tests, to see how each architecture uses (or lacks) flags.
   x86-64:  gcc -O2 -S -masm=intel flags.c         AArch64: aarch64-linux-gnu-gcc -O2 -S flags.c
   RISC-V:  clang --target=riscv64-linux-gnu -march=rv64gc -O2 -S flags.c */
int  lt_signed(int a, int b)            { return a < b; }
int  lt_unsigned(unsigned a, unsigned b){ return a < b; }
int  add_overflows(int a, int b)        { int r; return __builtin_add_overflow(a, b, &r); }
int  uadd_carries(unsigned a, unsigned b){ unsigned r; return __builtin_add_overflow(a, b, &r); }
unsigned __int128 add128(unsigned __int128 a, unsigned __int128 b) { return a + b; }
```

```text
x86-64                          AArch64                         RISC-V (rv64gc)
lt_signed:                      lt_signed:                      lt_signed:
    cmp   edi, esi                  cmp   w0, w1                    slt   a0, a0, a1
    setl  al                        cset  w0, lt                    ret
lt_unsigned:                    lt_unsigned:                    lt_unsigned:
    cmp   edi, esi                  cmp   w0, w1                    sltu  a0, a0, a1
    setb  al                        cset  w0, cc                    ret
add_overflows:                  add_overflows:                  add_overflows:
    add   edi, esi                  adds  w0, w0, w1                add   a2, a0, a1       ; 64-bit sum
    seto  al                        cset  w0, vs                    addw  a0, a0, a1       ; 32-bit sum
                                                                    xor   a0, a0, a2       ; they differ
uadd_carries:                   uadd_carries:                       snez  a0, a0           ; if it overflowed
    add   edi, esi                  adds  w0, w0, w1            uadd_carries:
    setc  al                        cset  w0, cs                    addw  a1, a1, a0
add128:                         add128:                             sltu  a0, a1, a0       ; sum < operand
    add   rax, r8                   adds  x0, x0, x2            add128:
    adc   rdx, rdi                  adc   x1, x1, x3                add   a1, a1, a3
                                                                    add   a0, a0, a2
                                                                    sltu  a2, a0, a2       ; carry of the low half
                                                                    add   a1, a1, a2
```

(Abridged: the `ret` lines, the `xor eax, eax` that clears the result register on x86, and the argument moves in x86 `add128` are left out. The full output is what the compiler printed; every instruction shown is from it.) The trade-offs:

- **Flags are cheap in hardware and hard on the pipeline.** Every flag-setting instruction writes a shared piece of state, and the next conditional instruction reads it. An out-of-order core must **rename** the flags like registers ([tutorial 11](11_superscalar_and_out_of_order_execution.md)); older x86 cores stalled on partial flag updates (`inc` leaves CF unchanged, *standard*). In x86 nearly every ALU instruction sets flags; in Arm only the `S` forms (`adds`, `subs`, and `cmp`/`tst` which are `subs`/`ands` into the zero register) do, so the compiler chooses when to pay.
- **RISC-V drops flags completely** to keep every instruction independent: comparisons produce a 0 or 1 in an ordinary register (`slt`, `sltu`) or branch directly (`blt`). The price shows in the listing: overflow detection and multi-word arithmetic take more instructions (a signed overflow test is two adds, an xor and a test; the 128-bit add needs a `sltu` to turn the carry into a value).
- **Detecting overflow in C** is best left to the compiler: `__builtin_add_overflow(a, b, &r)` compiles to `seto`/`cset vs` where flags exist. Signed overflow itself is undefined behaviour in C, so test *before* adding or use the builtin; do not add and then check the sign.
- **Multi-word arithmetic** (big integers, 128-bit values, cryptography) chains the carry: `add` then `adc` (add with carry), the same on x86 and Arm, as in `add128`. x86 also has `mulx` and the two independent carry chains `adcx` and `adox` for fast big-number multiplication (*standard*).

### 6.3 Bit-manipulation instructions

An ALU also carries a few operations that are awkward as a sequence of shifts and masks and are cheap as dedicated circuits:

| Operation | x86 / Arm / RISC-V (Zbb extension; instruction names *standard*) | C | Use |
|---|---|---|---|
| count ones | `popcnt` / `cnt` + `addv` / `cpop` | `__builtin_popcountll` | Hamming distance, bitmap statistics |
| count leading zeros | `lzcnt` / `clz` / `clz` | `__builtin_clzll` | log₂, normalising, finding the highest set bit |
| count trailing zeros | `tzcnt` / `rbit` + `clz` / `ctz` | `__builtin_ctzll` | **find first set bit** in a bitmap (schedulers, allocators, hash-table probing) |
| byte swap | `bswap` / `rev` / `rev8` | `__builtin_bswap64` | endianness (tutorial 02 section 8) |
| bit reverse | (none) / `rbit` / (none in Zbb) | | CRCs, FFT index reversal |
| extract / deposit bits | `pext`, `pdep` (BMI2) / `ubfx`, `bfi` (one fixed field) / (none) | | field packing, bit-fields, Morton codes |

Measured here: `popcnt` and `lzcnt` have a latency of 3 cycles and issue once per cycle (section 1), as expensive as a multiply, but each replaces a loop or a dozen instructions of SWAR (bit-hack) code. Note the traps: `__builtin_clz(0)` and `__builtin_ctz(0)` are **undefined** in C (the underlying `bsr`/`bsf` leave the destination undefined for zero), while `lzcnt` and `tzcnt` return the operand width. A portable program must test for zero first. Shifts and rotates use the barrel shifter of tutorial 03: one cycle at any amount.

---

## 7. What to take into your code

| If you see | Prefer | Why |
|---|---|---|
| `x / 10`, `x % 10` with a constant | leave it; the compiler uses multiply-shift (about 4 cycles) | `div r64` is 14 to 15 cycles and not pipelined (4.4) |
| a division by the same run-time value in a loop | precompute the reciprocal (with `-freciprocal-math` allowed) or a libdivide-style magic number | multiply 4 cycles, pipelined; divide 14, not (4.3) |
| signed `x / 2^n` or `x % 2^n` | use `unsigned` where negatives cannot occur; mask and shift | signed needs the sign fix-up (4.4) |
| `x * 10`, `x * 5 + 3` | write it plainly | `lea`/shift sequences, no multiply (3.6) |
| a floating-point sum or dot product with one accumulator | 4 to 8 accumulators, or let the compiler vectorise | latency 2 to 4, two issues per cycle (1) |
| `a*b + c` in numerical code | accept the FMA, and fix `-ffp-contract` if you need bit-identical results across machines | one rounding; different from separate ops (5.6) |
| values decaying toward zero (filters, recursion, activations) | flush-to-zero, or add a tiny constant | a subnormal result (or a subnormal multiply input) costs about 130 cycles (5.5) |
| `sqrt(x*x + y*y) < r`, or a `sqrt` only to compare | compare the squares | a root costs about 19 cycles (`double`), and the multiplies 4 (4.6) |
| `max`/`min` on data that can hold a NaN | explicit NaN handling; do not assume x86 and Arm agree | different results (5.7) |
| an integer overflow test | `__builtin_add_overflow` | uses the flag; the C expression is undefined behaviour (6.2) |
| a bitmap scan | `__builtin_ctzll` after testing for 0 | one instruction, not a loop (6.3) |

---

## 8. Mistakes that cost points

1. **Quoting one number for "the cost" of an operation.** A multiply has a latency of 3 cycles and a throughput of 1; a divide has 14 and 10. Which one matters depends on whether the next operation needs the result.
2. **Saying Booth encoding "skips zeros".** In hardware the number of rows is fixed. The useful version is radix-4, which halves the rows.
3. **Saying a lookahead adder is fast "because it works in parallel" and stopping there.** Name the depth (log₂ n levels against n) and the price (cells, wires, fan-out).
4. **Saying division is slow "because it is complicated".** It is slow because every quotient digit depends on the previous remainder, so there is nothing to put in parallel.
5. **Using `>>` for a signed `/`.** `-7 >> 3` is −1; `-7 / 8` is 0. Signed division by a power of two needs the sign fix-up.
6. **Treating `x * (1.0 / d)` as identical to `x / d`.** It is not always the same last bit; the compiler will not do it for you without `-ffast-math` or `-freciprocal-math`.
7. **Expecting bit-identical floating-point results from two builds.** FMA contraction, vector width and evaluation order all change the last bits.
8. **Muddling the three extra bits, or rounding "half up".** Guard is the first discarded bit, round the second, sticky the OR of the rest; ties go to the even neighbour, not up.
9. **`==` on floats, and ignoring NaN in comparisons and `max`/`min`.** NaN is unordered; x86 `maxss` and Arm `fmax` disagree about it.
10. **Mixing up C and V.** C (carry/borrow) decides unsigned comparisons; N and V decide signed ones. After a subtraction x86 CF means *borrow*, Arm C means *no borrow*.
11. **Assuming subnormal numbers cost nothing.** On this core a multiply with a subnormal input or result, or an add that produces a subnormal result, costs about 130 cycles.
12. **Calling `__builtin_clz` or `__builtin_ctz` on zero.** Undefined; test first.

---

## 9. Questions you will be asked

**A 64-bit ripple-carry add would take over 100 gate delays. How does a CPU add in one cycle?** It computes all the carries in parallel. Each bit position has a generate (`a·b`) and a propagate (`a ^ b`); combining neighbouring positions with the associative operator `(G,P) ∘ (G',P') = (G + P·G', P·P')` makes the carries a parallel-prefix problem, solvable in log₂ n levels (6 for 64 bits) with Kogge-Stone, Sklansky or Brent-Kung networks. They trade levels, cells and fan-out (for 64 bits: 6 levels with 321 cells, 6 with 192 but fan-out 32, or 11 with 120). Measured here: a dependent 64-bit add is 1 cycle, and about four can start per cycle.

**What is a carry-save adder and where is it used?** A row of independent full adders that turns three numbers into two (a sum word and a shifted carry word) in one full-adder delay, whatever the width. It postpones carry propagation, so it is used wherever many numbers are added: the partial-product tree of a multiplier, and multiply-accumulate units; one fast adder at the end resolves the last two words.

**How does a hardware multiplier work, and why is it 3 cycles but one per cycle?** Booth recoding turns the multiplier into n/2 signed digits {−2..2}, each selecting 0, ±M or ±2M; a tree of 3:2 or 4:2 compressors (Wallace or Dadda) reduces the n/2 rows to two in about log₁.₅ n stages (8 for 64 × 64); a prefix adder finishes. Three pipeline stages give a latency of 3, and because each stage is separate hardware a new multiply enters every cycle. A floating-point multiply uses the same tree on the significands.

**Does Booth recoding skip zeros? Why is it used?** No; the hardware always has the same number of rows. Radix-4 Booth halves them, from n to n/2, at the cost of a small encoder and a multiplexer per row, which is cheaper than the adders it saves. It also handles two's-complement multipliers without a sign correction.

**Why is the low half of a product the same for signed and unsigned operands?** Because two's-complement numbers are the unsigned residues modulo 2ⁿ, and the low n bits of a product depend only on the residues. The high half differs, so widening multiplies come in signed and unsigned forms (`mul`/`imul` one-operand on x86, `umulh`/`smulh` on Arm, `mulhu`/`mulh` on RISC-V).

**Why is division so much slower than multiplication, and not pipelined?** Each quotient digit depends on the remainder left by the previous one, so the work is a chain. A divider retires a few bits per cycle with SRT (redundant digits chosen from a small lookup of the top bits, remainder in carry-save form), and its iterations reuse the same hardware, so it stays busy for the whole division. Measured: 64-bit divide 14 to 15 cycles latency, one per 10 cycles; multiply 3 and 1.

**How does the compiler divide by 10 without a divide?** It multiplies by a precomputed `ceil(2³⁵/10) = 0xCCCCCCCD` and shifts right by 35, valid for every 32-bit unsigned x because the error term `x·e/(d·2ᵏ)` stays below 1/d (I tested all 2³² inputs). Signed division adds a sign fix-up; `x % 10` is `x - 10*(x/10)`. About 4 cycles against 11 to 12 for `div r32`.

**Why is `x / 2` on an `int` more than a shift?** C rounds toward zero and an arithmetic shift rounds toward minus infinity, so negative values need `x + (x < 0 ? 1 : 0)` first. For `unsigned` it is one shift.

**Walk me through a floating-point addition.** Compare exponents and swap so A is larger; shift B's significand right by the difference, keeping the bits that fall off as guard, round and sticky; add or subtract the significands; normalise (carry out: shift right one; cancellation: count leading zeros and shift left); round (round-to-nearest-even uses G, R and S: round up if G and (R or S or the last bit)); renormalise if rounding carried out. Overflow, underflow and the special values are handled around this.

**What are guard, round and sticky bits and why three?** Guard is the first discarded bit (tells you whether the discard is at least half), round is the second (it becomes the guard after a one-place left shift following cancellation), sticky is the OR of everything lower (distinguishes an exact tie from "a little more than half"). Fewer bits give wrong answers: I tested a 5-bit-significand adder on all 173,056 operand pairs, correct with three bits and wrong 2,292 times without the sticky bit.

**What is fused multiply-add, and what changes?** `a*b + c` with a single rounding, built by keeping the product unrounded in carry-save form and adding the aligned `c`. Same latency as a multiply (4 cycles here), two per cycle, so two flops per instruction. It is more accurate, recovers the exact error of a product (`fma(a, b, -a*b)`), but changes results versus separate multiply and add, so the same source can differ between machines unless contraction is disabled.

**Why can a loop get 100 times slower when values get small?** Subnormal numbers. Fast floating-point paths assume a normalised significand; a multiply with a subnormal input or result, or an add that produces a subnormal result, takes a microcode assist of about 130 cycles (40 ns here). Fix: flush-to-zero and denormals-are-zero, or keep values out of that range.

**How are the condition codes computed and how does a branch use them?** Z is a NOR of the result, N the top bit, C the carry out, V carry into the top bit XOR carry out. A compare is a subtract that keeps only the flags. Unsigned less-than is "no carry" (Arm `lo`), signed less-than is N ≠ V (`lt`). RISC-V has no flags: it writes a compare result to a register (`slt`) or branches on two registers.

**What does a square root cost, and how do you avoid it?** About a divide and a half: here `sqrtsd` is 19 cycles with one per 6 (`double`) and `sqrtss` 13 with one per 3, on the divider. Compare squares instead of roots; for 1/√x use the hardware estimate (`rsqrtps`, 12 bits) plus Newton steps, each of which doubles the correct bits.

**Why can `max(a, b)` give different answers on x86 and Arm?** x86 `maxss` returns the second operand when either is NaN or both are zero; Arm `fmax` returns NaN if either is NaN and treats +0 as greater than −0 (`fmaxnm` returns the number). The C library's `fmaxf` returns the non-NaN. Any hand-written or vectorised max must say which it means.

---

## 10. Problems

**How to use this section.** Try each problem before you read the answers. A, B and C are paper problems of about 25 minutes each (C has a short godbolt part); D is a short C program of about 40 minutes.

### Problem A (paper): Booth and the multiplier tree

(a) Multiply 6 × (−5) with 4-bit radix-2 Booth: write the multiplier `1011`, the four digits and their partial products, and check the total.
(b) Recode the 8-bit multiplier `01101110` (110) with radix-4 Booth. Give the four digits, say how many partial products are non-zero, and use them to compute 5 × 110.
(c) How many partial-product rows does a signed 32 × 32 multiplier have without Booth and with radix-4 Booth, and how many 3:2 compressor stages does each need to reach two rows?
(d) A multiplier has a 3-cycle latency and starts a new multiply every cycle. Name what happens in each cycle and why the next multiply can enter in the second cycle.

*Where:* paper. *How:* (a) take each bit together with the bit to its right, starting with a 0 to the right of bit 0. (b) use the 3-bit table of section 3.4 on bits (1,0,0), (3,2,1), (5,4,3), (7,6,5) and the weights 1, 4, 16, 64. (c) every compressor stage turns each group of three rows into two and passes a leftover one or two rows through (rows: 32, 22, 15 ... ). (d) section 3.5.

### Problem B (paper): fast adders

(a) Add `01101101` + `00111011` with a Kogge-Stone network: write G and P for each bit, G after each of the three levels, the carry into each bit and the sum. Check the result against decimal.
(b) Count levels and combine cells for a 16-bit Brent-Kung adder and a 16-bit Kogge-Stone adder. Which would you pick for a low-power core?
(c) Add 9 + 11 + 13 + 7 with carry-save adders: show the sum and carry words after each level, then the final add.
(d) With the model "generate/propagate takes 1 gate delay, each prefix level 2, the final sum XOR 1, and a ripple stage 2", compare the delay of a 32-bit ripple adder and a 32-bit Kogge-Stone adder.

*Where:* paper. *How:* (a) copy the layout of section 2.1; combine each position with the one `d` places to its right for d = 1, 2, 4, using `G' = G + P·G_right`, `P' = P·P_right`. (b) the formulas of section 2.2. (c) a CSA's sum bit is the XOR of the three bits in a column and its carry bit is their majority, shifted left one place. (d) count the levels.

### Problem C (paper, plus godbolt): floating-point rounding and dividing by a constant

(a) In the toy format `1.xxxx × 2ᵉ` (four fraction bits), add 12.5 and 0.40625. Write both numbers in the format, show the shifted operand with its G, R and S, and the rounded result.
(b) In the same format multiply `1.1011 × 2¹` (3.375) by `1.0111 × 2⁰` (1.4375). Give the 10-bit product of the significands, the normalised result and the rounded one.
(c) What is the smallest positive `float` x with `x + 1.0f == x`? The same for `double`? Why does it happen there?
(d) For a 32-bit unsigned x, find a multiplier `m` and shift `k` so that `x / 5 == (x * m) >> k` for every x, and justify it with the condition of section 4.4. Then compile `unsigned f(unsigned x) { return x / 5; }` on godbolt.org with GCC `-O2` for x86-64 and for AArch64 and compare.

*Where:* (a), (b), (c), (d) first half on paper; (d) second half on godbolt.org. *How:* (a) the exponents differ by 5: shift B right by 5 and read the bits after the four kept ones. (b) multiply the integers 27 and 23, then write the product as `1.xxx... × 2²`. (c) think about the spacing of floats near x and about ties (section 5.4). (d) `m = ceil(2ᵏ/5)`; the error `e = 5m - 2ᵏ` must satisfy `x·e < 2ᵏ` for x up to 2³² - 1.

### Problem D (C): a floating-point multiply from integer operations

Write `uint32_t soft_fmul(uint32_t a, uint32_t b)` that multiplies two `float` bit patterns using only integer operations, for **normal** operands whose product is normal, rounding to nearest even. Follow the seven steps of section 5.1. Then test it against the hardware `*` on ten million random pairs, using exponent fields from 64 to 190 so the product cannot overflow or underflow. Random mantissas almost never hit an exact tie (about one in 10⁵ products), so also run a second test with operands whose fractions have only 12 significant bits, which makes about 40 % of the products exact ties.

*Where:* your editor and terminal. *How:* extract the 24-bit significands with the hidden 1 (`(x & 0x7FFFFF) | 0x800000`); the product needs 64-bit integers; after multiplying decide whether the top bit is bit 47 or bit 46 and use that to choose the shift; to round, compare the discarded bits with half of the unit in the last place, and break an exact tie toward an even result. Compile with `gcc -O2 -Wall -Wextra`. The goal is zero mismatches in both tests.

---

## Answers

Try before reading.

### Problem A

(a) The multiplier −5 is `1011`. With a 0 to the right of bit 0:

| Bit i | Pair (y_i, y_i−1) | Digit | Partial product (digit × 6 × 2ⁱ) |
|---|---|---|---|
| 0 | (1, 0) | −1 | −6 |
| 1 | (1, 1) | 0 | 0 |
| 2 | (0, 1) | +1 | +24 |
| 3 | (1, 0) | −1 | −48 |

Total: −6 + 0 + 24 − 48 = **−30** = 6 × (−5).

(b) `01101110`, bits y₇ ... y₀ = 0 1 1 0 1 1 1 0, with y₋₁ = 0.

| j | Bits (y₂ⱼ₊₁, y₂ⱼ, y₂ⱼ₋₁) | Digit | Weight 4ʲ | Partial product for M = 5 |
|---|---|---|---|---|
| 0 | (1, 0, 0) | −2 | 1 | −10 |
| 1 | (1, 1, 1) | 0 | 4 | 0 |
| 2 | (1, 0, 1) | −1 | 16 | −80 |
| 3 | (0, 1, 1) | +2 | 64 | +640 |

The digits are (−2, 0, −1, +2), three of them non-zero. Check: −2 + 0 − 16 + 128 = 110. Product: −10 + 0 − 80 + 640 = **550** = 5 × 110.

(c) Without Booth a 32 × 32 multiply has 32 rows (plus sign handling): 32 → 22 → 15 → 10 → 7 → 5 → 4 → 3 → 2, so **8 stages**. With radix-4 Booth, a signed multiplier has 16 rows: 16 → 11 → 8 → 6 → 4 → 3 → 2, **6 stages** (17 rows if the multiplier is unsigned, also 6). Booth saves two stages and half of the compressors.

(d) Cycle 1: Booth encode and select the partial products. Cycle 2: the compressor tree reduces them to two rows. Cycle 3: the final prefix adder produces the product. The three stages are separate hardware with registers between them, so while multiply 1 is in the tree, multiply 2 can be in the encoder: one multiply starts per cycle (throughput 1), each finishing 3 cycles after it started (latency 3).

### Problem B

(a) `a = 01101101` (109), `b = 00111011` (59):

```text
bit                                  7 6 5 4 3 2 1 0
a                                    0 1 1 0 1 1 0 1
b                                    0 0 1 1 1 0 1 1
G                                    0 0 1 0 1 0 0 1
P                                    0 1 0 1 0 1 1 0
G after level 1 (span 1)             0 1 1 1 1 0 1 1
G after level 2 (span 2)             0 1 1 1 1 1 1 1
G after level 3 (span 4)             0 1 1 1 1 1 1 1
carry INTO bit i                     1 1 1 1 1 1 1 0
sum = P xor carry-in                 1 0 1 0 1 0 0 0
```

`10101000` = 168 = 109 + 59, no carry out (G of the whole group is 0). The carry generated in bit 0 ripples all the way to bit 7, and the network finds it in three levels.

(b) 16 bits: Brent-Kung has 2·4 − 1 = **7 levels** and 2·16 − 2 − 4 = **26 cells**; Kogge-Stone has **4 levels** and 16·4 − 16 + 1 = **49 cells**. For a low-power core, Brent-Kung: nearly half the cells and wires for three extra levels, which a low-clock-rate core can afford.

(c) CSA level 1 on 9 (`01001`), 11 (`01011`), 13 (`01101`): sum bits `01111` (15), carry bits `10010` (18). Level 2 on 15, 18 and the fourth number 7 (`00111`): sum bits `011010` (26), carry bits `001110` (14). Final add: 26 + 14 = **40** = 9 + 11 + 13 + 7. Two CSA delays plus one carry-propagate add, instead of three carry-propagate adds.

(d) Ripple: 32 stages × 2 = **64** gate delays. Kogge-Stone: 1 (G/P) + 5 levels × 2 + 1 (final XOR) = **12**. About five times faster, for 129 combine cells instead of 31.

### Problem C

(a) 12.5 = `1.1001 × 2³`; 0.40625 = `1.1010 × 2⁻²`. The exponents differ by 5, so B becomes `0.0000|11010 × 2³`: G = 1, R = 1, S = OR(0,1,0) = 1. Kept sum `1.1001 + 0.0000 = 1.1001` with GRS = 111, which is more than half: round up to `1.1010 × 2³` = **13.0**. The exact sum 12.90625 lies between 12.5 and 13.0 and is closer to 13.0.

(b) Significands 27 (`11011`) × 23 (`10111`) = 621 = `1001101101` (10 bits), which is 2.4258 when read with two integer bits and eight fraction bits (621 / 256). That is 2 or more, so shift right once: `1.001101101 × 2²` (the exponent was 1 + 0 = 1, plus 1). The fraction `001101101` has four kept bits `0011` and the discarded `01101`, whose first bit (G) is 0, so below half: round down. Result `1.0011 × 2²` = **4.75**. The exact product is 4.8515625, which is between 4.75 and 5.0 (`1.0100 × 2²`) and closer to 4.75.

(c) **16,777,216 = 2²⁴** for `float`, and 2⁵³ = 9,007,199,254,740,992 for `double`. Below 2²⁴ the spacing of floats is at most 1, so x + 1 is representable. At 2²⁴ the spacing becomes 2; x + 1 is exactly half way between x and x + 2, and round-to-even picks x (whose last significand bit is 0). Between 2²⁴ and 2²⁵ it depends on the value: the tie goes to whichever neighbour has an even significand, so 2²⁴ + 2 + 1 rounds *up* to 2²⁴ + 4. From 2²⁵ on the spacing is 4 or more and every float absorbs a 1. A short program shows both cases:

```c
/* Ties in floating-point addition: 2^24 is where the spacing of floats becomes 2.   build: gcc -O2 tie.c -o tie */
#include <stdio.h>

int main(void) {
    volatile float big = 16777216.0f, one = 1.0f, three = 3.0f;
    printf("2^24 + 1 = %.0f   2^24 + 3 = %.0f   (2^24 + 1) + 1 = %.0f   2^24 + (1 + 1) = %.0f\n",
           big + one, big + three, (big + one) + one, big + (one + one));
    return 0;
}
```

```text
2^24 + 1 = 16777216   2^24 + 3 = 16777220   (2^24 + 1) + 1 = 16777216   2^24 + (1 + 1) = 16777218
```

The second pair also shows that float addition is not associative.

(d) With k = 34: m = ceil(2³⁴/5) = 3,435,973,837 = `0xCCCCCCCD` (the same constant as for /10), and e = 5·m − 2³⁴ = 17,179,869,185 − 17,179,869,184 = 1. The condition `x·e < 2ᵏ` becomes x < 2³⁴, true for every 32-bit x, so `x / 5 == (x * 0xCCCCCCCD) >> 34`. On godbolt GCC emits the same multiply as for /10 with a shift of 34 instead of 35 (x86-64: `imul rax, rdx` then `shr rax, 34`; AArch64: `umull` then `lsr x0, x0, 34`).

### Problem D

```c
/* 32-bit float multiply with integer operations only (normal numbers, round to nearest even), checked against the hardware.   build: gcc -O2 fmul.c -o fmul */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static uint32_t bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float    flt(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

uint32_t soft_fmul(uint32_t a, uint32_t b) {
    uint32_t sign = (a ^ b) & 0x80000000u;
    int e = (int)((a >> 23) & 0xFF) + (int)((b >> 23) & 0xFF) - 127;     /* add exponents, remove one bias */
    uint64_t ma = (a & 0x7FFFFF) | 0x800000;                              /* 24-bit significands with the hidden 1 */
    uint64_t mb = (b & 0x7FFFFF) | 0x800000;
    uint64_t p = ma * mb;                                                 /* 48-bit product, in [2^46, 2^48) */
    int shift = 23;
    if (p >> 47) { shift = 24; e++; }                                     /* product in [2,4): normalise */
    uint64_t keep = p >> shift, rest = p & ((1ull << shift) - 1), half = 1ull << (shift - 1);
    if (rest > half || (rest == half && (keep & 1))) keep++;              /* round to nearest, ties to even */
    if (keep >> 24) { keep >>= 1; e++; }                                  /* rounding carried into a new bit */
    return sign | ((uint32_t)e << 23) | (uint32_t)(keep & 0x7FFFFF);
}

/* random operands; 'mask' keeps some fraction bits and 'force' sets others, so a narrow mask makes exact ties common */
static long test(uint32_t mask, uint32_t force, long n, long *ties) {
    uint64_t s = 88172645463325252ull;                                    /* xorshift random numbers */
    long bad = 0;
    for (long i = 0; i < n; i++) {
        uint32_t r[2];
        for (int k = 0; k < 2; k++) {
            s ^= s << 13; s ^= s >> 7; s ^= s << 17;
            uint32_t exp = 64 + (uint32_t)(s >> 40) % 127;               /* exponent field 64..190: the product stays normal */
            r[k] = ((uint32_t)(s >> 63) << 31) | (exp << 23) | (((uint32_t)s & mask) | force);
        }
        uint64_t p = (uint64_t)((r[0] & 0x7FFFFF) | 0x800000) * ((r[1] & 0x7FFFFF) | 0x800000);
        int shift = (p >> 47) ? 24 : 23;
        *ties += (p & ((1ull << shift) - 1)) == (1ull << (shift - 1));    /* the discarded part is exactly one half */
        if (bits(flt(r[0]) * flt(r[1])) != soft_fmul(r[0], r[1])) bad++;
    }
    return bad;
}

int main(void) {
    long ties = 0, bad = test(0x7FFFFF, 0, 10000000, &ties);
    printf("10000000 random products:       mismatches %ld, exact ties %ld\n", bad, ties);
    ties = 0; bad = test(0x7FF800, 0x800, 10000000, &ties);
    printf("10000000 products with 12-bit fractions: mismatches %ld, exact ties %ld\n", bad, ties);
    return 0;
}
```

```text
10000000 random products:       mismatches 0, exact ties 87
10000000 products with 12-bit fractions: mismatches 0, exact ties 3863358
```

How it works: `p` is the exact 48-bit product of the two 24-bit significands, between 2⁴⁶ and 2⁴⁸. If bit 47 is set the product is in [2, 4), so the quotient bits to keep start one place higher (`shift` = 24 and the exponent grows by one); otherwise `shift` = 23. `keep` is the 24-bit result, `rest` the discarded bits, and `half` exactly half a unit in the last place: round up when `rest > half`, or when `rest == half` and `keep` is odd (round to even). If the increment carries out of 24 bits (`keep >> 24`), shift once more and bump the exponent. The first test meets only 87 exact ties in ten million random products; the second (12-bit fractions) meets 3.86 million, and both show no mismatch. To see why the second test is there, I changed the tie rule to "always round half up": the first test then reported 86 mismatches in ten million, the second 1,931,271.

---

## Programs behind the numbers

Everything above that was timed comes from the two programs below; the others (`magic.c`, `fma_demo.c`, `nanmax.c`, `q15.c`, `flags.c`, `tie.c`, `fmul.c`) are listed where they are used. The small checks of sections 2 to 5 were Python scripts that built the adder networks, the Booth recodings, the compressor trees, the dividers, the toy floating-point adder and the reciprocal-square-root iteration and compared each with exact arithmetic; their results are quoted in the text, and the scripts are not reproduced.

**`chain.c`: latency and throughput.** Each operation is timed as one dependent chain (latency) and as eight independent chains (throughput). The empty `asm` statements stop the compiler from folding the chain and from replacing a divide by a known constant with a multiply (section 4.4). `-fno-math-errno` lets `sqrt` compile to the bare instruction. Run it pinned to one CPU, three times.

```c
/* Latency (one dependent chain) and throughput (eight independent chains) of arithmetic operations.
   build: gcc -O2 -fno-math-errno -mfma -mpopcnt -mlzcnt chain.c -o chain -lm        run: taskset -c 1 ./chain */
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <immintrin.h>
#include <time.h>

#define N 50000000L
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

/* E(x, y) is the operation. The empty asms stop the compiler from folding the chain and from turning a divide by the
   known constant y into a multiply (section 4.4). */
#define BENCH(NAME, T, C, E, Y)                                                                          \
static T NAME##_lat(void) { T x = 3, y = Y; asm volatile("" : "+" C(y)); for (long i = 0; i < N; i++) { x = E(x, y); asm volatile("" : "+" C(x)); } return x; } \
static T NAME##_tp(void)  { T a = 3, b = 4, c = 5, d = 6, e = 7, f = 8, g = 9, h = 10, y = Y; asm volatile("" : "+" C(y)); \
    for (long i = 0; i < N; i++) { a = E(a, y); b = E(b, y); c = E(c, y); d = E(d, y); e = E(e, y); f = E(f, y); g = E(g, y); h = E(h, y); \
        asm volatile("" : "+" C(a), "+" C(b), "+" C(c), "+" C(d), "+" C(e), "+" C(f), "+" C(g), "+" C(h)); } return a + b + c + d + e + f + g + h; }

#define ADD(x, y)  ((x) + (y))
#define MUL(x, y)  ((x) * (y))
#define DIV(x, y)  ((x) / (y) + 0x4000000000000000ull)       /* + keeps the dividend large (one extra 1-cycle add) */
#define DIVS(x, y) ((x) / (y) + 100)                          /* small dividend: about 100 */
#define DIV32(x, y) ((x) / (y) + 0x40000000u)
#define FDIV(x, y) ((x) / (y))
#define FMA(x, y)  fma((x), (y), 1e-9)
#define DSQRT(x, y) (sqrt(x) + 1000.0)                          /* + keeps the operand varied (one extra add) */
#define FSQRT(x, y) (sqrtf(x) + 1000.0f)
#define POP(x, y)  ((uint64_t)__builtin_popcountll(x) + 0 * (y))
#define LZ(x, y)   ((uint64_t)_lzcnt_u64(x) + 0 * (y))              /* leading zeros */
#define MULHI(x, y) ((uint64_t)(((unsigned __int128)(x) * (y)) >> 64))   /* high half of a 64 x 64 multiply */

BENCH(iadd,  uint64_t, "r", ADD, 7)
BENCH(imul,  uint64_t, "r", MUL, 7)
BENCH(ipop,  uint64_t, "r", POP, 7)
BENCH(ilz,   uint64_t, "r", LZ, 7)
BENCH(imulhi, uint64_t, "r", MULHI, 0x9E3779B97F4A7C15ull)
BENCH(idiv,  uint64_t, "r", DIV, 3)
BENCH(idivs, uint64_t, "r", DIVS, 7)
BENCH(idiv32, uint32_t, "r", DIV32, 3)
BENCH(dadd,  double,   "x", ADD, 1.0000001)
BENCH(dmul,  double,   "x", MUL, 1.0000001)
BENCH(dfma,  double,   "x", FMA, 1.0000001)
BENCH(ddiv,  double,   "x", FDIV, 1.0000001)
BENCH(fdiv,  float,    "x", FDIV, 1.0000001f)
BENCH(dsqrt, double,   "x", DSQRT, 1.0)
BENCH(fsqrt, float,    "x", FSQRT, 1.0f)

#define BEST(f) ({ double b = 1e9; for (int r = 0; r < 3; r++) { double t0 = now(); f(); double t = now() - t0; if (t < b) b = t; } b; })
#define ROW(NAME, LABEL) do { double l = BEST(NAME##_lat) / N * 1e9, tp = BEST(NAME##_tp) / N / 8 * 1e9;      \
    printf("%-26s %6.2f ns = %5.1f cycles    %6.3f ns = %5.2f cycles per op\n", LABEL, l, l / base, tp, tp / base); } while (0)

int main(void) {
    iadd_lat();                                                                      /* warm up the clock */
    double base = BEST(iadd_lat) / N * 1e9;                                          /* one dependent add = 1 cycle */
    printf("dependent add: %.3f ns, so 1 cycle = %.3f ns (%.2f GHz)\n", base, base, 1 / base);
    printf("%-26s %-28s %s\n", "", "latency (dependent)", "throughput (independent)");
    ROW(iadd, "add r64");        ROW(imul, "imul r64");        ROW(imulhi, "mul, high half");      ROW(ipop, "popcnt r64");     ROW(ilz, "lzcnt r64");
    ROW(idiv, "div r64 (+1 add)");   ROW(idivs, "div r64, small operands");   ROW(idiv32, "div r32 (+1 add)");
    ROW(dadd, "addsd");          ROW(dmul, "mulsd");         ROW(dfma, "fma (double)");
    ROW(ddiv, "divsd");          ROW(fdiv, "divss");
    ROW(dsqrt, "sqrtsd (+1 add)");   ROW(fsqrt, "sqrtss (+1 add)");
    return 0;
}
```

```text
dependent add: 0.310 ns, so 1 cycle = 0.310 ns (3.23 GHz)
                           latency (dependent)          throughput (independent)
add r64                      0.31 ns =   1.0 cycles     0.076 ns =  0.25 cycles per op
imul r64                     0.90 ns =   2.9 cycles     0.306 ns =  0.99 cycles per op
mul, high half               1.21 ns =   3.9 cycles     0.309 ns =  1.00 cycles per op
popcnt r64                   0.90 ns =   2.9 cycles     0.302 ns =  0.98 cycles per op
lzcnt r64                    0.90 ns =   2.9 cycles     0.302 ns =  0.97 cycles per op
div r64 (+1 add)             4.59 ns =  14.8 cycles     3.059 ns =  9.88 cycles per op
div r64, small operands      4.52 ns =  14.6 cycles     3.055 ns =  9.87 cycles per op
div r32 (+1 add)             3.62 ns =  11.7 cycles     1.835 ns =  5.93 cycles per op
addsd                        0.62 ns =   2.0 cycles     0.150 ns =  0.49 cycles per op
mulsd                        1.21 ns =   3.9 cycles     0.151 ns =  0.49 cycles per op
fma (double)                 1.22 ns =   3.9 cycles     0.155 ns =  0.50 cycles per op
divsd                        4.27 ns =  13.8 cycles     1.218 ns =  3.93 cycles per op
divss                        3.35 ns =  10.8 cycles     0.905 ns =  2.92 cycles per op
sqrtsd (+1 add)              6.33 ns =  20.5 cycles     1.812 ns =  5.85 cycles per op
sqrtss (+1 add)              4.49 ns =  14.5 cycles     0.900 ns =  2.91 cycles per op
```

The clock estimate (the first line) moved between 3.14 and 3.31 GHz over my runs, and the cycle counts move with it by a few percent; the table in section 1 gives the typical values. The divide and square-root rows include one extra add per iteration.

**`denorm2.c`: the cost of subnormal operands and results.**

```c
/* Subnormal numbers: time per operation for multiply and add, default mode and flush-to-zero.   build: gcc -O1 denorm2.c -o denorm2   run: taskset -c 1 ./denorm2 */
#include <stdio.h>
#include <time.h>
#include <xmmintrin.h>
#include <pmmintrin.h>

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
#define N 20000000L

#define BENCH(name, insn) \
static double name(float a, float b) {                       /* time per `insn`, restarting from a each time */ \
    double t0 = now(); float x = a; \
    for (long i = 0; i < N; i++) { x = a; asm volatile(insn " %1, %0" : "+x"(x) : "x"(b)); } \
    double t = now() - t0; asm volatile("" :: "x"(x)); return t / N * 1e9; }
BENCH(mul, "mulss")
BENCH(add, "addss")

static void row(const char *label, double ns) { printf("  %-46s %6.2f ns\n", label, ns); }

int main(void) {
    for (int ftz = 0; ftz < 2; ftz++) {
        if (ftz) { _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON); _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON); }
        printf("%s\n", ftz ? "flush-to-zero and denormals-are-zero ON" : "default");
        row("mul  normal x normal -> normal",        mul(1.5f, 0.75f));
        row("mul  normal x normal -> subnormal",     mul(1.5e-38f, 0.5f));
        row("mul  subnormal x normal -> subnormal",  mul(1e-40f, 0.75f));
        row("mul  subnormal x large  -> normal",     mul(1e-40f, 1e30f));
        row("add  normal + normal -> normal",        add(1.5f, 2.0f));
        row("add  normal + normal -> subnormal",     add(1.5e-38f, -1.4e-38f));
        row("add  subnormal + subnormal -> subnormal", add(1e-40f, 1e-40f));
        row("add  subnormal + normal -> normal",     add(1e-40f, 1.0f));
    }
    return 0;
}
```

```text
default
  mul  normal x normal -> normal                   0.31 ns
  mul  normal x normal -> subnormal               38.75 ns
  mul  subnormal x normal -> subnormal            39.16 ns
  mul  subnormal x large  -> normal               39.99 ns
  add  normal + normal -> normal                   0.61 ns
  add  normal + normal -> subnormal               41.19 ns
  add  subnormal + subnormal -> subnormal          0.64 ns
  add  subnormal + normal -> normal                0.63 ns
flush-to-zero and denormals-are-zero ON
  mul  normal x normal -> normal                   0.33 ns
  mul  normal x normal -> subnormal                0.31 ns
  mul  subnormal x normal -> subnormal             0.32 ns
  mul  subnormal x large  -> normal                0.30 ns
  add  normal + normal -> normal                   0.60 ns
  add  normal + normal -> subnormal                0.61 ns
  add  subnormal + subnormal -> subnormal          0.62 ns
  add  subnormal + normal -> normal                0.60 ns
```

(One of three runs; the table in section 5.5 gives the ranges.)

---

## Cheat sheet

- **Costs (this core, measured):** add 1 cycle (4 per cycle); `imul` 3 (1 per cycle); `mul` high half 4; `popcnt`/`lzcnt` 3 (1); 32-bit divide 11 to 12 (one per 6); 64-bit divide 14 to 15 (one per 10); FP add 2 (two per cycle); FP multiply and FMA 4 (two per cycle); FP divide 11 (`float`) or 14 to 15 (`double`); FP square root 13 (`float`) or 19 (`double`); a subnormal assist about 130.
- **Adders:** generate `G = a·b`, propagate `P = a^b`; combine `(G,P) ∘ (G',P') = (G + P·G', P·P')`, associative, so carries are a parallel prefix with log₂ n levels. 64 bits: Kogge-Stone 6 levels / 321 cells, Sklansky 6 / 192 (fan-out 32), Brent-Kung 11 / 120. Carry-skip and carry-select grow like √n. **Carry-save (3:2)**: sum = XOR, carry = majority shifted; one full-adder delay at any width.
- **Multipliers:** shift-and-add (n cycles); radix-4 Booth gives n/2 rows with digits {−2..2}; compressor trees reach 2 rows in about log₁.₅ n stages (8 for 64 × 64 with Booth); a prefix adder finishes; 3 pipeline stages. Low half is the same for signed and unsigned; the high half is not. Constants become shifts and adds.
- **Division:** restoring and non-restoring (one bit per step), SRT (redundant digits from the top bits, several bits per cycle), Newton-Raphson `x(2 − d·x)` doubles correct bits per step. By a constant: `(x * ceil(2^k/d)) >> k`, valid when `x·e < 2^k`; signed `/ 2^n` needs a sign fix-up; `x % c = x − c·(x/c)`. Divide by zero: trap on x86, 0 on Arm, all ones on RISC-V.
- **Floating point:** multiply = xor signs, add exponents, multiply significands, normalise by at most 1, round. Add = compare, align, add/subtract, normalise, round; far path and near path. **Guard, round, sticky**; round up when G and (R or S or L); ties to even. FMA: one rounding, error-free product `fma(a, b, -a*b)`; contraction changes results across machines. Subnormal input (multiply) or result: assist, 100 times slower; fix with flush-to-zero. `==` and `max` need NaN care (x86 `maxss` returns the second operand).
- **Flags:** Z = NOR(result), N = top bit, C = carry out, V = carry into top ⊕ carry out. Unsigned `<` is C = 0; signed `<` is N ≠ V. x86 CF after `sub` = borrow; Arm C = no borrow; RISC-V has no flags (`slt`, `sltu`, branch on two registers). Overflow test in C: `__builtin_add_overflow`.
- **Bit instructions:** `popcnt`, `lzcnt`/`tzcnt`, `bswap`, `pext`/`pdep`; `__builtin_clz(0)` and `__builtin_ctz(0)` are undefined.
- **Honesty:** the cycle counts, the subnormal and `max` results, the divide-by-constant code and the FMA results were measured or compiled here; the gate counts are from a model; the Arm and RISC-V code was only compiled; instruction names marked *standard* come from the architecture manuals and were not run.

**Next:** tutorial 06, ISA Design (what an instruction set defines, the five ISA classes, instruction formats and addressing modes, x86 against Arm against RISC-V, RISC and CISC). Say "next" when you are ready.
