# CO 04: Sequential Logic and State Machines

*Part of the [Computer Organization plan](00_plan.md) · Skim tutorial · Time: about 2 hours (1 reading, 1 problems) · You need: [tutorial 03](03_boolean_logic_and_combinational_circuits.md)*

By the end of this tutorial you can:

- explain how a circuit can remember a bit, and the difference between a **latch** and a **flip-flop**;
- trace a D, T or JK flip-flop, a shift register and a counter, cycle by cycle;
- explain **setup time, hold time and clock skew**, and compute the fastest clock a circuit allows;
- explain **metastability** and why signals from outside the clock domain go through a synchronizer;
- design a small **finite state machine** (Moore or Mealy) from a description to a state table and equations.

This is a **Skim** tutorial, and the shortest in Part 1. It gives you the vocabulary for registers, pipelines and control logic in the tutorials that follow.

---

## 1. Why a CPU needs memory inside the logic

Tutorial 03's circuits have no memory: the outputs are a function of the inputs *right now*. A CPU must remember things: the program counter, the registers, the result of the last step. A circuit whose outputs depend on **past** inputs as well is **sequential**.

Every sequential design has the same shape:

```text
          +---------------------+
   inputs |  combinational      |  outputs
  ------->|  logic (tutorial 03)|----------->
          +---+-------------+---+
              ^             |
     current  |             | next state
      state   |             v
          +---+-------------+---+
          |  state registers    |<---- clock
          +---------------------+
```

The registers hold the **state**. The combinational logic computes the **next state** (and the outputs) from the state and the inputs. On each tick of the **clock**, all registers load their new values at the same moment. Everything in a CPU, from a counter to a whole pipeline, is this picture repeated.

---

## 2. Latches: memory from feedback

Remembering a bit needs a loop: an output wired back to an input. The simplest is the **SR latch**: two NOR gates, each gate's output wired to one input of the other ("cross-coupled"). Its inputs are S (set) and R (reset), and its outputs are Q and its opposite Qb:

```text
Q  = NOR(R, Qb)
Qb = NOR(S, Q)
```

| S | R | Effect |
|---|---|---|
| 1 | 0 | **Set**: Q becomes 1 |
| 0 | 1 | **Reset**: Q becomes 0 |
| 0 | 0 | **Hold**: Q keeps its value |
| 1 | 1 | Invalid: both outputs go to 0, and the result is unpredictable when you let go |

Trace from Q = 0: apply (S,R) = (1,0): Qb = NOR(1,0) = 0, then Q = NOR(0,0) = 1. Now set (0,0): Q = NOR(0, Qb=0) = 1, so Q stays 1. That *is* memory: the inputs went back to 0 and the output kept the answer. Apply (0,1): Q = 0, and (0,0) afterwards holds 0. Sequence of Q: **1, 1, 0, 0**.

The **D latch** removes the invalid input. It has data D and an enable E: while E = 1 the latch is **transparent** (Q follows D); while E = 0 it **holds**. Inside, S = D·E and R = !D·E, so S and R can never both be 1.

A latch is **level-sensitive**: for the whole time E is 1, changes on D flow straight through. That is a problem in a circuit where the output feeds back to the input, because the signal can race around the loop while the latch is open.

---

## 3. Flip-flops: capture on the clock edge

A **flip-flop** is **edge-triggered**: it looks at D only at one instant, the rising edge of the clock, and holds the value until the next edge. It is built from two D latches back to back, the first open while the clock is low (the **master**), the second open while the clock is high (the **slave**). At the rising edge the master closes (freezing D) just as the slave opens (passing that frozen value to Q). The input is sampled at one moment, so nothing can race through.

The D flip-flop, in one line:

```text
at every rising clock edge:   Q  <=  D
```

A trace (Q starts at 0; each row is one clock edge, D is its value just before that edge):

| Edge | D | Q after the edge |
|---|---|---|
| 1 | 1 | 1 |
| 2 | 1 | 1 |
| 3 | 0 | 0 |
| 4 | 1 | 1 |
| 5 | 0 | 0 |
| 6 | 0 | 0 |

Q after each edge is simply the D that was waiting at the edge. The flip-flop has delayed D by one clock cycle.

### T and JK flip-flops

Other types are the D flip-flop plus a little logic:

| Type | Behaviour | Built from D as |
|---|---|---|
| **T** (toggle) | T = 0 hold; T = 1 flip Q | `D = Q ^ T` |
| **JK** | 00 hold; 01 reset (Q=0); 10 set (Q=1); 11 toggle | `D = J·!Q + !K·Q` |

T flip-flop trace with T = 1, 1, 0, 1, 1 starting at Q = 0: Q after each edge is **1, 0, 0, 1, 0**. Toggle flip-flops are the natural building block for counters. In modern designs the D flip-flop is what you actually use; the others are mostly teaching tools.

Reset: real flip-flops also have a **reset** input that forces Q to 0 (**synchronous** reset acts at a clock edge, **asynchronous** reset acts immediately). Every register that matters has one, so that the circuit starts in a known state.

---

## 4. Timing: how fast can the clock go?

A flip-flop can only sample a stable input. Two numbers describe "stable":

- **Setup time (t_setup):** D must be steady for this long **before** the clock edge.
- **Hold time (t_hold):** D must stay steady for this long **after** the edge.

Add the **clock-to-Q delay (t_cq):** after the edge, the new output appears at Q only after this delay.

Between two flip-flops sits combinational logic. Launch from the first flip-flop at an edge, and the signal must get through the logic and settle at the second flip-flop's D before the next edge:

```text
T_clock  >=  t_cq  +  t_logic(longest path)  +  t_setup  +  t_skew
```

**Clock skew** is the difference in arrival time of the same clock edge at two flip-flops (the wires are different lengths). Example: t_cq = 40 ps, longest logic path 260 ps, t_setup = 30 ps, skew 10 ps:

```text
T_clock >= 40 + 260 + 30 + 10 = 340 ps     ->   f_max = 1 / 340 ps  =  about 2.94 GHz
```

The longest path is the **critical path** from tutorial 03; it sets the clock. If a design misses its target, you shorten that path or cut it in two with a register (that is pipelining, tutorial 09).

The other side is the **hold** check. The *shortest* path must not be so fast that the new data arrives while the receiving flip-flop is still holding its old value:

```text
t_cq  +  t_logic(shortest path)  >=  t_hold  +  t_skew
```

Example with t_hold = 45 ps, skew 10 ps, t_cq = 40 ps, shortest path 5 ps: 40 + 5 = 45, but 45 + 10 = 55, so the check fails by 10 ps. The fix is to add at least 10 ps of delay (a buffer) on that path. **Slowing the clock does not help a hold violation**, because the clock period does not appear in the hold inequality. A setup violation is fixed by a slower clock; a hold violation is a design bug that stays at any speed.

### Metastability

If D changes right inside the setup/hold window around the edge, the flip-flop may not settle cleanly. Its output can hover at a voltage that is neither 0 nor 1 for an unpredictable time before it falls one way. That is **metastability**.

Inside one clock domain the timing rules above keep every input stable at the edge, so it does not happen. It happens when a signal arrives that **has no relationship to the clock**: a button press, an external interrupt pin, or data coming from a block running on a different clock. You cannot eliminate it, you can only make it very unlikely to reach the logic. The standard remedy is a **two-flip-flop synchronizer**:

```text
 async input ----> [ FF1 ] ----> [ FF2 ] ----> use this one in the design
                      ^             ^
                      +---- clk ----+
```

FF1 may go metastable, but it has a whole clock period to settle before FF2 samples it; the chance that it is still undecided a full cycle later is tiny. The price is that the signal is delayed by two clocks. (A mechanical button has a second, separate problem: its contacts **bounce** for a few milliseconds, which is fixed by a debounce filter or a short software delay, not by a synchronizer.) You will meet this again with interrupt lines and GPIO inputs on embedded chips (tutorial 26).

---

## 5. Registers and shift registers

An **n-bit register** is n D flip-flops sharing one clock. To make it **load only when told**, put a 2:1 mux in front of each D: when `load = 1` the mux passes the new data, otherwise it feeds Q back, so the register keeps its value:

```text
D_in = load ? new_value : Q
```

A CPU's **register file** is an array of such registers: a decoder picks which register is written (write-enable), and muxes pick which ones are read.

A **shift register** is flip-flops in a chain, each Q feeding the next D. Every clock the contents move one place. Because every flip-flop samples before any output changes (the clock-to-Q delay), each stage captures its neighbour's *old* value, which is what makes the shift work.

Four-bit shift register, serial input 1, 0, 1, 1, new bit entering at the right (bit 0):

| Edge | Input | Register |
|---|---|---|
| start | | 0000 |
| 1 | 1 | 0001 |
| 2 | 0 | 0010 |
| 3 | 1 | 0101 |
| 4 | 1 | 1011 |

Types by how data enters and leaves: **SISO** (serial in, serial out: a delay line), **SIPO** (serial in, parallel out: collect bits into a byte, as a UART or SPI receiver does), **PISO** (parallel in, serial out: transmit a byte bit by bit), **PIPO** (parallel in, parallel out: an ordinary register). A shift left by one is also a multiply by two, as seen in tutorial 02.

---

## 6. Counters

A **counter** is a register that goes to the next value each clock.

**Synchronous counter.** All flip-flops share the same clock. The next state is the current state plus one, so it is a register fed by an incrementer (an adder with one input fixed at 1):

```text
Q  <=  Q + 1
```

In toggle-flip-flop terms: bit *i* toggles on this clock if **all lower bits are 1**. Counting 000, 001, 010, 011, 100, ...: bit 0 toggles every time; bit 1 toggles when bit 0 is 1; bit 2 toggles when bits 1 and 0 are both 1.

**Ripple (asynchronous) counter.** Each flip-flop is clocked by the output of the previous one. It needs no extra gates, but the change ripples through the stages one after another, so the outputs are briefly wrong and the delay adds up with the width. It is simple and cheap and is rarely used in a synchronous CPU.

**Mod-N counter.** A counter that wraps early. A decimal (BCD) counter counts 0 to 9 and returns to 0:

```text
Q  <=  (Q == 9) ? 0 : Q + 1          sequence 0, 1, 2, ..., 9, 0, 1, ...
```

**Divide by two.** A T flip-flop with T = 1 toggles on every clock, so its output runs at half the clock frequency. Chain *n* of them and you divide by 2ⁿ: that is the ripple counter above, and how simple clock dividers work.

**Ring and Johnson counters.** Feed a shift register's output back into its input. A **ring counter** circulates a single 1 (0001, 0010, 0100, 1000, ...), a one-hot state that needs no decoder. A **Johnson counter** feeds back the *inverted* output and has twice as many states (0000, 0001, 0011, 0111, 1111, 1110, 1100, 1000, back to 0000).

**LFSR (linear-feedback shift register).** A shift register whose new input bit is the XOR of a few chosen bits (the *taps*). With well-chosen taps, an *n*-bit LFSR steps through all 2ⁿ − 1 non-zero values in a fixed pseudo-random order before repeating. For 4 bits with taps on bits 3 and 2 (the polynomial x⁴ + x³ + 1), the new bit enters at the right, starting at 0001:

```text
0001 0010 0100 1001 0011 0110 1101 1010 0101 1011 0111 1111 1110 1100 1000 -> 0001     (15 states)
```

An LFSR is tiny (n flip-flops and one XOR), so hardware uses it wherever it needs cheap pseudo-random bits or a running checksum: **CRC** (the frame check sequence of Ethernet and Wi-Fi frames is a CRC-32), **scramblers** (the 802.11 OFDM scrambler is a 7-bit LFSR, x⁷ + x⁴ + 1, with period 127), and test-pattern generators. The all-zero state is a trap: with XOR feedback it stays at zero forever.

The CPU's **program counter** from tutorial 01 is this idea: a register holding the address of the next instruction, loaded with `PC + 4` each step, or with a branch target when the control logic says jump (a mux in front of the register).

---

## 7. Finite state machines

A **finite state machine (FSM)** is the general sequential circuit from section 1: a state register, next-state logic and output logic. It appears as the control logic of a CPU, memory controllers, protocol engines and cache coherence (tutorial 19). Two styles:

- **Moore**: outputs depend only on the **current state**. Outputs change only at clock edges (cleaner).
- **Mealy**: outputs depend on the current state **and the current input**. They react in the same cycle as the input and usually need fewer states, but can change mid-cycle when the input changes.

**Design procedure:** (1) list the states and what each means; (2) draw the diagram or write the state table; (3) choose a binary encoding for the states (n states need ceil(log₂ n) flip-flops; the alternative **one-hot** uses one flip-flop per state and simplifies the logic, common in FPGAs); (4) derive next-state and output equations (with the K-maps from tutorial 03); (5) build it.

### Worked example: detect the bit pattern 101

Input arrives one bit per clock. The output should be 1 whenever the **last three bits seen were 1, 0, 1**. Overlapping matches count (in `10101`, both the first and the last three bits match).

States, as a **Moore** machine:

| State | Meaning |
|---|---|
| S0 | no useful progress |
| S1 | last bit was `1` |
| S2 | last two bits were `10` |
| S3 | last three bits were `101` (output = 1) |

Transitions (next state for input 0, input 1):

| State | input 0 | input 1 | Output |
|---|---|---|---|
| S0 | S0 | S1 | 0 |
| S1 | S2 | S1 | 0 |
| S2 | S0 | S3 | 0 |
| S3 | S2 | S1 | **1** |

Why S3 goes to S1 on a 1 and to S2 on a 0: the last bit of `101` is a 1, which may be the start of a new `101`. After `101` then 0 the last two bits are `10`, which is state S2.

Run it on the input `1 0 1 0 1 1 0 1`:

| Input | 1 | 0 | 1 | 0 | 1 | 1 | 0 | 1 |
|---|---|---|---|---|---|---|---|---|
| State after | S1 | S2 | S3 | S2 | S3 | S1 | S2 | S3 |
| Output | 0 | 0 | **1** | 0 | **1** | 0 | 0 | **1** |

Three matches, at positions 3, 5 and 8, which is right: `101` appears at 1 to 3, 3 to 5 and 6 to 8.

The **Mealy** version needs only three states, S0 ("nothing"), S1 (`1`), S2 (`10`), and produces the output on the transition: from S2 with input 1 the output is 1 and the next state is S1. Its output appears in the **same cycle** as the final `1`; the Moore output appears one cycle later (it needs to be in S3 first). Fewer states versus a clean registered output is the usual trade.

---

## 8. A read-only taste of Verilog

Nobody draws flip-flops by hand; designers describe hardware in a language such as Verilog and let a tool produce the gates. This is a 4-bit counter with an enable and a reset:

```verilog
module counter(input clk, input reset, input en, output reg [3:0] q);
  always @(posedge clk) begin
    if (reset)   q <= 4'd0;
    else if (en) q <= q + 4'd1;
  end
endmodule
```

Read it as: *on every rising clock edge: if reset, clear; otherwise if enabled, add one.* The `<=` means all registers update together at the edge, just like the flip-flops above. The tool turns this into four flip-flops, an incrementer and the load-enable muxes of section 5. You do not need to write Verilog for this track; recognising what it describes is enough.

---

## 9. Where you will meet these

| Idea | Where |
|---|---|
| Register with load enable | Every CPU register; the program counter |
| Register file | Reading and writing the CPU's general registers (tutorials 06, 08) |
| Pipeline registers between stages | Pipelining (tutorial 09) |
| FSMs | Control unit (08), cache coherence states (19), DRAM controller (16) |
| Synchronizer | Interrupt and GPIO inputs (26), data crossing between clock domains |
| Shift registers and LFSRs | UART and SPI, serial links, CRC and scrambler hardware |
| Setup and the critical path | Why clock frequency is limited (28) |

---

## 10. Mistakes that cost points

1. **Mixing up latch and flip-flop.** A latch is level-sensitive (transparent while enabled); a flip-flop samples on an edge.
2. **Thinking the output changes at the clock edge itself.** Q changes after the clock-to-Q delay, which is why a shift register moves one place, not all the way through.
3. **Trying to fix a hold violation by slowing the clock.** The period does not appear in the hold check; add delay on the short path.
4. **Feeding an asynchronous signal straight into logic.** Use a synchronizer.
5. **Treating a Mealy output like a registered one.** It depends on the input and can change in the middle of a cycle.
6. **Forgetting a reset or initial state.** Flip-flops power up with unknown values.
7. **Wrong bit count.** n states need ceil(log₂ n) flip-flops (binary encoding), or n with one-hot.

---

## 11. Questions you will be asked

**What is the difference between a latch and a flip-flop?** A latch is level-sensitive (follows its input while enabled). A flip-flop is edge-triggered: it samples at the clock edge only. A flip-flop is two latches in a master-slave arrangement.

**What are setup and hold time?** Setup: the input must be stable for a minimum time before the clock edge. Hold: it must stay stable for a minimum time after. Violating either can capture a wrong or undefined value.

**What decides the maximum clock frequency?** The critical path: T ≥ t_cq + longest logic delay + t_setup + skew.

**What is metastability and how do you deal with it?** A flip-flop caught in its setup/hold window may sit at an undefined voltage for a while. It happens when asynchronous signals meet a clock. You cannot prevent it, but a chain of two (or more) flip-flops makes the probability of it reaching the logic very small.

**Moore versus Mealy?** Moore outputs depend only on the state, so they are stable and registered, usually with more states. Mealy outputs depend on state and input, so they react a cycle sooner with fewer states but may glitch.

**How many flip-flops for a machine with 10 states?** ceil(log₂ 10) = 4 with binary encoding, 10 with one-hot.

**What is an LFSR used for?** A shift register with XOR feedback that cycles through 2ⁿ − 1 states: cheap pseudo-random numbers, CRC checksums, scramblers and test patterns.

**Synchronous versus asynchronous counter?** Synchronous: all flip-flops share the clock and change together. Asynchronous (ripple): each is clocked by the previous stage, so delays add up and outputs glitch.

---

## 12. Problems

**How to use this section.** Try each problem before looking at the answers at the bottom. A, B and C are paper problems (about 20 minutes each), D is a short C program (about 30 minutes).

### Problem A (paper): trace the circuits

(a) An SR latch (two NOR gates) starts with Q = 0. Apply (S,R) in this order: (1,0), (0,0), (0,1), (0,0). What is Q after each step?
(b) A T flip-flop built as `D = Q ^ T` starts at Q = 0. T takes the values 1, 1, 0, 1, 1 on successive clock edges. What is Q after each edge?
(c) A 4-bit shift register starts at 0000 and shifts left, taking a new bit into bit 0 each edge. The serial input is 1, 1, 0, 1. Show the register after each edge.

*Where:* paper. *How:* for (a) work out Qb and Q from the two NOR equations, one step at a time. For (b) compute the new Q as the old Q XOR T. For (c) shift left and put the input bit in the rightmost position.

### Problem B (paper): timing

(a) t_cq = 40 ps, longest logic path = 260 ps, t_setup = 30 ps, skew = 10 ps. What is the minimum clock period and the maximum frequency?
(b) The logic path in (a) is redesigned to be 20% shorter. What is the new maximum frequency?
(c) t_hold = 45 ps, skew = 10 ps, t_cq = 40 ps, shortest logic path = 5 ps. Is there a hold violation? If so, by how much, and what do you do about it?
(d) Draw a synchronizer for a button wired to a chip's pin, and say what each flip-flop is for.

*Where:* paper and a calculator. *How:* (a) add the four terms. (b) 20% shorter means 0.8 × 260 ps; redo the sum. (c) compare the left side t_cq + t_logic(shortest) with the right side t_hold + skew. (d) two flip-flops in series, both on the chip's clock.

### Problem C (paper): design a state machine

Use the 101 detector of section 7 (Moore, overlapping).
(a) Encode the states as S0 = 00, S1 = 01, S2 = 10, S3 = 11 (bits Q1 Q0) and write the next-state table with the input x.
(b) Derive D1, D0 (the inputs of the two D flip-flops) and the output Z as Boolean equations. Use a K-map or the algebra from tutorial 03.
(c) Change the machine so that matches may **not** overlap (after a match, start over). Which transition changes?

*Where:* paper. *How:* for (a) list all eight (Q1 Q0, x) rows and read the next state off the table in section 7. For (b) each D is 1 on the rows where that bit of the next state is 1. For (c) ask which bits of `101` may be reused.

### Problem D (hands-on C): run the state machine

Write a short C program that implements the Moore 101 detector with a state variable and a transition table (`next[state][bit]`). Feed it the string `"10101101"` and print the output after each bit. It should print `0 0 1 0 1 0 0 1`. Then run it on `"1010101"` and check by hand.

*Where:* your editor and terminal. *How:* keep `int state = 0;` and a 4x2 array; for each character, set `state = next[state][bit]` and print `state == 3`. Firmware state machines are written exactly this way.

---

## Answers

Try before reading.

### Problem A

(a) Q after each step: **1, 1, 0, 0**. Set makes Q = 1; (0,0) holds it; reset makes Q = 0; (0,0) holds it.

(b) Q after each edge: **1, 0, 0, 1, 0** (T = 1 toggles, T = 0 holds).

(c)

| Edge | Input | Register |
|---|---|---|
| start | | 0000 |
| 1 | 1 | 0001 |
| 2 | 1 | 0011 |
| 3 | 0 | 0110 |
| 4 | 1 | 1101 |

### Problem B

(a) T ≥ 40 + 260 + 30 + 10 = **340 ps**, so f_max = 1 / 340 ps ≈ **2.94 GHz**.

(b) Logic is 0.8 × 260 = 208 ps. T ≥ 40 + 208 + 30 + 10 = 288 ps, so f_max ≈ **3.47 GHz**. A 20% shorter logic path gave about an 18% faster clock (the flip-flop overhead does not shrink).

(c) Left side 40 + 5 = 45 ps; right side 45 + 10 = 55 ps. 45 < 55, so there **is** a violation of **10 ps**. Fix: add at least 10 ps of delay (a buffer) on the short path. Slowing the clock does not help, because the period is not in the hold inequality.

(d) `button -> [FF1] -> [FF2] -> design logic`, both flip-flops on the chip's clock. FF1 samples the asynchronous signal and may go metastable. FF2 samples FF1 a whole cycle later, when FF1 has almost certainly settled; the rest of the design only ever sees FF2.

### Problem C

(a) With S0 = 00, S1 = 01, S2 = 10, S3 = 11:

| Q1 Q0 | x | Next Q1 Q0 |
|---|---|---|
| 00 | 0 | 00 |
| 00 | 1 | 01 |
| 01 | 0 | 10 |
| 01 | 1 | 01 |
| 10 | 0 | 00 |
| 10 | 1 | 11 |
| 11 | 0 | 10 |
| 11 | 1 | 01 |

(b) Next Q1 is 1 in the rows (01,0), (10,1) and (11,0):

```text
D1 = !Q1·Q0·!x + Q1·!Q0·x + Q1·Q0·!x  =  Q0·!x + Q1·!Q0·x
```

Next Q0 is 1 in exactly the rows where x = 1, so **D0 = x**: the newest bit is always the last bit of the history. The output is **Z = Q1·Q0** (state S3).

(c) Only S3's transition on a 0 changes: it goes to **S0** instead of S2, since the last 1 of the match is consumed and may not start another `10`. On the input `10101` the overlapping machine reports matches at positions 3 and 5, the non-overlapping one only at 3.

### Problem D

```c
#include <stdio.h>

int main(void) {
    /* states: 0 = no progress, 1 = saw "1", 2 = saw "10", 3 = saw "101" */
    static const int next[4][2] = {
        /* S0 */ {0, 1},
        /* S1 */ {2, 1},
        /* S2 */ {0, 3},
        /* S3 */ {2, 1},
    };
    const char *in = "10101101";
    int state = 0;
    for (const char *p = in; *p; p++) {
        state = next[state][*p - '0'];
        printf("%d ", state == 3);          /* Moore output: 1 only in S3 */
    }
    printf("\n");
    return 0;
}
```

Output: `0 0 1 0 1 0 0 1`. For `"1010101"` the same machine gives `0 0 1 0 1 0 1`: matches end at positions 3, 5 and 7.

---

## Cheat sheet

- **Sequential** = combinational logic + state registers + a clock. Outputs depend on history.
- **SR latch**: set / reset / hold, S = R = 1 invalid. **D latch**: transparent while enabled. **D flip-flop**: `Q <= D` at the clock edge (two latches, master and slave).
- **T**: `D = Q ^ T`. **JK**: `D = J·!Q + !K·Q`.
- `T_clock >= t_cq + t_logic(longest) + t_setup + skew`. Hold: `t_cq + t_logic(shortest) >= t_hold + skew`. A slower clock fixes setup, not hold.
- **Metastability** happens with asynchronous inputs; use a two-flip-flop synchronizer.
- **Register** with load: `D = load ? new : Q`. **Shift register**: SISO, SIPO, PISO, PIPO.
- **Counter**: `Q <= Q + 1`; bit i toggles when all lower bits are 1. Mod-N wraps early. The PC is a counter with a load. A T flip-flop with T = 1 divides the clock by 2.
- **LFSR**: shift register with XOR feedback; *n* bits with good taps give 2ⁿ − 1 states. Used for CRC, scramblers (802.11: x⁷ + x⁴ + 1) and test patterns. Ring and Johnson counters are shift registers with feedback (one-hot, and 2n states).
- **FSM** = state register + next-state logic + output logic. **Moore**: output from state. **Mealy**: output from state and input (fewer states, reacts a cycle sooner). n states need ceil(log₂ n) flip-flops, or n with one-hot.

**Next:** tutorial 05, Computer Arithmetic and the ALU, is **Optional** (fast adders, multiplication, division; skip it on a first pass). Part 2 then starts with tutorial 06, ISA Design (Core). Say "next" to go to 06, or "05" if you want the arithmetic tutorial first.
