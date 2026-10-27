# CO 04: Sequential Logic and State Machines

*Part of the [Computer Organization plan](00_plan.md) · Skim tutorial · Time: about 2.5 hours (1.25 reading, 1.25 problems) · You need: [tutorial 03](03_boolean_logic_and_combinational_circuits.md)*

By the end of this tutorial you can:

- explain how a circuit can remember a bit, and the difference between a **latch** and a **flip-flop**;
- trace a D, T or JK flip-flop, a shift register and a counter, cycle by cycle;
- explain **setup time, hold time and clock skew**, and compute the fastest clock a circuit allows;
- explain **metastability**, why signals from outside the clock domain go through a synchronizer, and how to cross a bus, an event or a reset between two clock domains (Gray-coded pointers, an asynchronous FIFO, handshakes);
- design a small **finite state machine** (Moore or Mealy) from a description to a state table and equations.

This is a **Skim** tutorial. It gives you the vocabulary for registers, pipelines and control logic in the tutorials that follow, and section 9 (crossing clock domains) is the part to read closely if you target hardware, firmware or verification roles.

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

FF1 may go metastable, but it has a whole clock period to settle before FF2 samples it; the chance that it is still undecided a full cycle later is tiny. The price is that the signal is delayed by two clocks. (A mechanical button has a second, separate problem: its contacts **bounce** for a few milliseconds, which is fixed by a debounce filter or a short software delay, not by a synchronizer.) You will meet this again with interrupt lines and GPIO inputs on embedded chips ([tutorial 26](26_interrupts_exceptions_and_dma.md)). A synchronizer is safe for one slow-changing bit only; section 9 covers buses, pulses and resets.

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

## 9. Crossing clock domains

Sections 3 to 8 assumed one clock. A real chip has many: a core at 3 GHz, a memory controller on its own clock, PCIe, a 125 MHz Ethernet PHY, a UART baud generator, a sensor on a 32 kHz crystal. A signal that leaves one **clock domain** and enters another is **asynchronous** to the receiver, so the receiving flip-flop can sample it right at a transition and the guarantee of section 4 no longer holds. This is **clock-domain crossing (CDC)**. Interviewers for hardware, firmware and verification roles like it because the bugs are rare (once in billions of transfers), do not show up in ordinary simulation, and are caused by a few simple mistakes. The formula and the structures below are standard; the numbers in the tables come from a script and from a small model I ran (not from silicon), and the model is described where it is used.

### 9.1 How safe is a synchronizer?

The failure rate of a synchronizer follows a formula you should know in form, if not by heart:

```text
MTBF  =  e^(t_r / τ)  /  (T_W · f_clk · f_data)
```

Here t_r is the time the first flip-flop has to settle before anything uses its output, τ is a time constant of the flip-flop (tens of picoseconds), T_W is the width of the unsafe window around the clock edge, f_clk is the receiving clock and f_data the rate at which the input changes. The exponential is the point: every extra bit of settling time multiplies the mean time between failures. The table uses **assumed** values (τ = 30 ps, T_W = 20 ps, an input that changes 50 million times a second; a real design takes them from the cell library), computed with a script:

| Design | t_r | MTBF |
|---|---|---|
| 500 MHz, one flip-flop whose output feeds 1.5 ns of logic | 0.45 ns | 6.5 seconds |
| 500 MHz, two flip-flops back to back | 1.90 ns | 6 × 10²¹ s, about 2 × 10¹⁴ years |
| 2 GHz, two flip-flops | 0.40 ns | 0.3 seconds |
| 2 GHz, three flip-flops | 0.90 ns | 5 × 10⁶ s, about two months |

Three conclusions. One flip-flop with logic behind it fails within seconds. Each extra flip-flop adds a whole clock period to t_r and multiplies the MTBF by e^(T/τ) (1.7 × 10⁷ at 2 GHz), so the faster the clock, the more flip-flops a synchronizer needs. And the synchronizer solves *only* metastability: it costs two to three destination clock edges of latency and says nothing about the problems in the next two sections.

### 9.2 One bit is easy; a bus is not

A single bit that stays stable for at least a couple of destination clock periods goes through two flip-flops safely. A **pulse** shorter than one destination period may fall between two edges and vanish, so crossing from a fast clock to a slow one needs the pulse stretched or converted (section 9.5).

A **multi-bit value** is different. Suppose a counter goes from 3 to 4 (`011` to `100`) while a receiver samples it. Each bit has its own wire and its own flip-flop, and each captures either its old or its new value, independently:

| Bit | Old value (3) | New value (4) | What the receiver may capture |
|---|---|---|---|
| 2 | 0 | 1 | 0 or 1 |
| 1 | 1 | 0 | 1 or 0 |
| 0 | 1 | 0 | 1 or 0 |

Two choices for each of three bits is eight patterns: the receiver can see any value from 0 to 7, and only 3 and 4 were ever true. For a 4-bit counter going from 7 to 8, all 16 patterns are possible and 14 are wrong. So **synchronizing each bit with its own pair of flip-flops does not make a bus safe**. There are three correct ways to cross a bus: change only one bit at a time (Gray code, next), hold the data steady and signal "it is ready" with one synchronized bit (a handshake, section 9.5), or put the data in memory and cross only the pointers (an asynchronous FIFO, section 9.4).

### 9.3 Gray code: only one bit changes

You met the Gray code in tutorial 02: `g = b ^ (b >> 1)`, and consecutive values differ in one bit. For 3 bits the sequence is `000 001 011 010 110 111 101 100`, and the step from the last value back to the first also changes one bit. I checked widths 1 to 16 exhaustively. A receiver that samples a Gray-coded counter in the middle of a step sees either the old value or the new value, never anything else, because only one bit is in doubt. That is what makes a counter safe to carry across a clock boundary.

### 9.4 The asynchronous FIFO

A FIFO that is written in one clock domain and read in another is the standard way to move a stream. The data stay in a dual-port memory and never cross; only the pointers cross, and they cross as Gray code:

```text
  write clock domain                                          read clock domain

  data in --> [ memory: 2^n words ] --------------------------> data out
               ^ address = low n bits of wptr                    ^ address = low n bits of rptr

  wptr (binary, n+1 bits) -> Gray -> wgray ---[FF]->[FF]---> wgray_sync    empty = (rgray == wgray_sync)
  rgray_sync <---[FF]<-[FF]--- rgray <- Gray <- rptr (binary, n+1 bits)
  full = (wgray == { ~rgray_sync[n:n-1], rgray_sync[n-2:0] })
```

Each pointer has **one more bit than the address needs**, so the FIFO can tell "empty" (the pointers are equal) from "full" (the write pointer is exactly one lap, 2ⁿ, ahead). On Gray-coded pointers the two tests are: *empty* when the read pointer equals the synchronized write pointer, and *full* when the write pointer equals the synchronized read pointer with its **top two bits inverted**. Example for 8 entries (4-bit pointers): with w = 9 and r = 1 the Gray codes are `1101` and `0001`; inverting the top two bits of `0001` gives `1101`, so the FIFO is full (eight words in it). With w = 9 and r = 2 the Gray code of r is `0011`, which becomes `1111`, not equal, so it is not full (seven words). I checked both rules against the true occupancy for every reachable pair of pointers of an 8-entry FIFO.

Each side sees the *other* side's pointer a few clock edges late. That is safe, because a stale pointer is always conservative: the reader sees an older write pointer, so it thinks there are fewer words and reports empty a little too long; the writer sees an older read pointer, so it thinks there is less free space and reports full a little too long. Synchronization latency costs throughput and latency, never correctness.

**Sizing.** Suppose the writer pushes a burst of B words back to back at f_w and the reader drains at f_r. The words that pile up are B × (1 − f_r / f_w), plus a few for the flag latency, rounded up to a power of two. In my model of such a FIFO (a discrete-event simulation with random clock phases, two-flip-flop synchronizers and bit-by-bit resolution of samples taken at a change; a model, not silicon), a burst of 80 words at 100 MHz drained at 40 MHz peaked at 49 occupied entries (formula: 48), so 64 entries; a burst of 200 peaked at 121 (formula: 120), so 128.

**Why not a binary pointer?** The same model with binary pointers is the instructive part. A FIFO that moves one word per cycle on each side had *no* errors with either pointer type in 280 runs of 3,000 words: the glitch lasts one cycle and one word at most can be read or written in it, so binary got lucky. Give the reader a wide port (up to four words per cycle, as a burst reader or a DMA engine does) and the sampled difference between the pointers can be wrong by several words: **126 of the 280 runs failed with binary pointers (1,570 overflows, 5,711 reads of unwritten entries) and none with Gray pointers**. A design that works because of a one-line timing argument breaks when someone widens a port; Gray pointers need no argument.

### 9.5 Handshakes and pulses

Not everything is a stream. A configuration word or a status value can cross with a **four-phase request/acknowledge handshake**:

```text
source:       put the data on the bus, hold it, raise req
destination:  sees req (after its synchronizer), captures the data, raises ack
source:       sees ack (after its synchronizer), lowers req
destination:  sees req low, lowers ack
source:       sees ack low; the next transfer may start
```

The data bus is never sampled while it can change: it is stable from before `req` rises until after `ack` has come back. Each of the four steps crosses a synchronizer, so one transfer costs about four crossings. With a 100 MHz source, a 50 MHz destination and two-flip-flop synchronizers (two edges each) that is at least 2 × 2 × 20 ns + 2 × 2 × 10 ns = **120 ns per word, 8.3 million words per second**, against 50 million per second for a FIFO on the same slower clock. Handshakes are for rare, wide words; FIFOs are for streams.

A single-cycle **event** (an interrupt request, a start strobe) crosses as a **toggle**: the source flips a flip-flop on each event, the flip-flop's output crosses through the two-flip-flop synchronizer, and the destination XORs the synchronized value with its previous value to get a one-cycle pulse back. Events must be at least two or three destination periods apart, or two toggles merge into one.

### 9.6 Resets cross domains too

A reset that is released near a clock edge is itself an asynchronous event: some flip-flops see it released at this edge and others only at the next, so a state machine can start in a state that should be impossible (a one-hot machine with two hot bits). The standard cure is a **reset synchronizer**: assert the reset asynchronously (the system must reset even if the clock has stopped), but release it synchronously, through two flip-flops whose D input is tied to 1 and whose asynchronous reset is the incoming reset. Every clock domain gets its own.

### 9.7 What a firmware engineer sees

The same problems appear in registers. A 64-bit free-running counter read as two 32-bit halves can be **torn**: read `lo` just before the counter carries into `hi` and `hi` just after, and you assemble a value that never existed. With the counter at `0x00000000FFFFFFFF` it advances to `0x0000000100000000`; reading `lo` first and `hi` second gives `0x00000001FFFFFFFF`, 4 billion too big; reading `hi` first and `lo` second gives `0x0000000000000000`, 4 billion too small. The fix is the same idea as a Gray code, retrying until the high half did not change (Problem E). Many microcontrollers also have registers in a slow clock domain (a real-time clock, a low-power timer) where a write takes several slow clock periods to take effect, a read may return the old value, and a flag such as "synchronization busy" or "registers synchronized" says when it is safe to continue (*standard*; the name differs by chip). Reading the data sheet for such a flag is the firmware side of CDC.

### 9.8 How CDC bugs get found

Not by the usual tools. A functional simulation has no metastability: a sampled signal is 0 or 1, so a bad crossing simulates correctly. Static timing analysis does not check a path between two asynchronous clocks, because the designer tells it to ignore those paths (*false path* or *asynchronous clock group* constraints). So CDC is checked structurally, by **CDC lint tools** that find every crossing and verify that each one goes through an approved structure (two-flip-flop synchronizer, Gray-coded FIFO pointers, handshake), and by review. The rule for your own designs: every signal that crosses has exactly one of the structures above, written once and reused.

---

## 10. Where you will meet these

| Idea | Where |
|---|---|
| Register with load enable | Every CPU register; the program counter |
| Register file | Reading and writing the CPU's general registers (tutorials 06, 08) |
| Pipeline registers between stages | Pipelining (tutorial 09) |
| FSMs | Control unit (08), cache coherence states (19), DRAM controller (16) |
| Synchronizer, Gray-coded FIFO pointers, reset synchronizer | Interrupt and GPIO inputs (26), any block on its own clock (PCIe, DDR, Ethernet PHY), a 64-bit timer read in halves (9.7) |
| Shift registers and LFSRs | UART and SPI, serial links, CRC and scrambler hardware |
| Setup and the critical path | Why clock frequency is limited (28) |

---

## 11. Mistakes that cost points

1. **Mixing up latch and flip-flop.** A latch is level-sensitive (transparent while enabled); a flip-flop samples on an edge.
2. **Thinking the output changes at the clock edge itself.** Q changes after the clock-to-Q delay, which is why a shift register moves one place, not all the way through.
3. **Trying to fix a hold violation by slowing the clock.** The period does not appear in the hold check; add delay on the short path.
4. **Feeding an asynchronous signal straight into logic.** Use a synchronizer.
5. **Treating a Mealy output like a registered one.** It depends on the input and can change in the middle of a cycle.
6. **Forgetting a reset or initial state.** Flip-flops power up with unknown values.
7. **Wrong bit count.** n states need ceil(log₂ n) flip-flops (binary encoding), or n with one-hot.
8. **Synchronizing each bit of a bus separately.** The bits settle independently, so the receiver can see values that never existed. Use a Gray code, a handshake with stable data, or an asynchronous FIFO.
9. **Expecting a synchronizer to catch a short pulse.** A pulse shorter than the destination clock period can fall between two edges. Stretch it, or cross it as a toggle.
10. **Releasing a reset asynchronously.** Assert it asynchronously if you must, but release it through a synchronizer in each clock domain.

---

## 12. Questions you will be asked

**What is the difference between a latch and a flip-flop?** A latch is level-sensitive (follows its input while enabled). A flip-flop is edge-triggered: it samples at the clock edge only. A flip-flop is two latches in a master-slave arrangement.

**What are setup and hold time?** Setup: the input must be stable for a minimum time before the clock edge. Hold: it must stay stable for a minimum time after. Violating either can capture a wrong or undefined value.

**What decides the maximum clock frequency?** The critical path: T ≥ t_cq + longest logic delay + t_setup + skew.

**What is metastability and how do you deal with it?** A flip-flop caught in its setup/hold window may sit at an undefined voltage for a while. It happens when asynchronous signals meet a clock. You cannot prevent it, but a chain of two (or more) flip-flops makes the probability of it reaching the logic very small.

**Why does a two-flip-flop synchronizer work, and when is it not enough?** The first flip-flop may go metastable, but it has a whole clock period to settle, and the chance that it has not settled falls exponentially with that time (MTBF = e^(t_r/τ) / (T_W · f_clk · f_data)); the second flip-flop then shows a clean value. It is not enough for a multi-bit value (the bits settle independently), for a pulse shorter than the destination period, or for a reset release.

**How do you pass a multi-bit value between two clock domains?** Never by synchronizing the bits one by one. Options: a Gray-coded counter (only one bit changes, so the receiver sees the old or the new value); hold the data stable and cross a single request bit with an acknowledge coming back (a handshake, slow but simple); or an asynchronous FIFO, where the data sit in a dual-port memory and only the Gray-coded pointers cross.

**How does an asynchronous FIFO tell full from empty?** Both pointers have one more bit than the address needs and are Gray coded before they cross. Empty (checked in the read domain): the read pointer equals the synchronized write pointer. Full (checked in the write domain): the write pointer equals the synchronized read pointer with its top two bits inverted. A stale pointer only makes each flag late, never wrong.

**Why are clock-domain-crossing bugs missed by simulation and by timing analysis?** A simulator has no metastability (a sampled signal is 0 or 1), and static timing analysis skips paths between asynchronous clocks by constraint. They are found by CDC lint tools, which check that every crossing uses an approved structure, and by review.

**A 64-bit hardware counter is read as two 32-bit halves. What can go wrong, and what is the fix?** The counter can carry from the low half into the high half between the two reads, giving a value 2³² too large or too small. Read high, low, high again and retry if the two high reads differ.

**Moore versus Mealy?** Moore outputs depend only on the state, so they are stable and registered, usually with more states. Mealy outputs depend on state and input, so they react a cycle sooner with fewer states but may glitch.

**How many flip-flops for a machine with 10 states?** ceil(log₂ 10) = 4 with binary encoding, 10 with one-hot.

**What is an LFSR used for?** A shift register with XOR feedback that cycles through 2ⁿ − 1 states: cheap pseudo-random numbers, CRC checksums, scramblers and test patterns.

**Synchronous versus asynchronous counter?** Synchronous: all flip-flops share the clock and change together. Asynchronous (ripple): each is clocked by the previous stage, so delays add up and outputs glitch.

---

## 13. Problems

**How to use this section.** Try each problem before looking at the answers at the bottom. A, B, C and E are paper problems (about 20 minutes each; E ends with a few lines of C), D is a short C program (about 30 minutes).

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

### Problem E (paper): crossing clock domains

(a) Use MTBF = e^(t_r/τ) / (T_W · f_clk · f_data) with τ = 30 ps, T_W = 20 ps, a 1 GHz receiving clock and an input that changes 10 million times a second. Two flip-flops back to back leave the first one a period minus 0.1 ns of wire and setup to settle. What is the MTBF? Would a product with a million units accept it? What does a third flip-flop give?
(b) A 4-bit counter goes from 7 to 8 across a clock boundary and each bit has its own synchronizer. How many values can the receiver see, and how many are wrong? Write the Gray codes of 7 and 8 and count the bits that differ.
(c) An asynchronous FIFO has 8 entries and 4-bit Gray-coded pointers. Using the rules of section 9.4, decide full or empty for (w = 9, r = 1), (w = 9, r = 2) and (w = 5, r = 5). A writer pushes 120 words back to back at 100 MHz and the reader drains at 25 MHz: how deep must the FIFO be?
(d) Write `uint64_t read_counter(volatile uint32_t *hi_reg, volatile uint32_t *lo_reg)` for a free-running 64-bit counter that is read as two 32-bit registers, so that the result is never torn. Say why it works.

*Where:* paper and a calculator for (a) to (c); your editor for (d). *How:* (a) t_r = 1 ns − 0.1 ns, then evaluate e^(t_r/τ) and divide. (b) each of the four bits can show its old or its new value. (c) Gray code is `b ^ (b >> 1)`; invert the top two bits of the Gray read pointer and compare with the Gray write pointer; for the depth use B × (1 − f_r / f_w) and add a few entries for the flag latency. (d) read the high half twice around the low half and compare.

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

### Problem E

(a) t_r = 0.9 ns, so MTBF = e^(0.9 / 0.03) / (20 ps × 1 GHz × 10⁷ per second) = e³⁰ / (2 × 10⁵) = 1.07 × 10¹³ / (2 × 10⁵) = 5.3 × 10⁷ s, about **1.7 years** per unit. A million units would see a failure about **every 53 seconds**: not acceptable. A third flip-flop gives 1.9 ns: e^(63.3) / (2 × 10⁵), about 1.6 × 10²² s, roughly 5 × 10¹⁴ years. (The inputs are assumed values; real ones come from the cell library.)

(b) Each of the four bits can be old or new, so all **16** values can appear, and **14** of them are wrong (only 7 and 8 were true). Gray(7) = `0100` and Gray(8) = `1100`: **one** bit differs, so the receiver sees 7 or 8 and nothing else.

(c) Gray(9) = `1101`, Gray(1) = `0001`, Gray(2) = `0011`, Gray(5) = `0111`. For (9, 1): inverting the top two bits of `0001` gives `1101`, equal to Gray(9), so **full** (8 words). For (9, 2): `0011` becomes `1111`, not equal to `1101`, and the pointers differ, so neither full nor empty (7 words). For (5, 5): the Gray codes are equal, so **empty**. Depth: 120 × (1 − 25/100) = 90 words pile up, a few more for the flag latency, so **128 entries** (the next power of two).

(d)

```c
uint64_t read_counter(volatile uint32_t *hi_reg, volatile uint32_t *lo_reg) {
    uint32_t hi, lo, hi2;
    do {
        hi  = *hi_reg;
        lo  = *lo_reg;
        hi2 = *hi_reg;
    } while (hi != hi2);                 /* the counter carried into the high half while we read: try again */
    return ((uint64_t)hi << 32) | lo;
}
```

If the high half is the same before and after the low half is read, the counter did not carry into it in between, so `lo` belongs with `hi`. If it changed, the low half may be from either side of the carry, so read again; the high half changes only once every 2³² counts, so the loop almost never repeats. I ran a harness where the counter advanced on every register access and the carry fell close to the reads: the plain high-then-low read returned a value the counter never had in 333,334 of 2,000,000 trials; the loop above, in none.

---

## Cheat sheet

- **Sequential** = combinational logic + state registers + a clock. Outputs depend on history.
- **SR latch**: set / reset / hold, S = R = 1 invalid. **D latch**: transparent while enabled. **D flip-flop**: `Q <= D` at the clock edge (two latches, master and slave).
- **T**: `D = Q ^ T`. **JK**: `D = J·!Q + !K·Q`.
- `T_clock >= t_cq + t_logic(longest) + t_setup + skew`. Hold: `t_cq + t_logic(shortest) >= t_hold + skew`. A slower clock fixes setup, not hold.
- **Metastability** happens with asynchronous inputs; use a two-flip-flop synchronizer. MTBF = e^(t_r/τ) / (T_W · f_clk · f_data): each extra flip-flop adds a period to t_r.
- **Crossing clock domains:** one slow bit, a synchronizer; a bus, never bit by bit (Gray code, a handshake with stable data, or an asynchronous FIFO with Gray pointers of n+1 bits: empty = pointers equal, full = top two bits inverted); an event, a toggle synchronizer; a reset, asynchronous assert and synchronous release; a counter in halves, read high, low, high and retry.
- **Register** with load: `D = load ? new : Q`. **Shift register**: SISO, SIPO, PISO, PIPO.
- **Counter**: `Q <= Q + 1`; bit i toggles when all lower bits are 1. Mod-N wraps early. The PC is a counter with a load. A T flip-flop with T = 1 divides the clock by 2.
- **LFSR**: shift register with XOR feedback; *n* bits with good taps give 2ⁿ − 1 states. Used for CRC, scramblers (802.11: x⁷ + x⁴ + 1) and test patterns. Ring and Johnson counters are shift registers with feedback (one-hot, and 2n states).
- **FSM** = state register + next-state logic + output logic. **Moore**: output from state. **Mealy**: output from state and input (fewer states, reacts a cycle sooner). n states need ceil(log₂ n) flip-flops, or n with one-hot.

**Next:** tutorial 05, Computer Arithmetic and the ALU (fast adders, multipliers and dividers, floating-point hardware, the flags). Part 2 then starts with tutorial 06, ISA Design. Say "next" when you are ready.
