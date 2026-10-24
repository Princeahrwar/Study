# CO 03: Boolean Logic and Combinational Circuits

*Part of the [Computer Organization plan](00_plan.md) · Skim tutorial · Time: about 2 hours (1 reading, 1 problems) · You need: [tutorial 02](02_data_representation.md) (two's complement, carry vs overflow, Gray code)*

By the end of this tutorial you can:

- explain what a logic gate is and why hardware is built mostly from NAND and NOR;
- simplify a Boolean expression with the algebra laws and De Morgan's rules;
- go from a truth table to a circuit (sum of products), and shrink it with a Karnaugh map;
- explain what a multiplexer, decoder, priority encoder and comparator do, and where a CPU uses each;
- build a full adder, chain four of them into a ripple-carry adder, and read the carry and overflow flags off it;
- sketch an ALU, and say why a 64-bit adder is not a simple chain.

This is a **Skim** tutorial: it is the bottom layer, and interviews touch it lightly. Read it once, do the problems, and do not memorise the K-map rules. Tutorials 06 onward rely on the ideas, not the details.

**Notation.** Plain text cannot put a bar over a letter, so this tutorial writes NOT as `!A` (same as C). AND is `A·B` or just `AB`, OR is `A+B`, XOR is `A^B`.

---

## 1. From transistors to gates

A **transistor** is a switch that a voltage controls. In CMOS, the technology of every modern chip, there are two kinds: an **NMOS** transistor conducts when its input is 1, a **PMOS** transistor conducts when its input is 0.

Wire one of each between the power supply and ground and you get an **inverter** (NOT): input 1 turns the NMOS on and pulls the output down to 0; input 0 turns the PMOS on and pulls it up to 1. Two transistors, one gate.

A **NAND** gate takes four transistors:

```text
           VDD (logic 1)
        +------+------+
      [P] A         [P] B       two PMOS in parallel: either one pulls the output up
        +------+------+
               |
               +------ Y        output
               |
             [N] A              two NMOS in series: both must conduct to pull it down
               |
             [N] B
               |
              GND (logic 0)
```

Y is pulled to 0 only when **both** A and B are 1 (both NMOS conduct). In every other case at least one PMOS conducts and Y is 1. That is NAND. Swap series and parallel and you get NOR.

Two facts follow that matter later:

- **AND and OR cost more than NAND and NOR.** An AND is a NAND followed by an inverter (6 transistors). So hardware designers think in NAND and NOR.
- **A gate is not instant.** The output changes a small delay after the inputs change, on the order of tens of picoseconds in a modern process. A signal that passes through a chain of gates takes the sum of those delays. The clock period must be longer than the **slowest chain** between two storage elements (the **critical path**), so a few tens of gate delays per cycle is all a fast CPU has. This is why section 8 cares about how an adder is wired, and why tutorial 28 ties clock speed and power to switching.

You will never draw transistors again in this track. Real designers write Verilog or VHDL, and tools turn that into gates. But everything a CPU does is these gates.

---

## 2. The basic gates

Each gate computes a **Boolean function**: inputs and outputs are 0 or 1. A **truth table** lists the output for every input combination. For *n* inputs there are 2ⁿ rows.

| A | B | AND `A·B` | OR `A+B` | XOR `A^B` | NAND | NOR | XNOR |
|---|---|---|---|---|---|---|---|
| 0 | 0 | 0 | 0 | 0 | 1 | 1 | 1 |
| 0 | 1 | 0 | 1 | 1 | 1 | 0 | 0 |
| 1 | 0 | 0 | 1 | 1 | 1 | 0 | 0 |
| 1 | 1 | 1 | 1 | 0 | 0 | 0 | 1 |

NOT is the one-input gate: `!0 = 1`, `!1 = 0`.

In words: AND is 1 when **both** are 1; OR is 1 when **at least one** is 1; XOR is 1 when the inputs **differ** (it is "odd parity", and also "add without carry"); NAND, NOR and XNOR are the inverted versions.

**In C** these are the bitwise operators you met in tutorial 02: `&`, `|`, `^`, `~`, working on every bit position at once. One 32-bit `a & b` is 32 AND gates side by side.

Watch the difference between `&` and `&&`: `&` works bit by bit, `&&` treats each whole value as true (non-zero) or false. `2 & 1` is 0, but `2 && 1` is 1.

There are 16 possible functions of two inputs, 256 of three, 65,536 of four (2 to the power 2ⁿ). Every one of them can be built from a few gate types, which is the next point.

---

## 3. NAND (and NOR) can build everything

NAND is **universal**: any Boolean function can be built using only NAND gates.

```text
NOT a    = NAND(a, a)                        both inputs tied together
a AND b  = NOT(NAND(a, b))                   a NAND followed by a NOT
a OR  b  = NAND(NOT a, NOT b)                De Morgan, next section
```

XOR from four NANDs, a classic:

```text
n1  = NAND(a, b)
n2  = NAND(a, n1)
n3  = NAND(b, n1)
a^b = NAND(n2, n3)
```

Check a=1, b=0: n1 = NAND(1,0) = 1; n2 = NAND(1,1) = 0; n3 = NAND(0,1) = 1; output = NAND(0,1) = 1. Correct, since 1^0 = 1.

NOR is universal too. This is why a chip library may contain only NAND and NOR cells plus a few special ones, and why a "gate count" is quoted in NAND-equivalents.

---

## 4. Boolean algebra

Boolean algebra is the set of rules for rewriting expressions without changing what they compute. Smaller expressions mean fewer gates, less delay and less power.

| Law | AND form | OR form |
|---|---|---|
| Identity | `A·1 = A` | `A+0 = A` |
| Null | `A·0 = 0` | `A+1 = 1` |
| Idempotent | `A·A = A` | `A+A = A` |
| Complement | `A·!A = 0` | `A+!A = 1` |
| Commutative | `AB = BA` | `A+B = B+A` |
| Distributive | `A(B+C) = AB + AC` | `A+BC = (A+B)(A+C)` |
| Absorption | `A(A+B) = A` | `A+AB = A` |
| Double negation | `!!A = A` | |

The OR-distributes-over-AND law (`A+BC = (A+B)(A+C)`) has no counterpart in ordinary arithmetic and trips people up.

### De Morgan's laws

```text
!(A·B) = !A + !B          NOT of an AND  = OR of the NOTs
!(A+B) = !A · !B          NOT of an OR   = AND of the NOTs
```

Rule of thumb: **push the NOT inside, and swap AND with OR**. A very common mistake is to flip only one of those two things. In C: `!(a && b) == (!a || !b)`.

De Morgan explains the NAND construction of OR above: `A+B = !(!A · !B)`, which is `NAND(!A, !B)`.

### Simplifying: two examples

```text
Example 1:   X = !A·B + A·B + A·!B
                = B(!A + A) + A·!B           group the first two terms
                = B·1 + A·!B                 complement law
                = B + A·!B
                = B + A                      uses the rule  P + !P·Q = P + Q
```

The rule `P + !P·Q = P + Q` appears constantly. Why it holds: `P + !P·Q = (P + !P)(P + Q) = 1·(P+Q) = P+Q`.

```text
Example 2:   Y = A·B + A·!B·C
                = A(B + !B·C)                factor out A
                = A(B + C)                   same rule again
```

Check example 2 with A=1, B=0, C=1: original = 0 + 1·1·1 = 1; simplified = 1·(0+1) = 1. Same.

---

## 5. From a truth table to a circuit

Given a truth table, there is a mechanical way to build a circuit that works.

A **minterm** is an AND of all the variables, each plain or inverted, that is 1 for exactly **one** input row. Row number *i* gives minterm *m*ᵢ. A **sum of products (SOP)** is the OR of the minterms for rows where the function is 1.

Example, the **majority** function (output 1 when at least two of three inputs are 1):

| A | B | C | row | majority |
|---|---|---|---|---|
| 0 | 0 | 0 | 0 | 0 |
| 0 | 0 | 1 | 1 | 0 |
| 0 | 1 | 0 | 2 | 0 |
| 0 | 1 | 1 | 3 | **1** |
| 1 | 0 | 0 | 4 | 0 |
| 1 | 0 | 1 | 5 | **1** |
| 1 | 1 | 0 | 6 | **1** |
| 1 | 1 | 1 | 7 | **1** |

The function is 1 for rows 3, 5, 6, 7, written Σm(3, 5, 6, 7):

```text
majority = !A·B·C + A·!B·C + A·B·!C + A·B·C
```

That works, but it has four 3-input ANDs. Simplify with the laws (the last term `ABC` can be reused three times, since `X + X = X`):

```text
majority = BC(!A + A) + AC(!B + B) + AB(!C + C)  =  BC + AC + AB
```

Three 2-input ANDs and a 3-input OR. Much smaller, and it is the obvious answer: "at least two are 1".

The dual form is the **product of sums (POS)**: for each row where the function is **0**, write a **maxterm** (an OR that is 0 only on that row), then AND them all. Same function, different shape; SOP is used more often.

Doing the algebra by hand gets tedious. The next section is a picture that does the same job.

---

## 6. Karnaugh maps

A **Karnaugh map (K-map)** is a truth table laid out in a grid so that you can *see* which terms combine. The key: two cells that sit next to each other differ in **exactly one variable**, and

```text
A·B + A·!B = A(B + !B) = A
```

so two adjacent 1s merge into a term with **one variable fewer**. For that to work, the row and column labels must run in **Gray code** (00, 01, **11, 10**, not 00, 01, 10, 11), exactly the one-bit-change order from tutorial 02. The map also wraps: the left edge is adjacent to the right edge, and the top to the bottom.

**Rules for grouping the 1s:**

1. Group sizes are powers of two: 1, 2, 4, 8, 16.
2. Groups are rectangles (wrap-around counts).
3. Make groups as **large** as possible, and use as **few** groups as possible. Groups may overlap.
4. Every 1 must be in at least one group. Each group becomes one product term containing only the variables that **do not change** inside it.

### Example 1: four corners

f(A,B,C,D) = Σm(0, 2, 5, 7, 8, 10, 13, 15). Rows are AB, columns are CD:

```text
              CD
          00   01   11   10
   AB 00 | 1  | 0  | 0  | 1 |
      01 | 0  | 1  | 1  | 0 |
      11 | 0  | 1  | 1  | 0 |
      10 | 1  | 0  | 0  | 1 |
```

- The **four corners** form one group (the map wraps both ways). Inside it B=0 and D=0 never change, A and C do: the term is `!B·!D`.
- The **middle square** (B=1, D=1) is the other group: `B·D`.

```text
f = B·D + !B·!D        which is XNOR(B, D)
```

Eight minterms of four variables became one two-term expression, and a pattern popped out that would be hard to see in algebra.

### Example 2: don't-cares

Often some input combinations never happen. For a **BCD digit** (a decimal digit 0 to 9 stored in 4 bits), the patterns 10 to 15 cannot occur. For those rows we do not care whether the output is 0 or 1; mark them **X** and use them as 1s when that makes a group bigger.

f = 1 when the digit is 5 or more:

```text
              CD
          00   01   11   10
   AB 00 | 0  | 0  | 0  | 0 |
      01 | 0  | 1  | 1  | 1 |
      11 | X  | X  | X  | X |
      10 | 1  | 1  | X  | X |
```

- The bottom two rows (A=1, all eight cells) form one group: `A`.
- Columns 01 and 11 in rows 01 and 11 (B=1, D=1): `B·D`, covers digits 5 and 7.
- Columns 11 and 10 in rows 01 and 11 (B=1, C=1): `B·C`, covers 6 and 7.

```text
f = A + B·C + B·D
```

Check: digit 9 = 1001 gives A=1 so f=1. Digit 4 = 0100 gives A=0, BC=0, BD=0 so f=0. Digit 6 = 0110 gives BC=1 so f=1.

### Beyond the map

K-maps are comfortable up to four variables; five need two stacked maps; six are painful. For larger functions the **Quine-McCluskey method** does the same job as a table: list the minterms, group them by number of 1s, repeatedly merge pairs that differ in one bit to get the **prime implicants** (largest possible groups), then choose a smallest set of them that covers all the minterms. Real tools use heuristic versions (such as Espresso) that scale to thousands of inputs. In practice a synthesis tool does this for you; the point of learning K-maps is to **see why** simplification works.

---

## 7. Combinational building blocks

A **combinational circuit** has no memory: its outputs depend **only on its current inputs**. (A circuit whose outputs also depend on past inputs is **sequential**: tutorial 04.) Gates are wired into standard blocks that appear everywhere in a CPU.

### Multiplexer (mux)

A mux **selects** one of several inputs and passes it to the output. A 2:1 mux has data inputs A and B, a select S, and output `Y = !S·A + S·B` (S=0 picks A, S=1 picks B). A 4:1 mux has four data inputs and 2 select bits. With *n* select bits a mux picks one of 2ⁿ inputs.

In C, `y = s ? b : a;` is a 2:1 mux, and `arr[i]` is a big mux (the index is the select).

A mux can also build **any function**. Majority from section 5, with A and B as the selects and C as data:

```text
 A B | output
 0 0 | 0
 0 1 | C
 1 0 | C
 1 1 | 1       -> a 4:1 mux with data inputs 0, C, C, 1
```

That idea is how an FPGA works: a **lookup table (LUT)** is a mux whose data inputs are stored bits, so a 6-input LUT is a 64:1 mux that can compute any function of 6 variables (tutorial 24).

### Decoder and demux

A **decoder** turns an *n*-bit number into a **one-hot** signal on 2ⁿ output wires: exactly one wire is 1. A 2-to-4 decoder:

| A1 A0 | Y0 | Y1 | Y2 | Y3 |
|---|---|---|---|---|
| 00 | 1 | 0 | 0 | 0 |
| 01 | 0 | 1 | 0 | 0 |
| 10 | 0 | 0 | 1 | 0 |
| 11 | 0 | 0 | 0 | 1 |

Each output is a single AND (minterm) of the inputs: `Y2 = A1·!A0`. A **demux** is the reverse of a mux: it sends one input to one of 2ⁿ outputs, chosen by the select bits.

### Encoder and priority encoder

An **encoder** does the opposite of a decoder: one-hot in, binary number out. That assumes only one input is 1. A **priority encoder** handles several inputs being 1 by reporting the **highest** one, with a **valid** output V that says whether any input is set at all. For four inputs I3..I0:

```text
Y1 = I3 + I2
Y0 = I3 + !I2·I1
V  = I3 + I2 + I1 + I0
```

Check I = 0110 (I2 and I1 set): Y1 = 1, Y0 = 0 + 0·1 = 0, so Y = 10 = 2, the highest set input.

### Comparator

Equality of two *n*-bit numbers: XNOR each pair of bits (1 when they match), then AND all the results. A "less than" comparator is built from a subtractor (section 8) and the flags it produces.

### Parity

The XOR of all bits is 1 when an odd number of bits are 1. A tree of XOR gates computes it in log₂(n) levels.

### Tri-state buffer

A **tri-state buffer** has a third output state besides 0 and 1: **high impedance** ("Z", effectively disconnected), chosen by an enable input. Several drivers can then share one wire, as long as only one is enabled at a time; a bus works this way (tutorial 25). Two drivers enabled with opposite values is **bus contention**, a near short circuit that wastes power and can damage the chip.

### Where the CPU uses these

| Block | Where you meet it |
|---|---|
| Mux | Choosing the ALU result; picking which register to read; forwarding paths in a pipeline (tutorial 09) |
| Decoder | Instruction decode (opcode to control signals); address decode that picks one device from the address bits (tutorial 25); register write-enable |
| Priority encoder | An interrupt controller picking the highest-priority pending interrupt (tutorial 26); arbiters; the `clz` instruction behind `__builtin_clz` |
| Comparator | Branch conditions; comparing cache tags to the address (tutorial 13) |
| Adder | ALU, PC + 4, address calculation, counters |
| XOR / parity tree | Parity bits and ECC memory (tutorial 17), CRC |

---

## 8. Adders

Adding is the most important arithmetic circuit. Start with one column.

**Half adder.** Adds two bits: `Sum = A^B`, `Carry = A·B`. It has no input for a carry coming in from the right, so it works only for the lowest bit.

**Full adder.** Adds two bits **plus** a carry-in:

```text
Sum  = A ^ B ^ Cin
Cout = A·B + Cin·(A ^ B)
```

Plain English: Sum is 1 when an odd number of the three inputs are 1; Cout is 1 when at least two are (it is the majority function from section 5, and `A·B + Cin·(A^B)` is just a cheaper way to write it). In C, one full adder is two lines:

```c
int full_adder(int a, int b, int cin, int *cout) {
    *cout = (a & b) | (cin & (a ^ b));
    return a ^ b ^ cin;
}
```

A full adder is roughly two half adders and an OR gate; a classic CMOS design uses about 28 transistors.

### Ripple-carry adder

Chain *n* full adders: each Cout feeds the next stage's Cin. A 4-bit addition, 0110 + 0111 (6 + 7):

| Bit | A | B | Cin | Sum | Cout |
|---|---|---|---|---|---|
| 0 | 0 | 1 | 0 | 1 | 0 |
| 1 | 1 | 1 | 0 | 0 | 1 |
| 2 | 1 | 1 | 1 | 1 | 1 |
| 3 | 0 | 0 | 1 | 1 | 0 |

Result: `1101`. You can read both flags from tutorial 02 straight off the wires:

- **Carry flag (CF)** = the carry out of the top bit = **0**. As unsigned, 6 + 7 = 13 fits in 4 bits.
- **Overflow flag (OF)** = the carry **into** the top bit XOR the carry **out of** the top bit = 1 xor 0 = **1**. As signed 4-bit numbers (range -8 to 7), 13 does not fit; `1101` reads as -3.

That one XOR is how every CPU computes signed overflow.

### Subtraction for free

From tutorial 02, `A - B = A + (~B) + 1`. So a subtractor is the same adder with an XOR gate on each B input and a control signal **Sub** that feeds both the XORs (invert B when Sub=1) and the Cin (the "+1"):

```text
5 - 3 in 4 bits:   0101 + ~0011 + 1  =  0101 + 1100 + 1  =  1 0010
                                         result 0010 = 2, carry out = 1
```

One adder does both operations. Note the carry out: after a subtraction, carry out = 1 means **no borrow was needed** (A ≥ B as unsigned). x86 stores the opposite, a "borrow" flag, in CF after a `sub`/`cmp`, while ARM stores the carry-out as is. Same hardware idea, different flag convention, and a classic gotcha when reading assembly on both.

### Why a long ripple adder is too slow

In a ripple adder the carry has to pass through every stage in turn. With roughly two gate delays per stage, a 64-bit ripple adder is about 128 gate delays, several times more than one clock cycle allows. Fast adders compute carries **ahead of time**.

**Carry-lookahead:** define for each bit position a **generate** `G = A·B` (this position makes a carry on its own) and a **propagate** `P = A^B` (this position passes an incoming carry on). Then

```text
c(i+1) = G(i) + P(i)·c(i)
c2     = G1 + P1·G0 + P1·P0·c0          (c1 substituted in; no waiting for c1)
```

Each carry becomes a short AND-OR of the inputs. Real 64-bit adders arrange this as a **prefix tree** with depth proportional to log₂(64) = 6 levels instead of 64. Tutorial 05 (optional) goes deeper; for now, remember: **the wiring of an adder decides its speed**.

---

## 9. A first ALU

The **ALU** (arithmetic logic unit) is the part of the CPU that computes. Here is the surprising part: it does not "choose" which circuit to use. It computes **every** result at the same time and a **mux** picks the one wanted.

```text
 A, B ---> [ AND ] -------+
 A, B ---> [ OR  ] -------+
 A, B ---> [ ADD ] -------+--> [ 4:1 mux ] --> Result --> flags Z N C V
 A, B ---> [ SUB ] -------+         ^
                                    |
                              op (2 bits, from the control unit)
```

With A = 0110 and B = 0011:

| op | Result |
|---|---|
| ADD | 1001 |
| SUB | 0011 |
| AND | 0010 |
| OR | 0111 |

Next to the result the ALU produces **flags**: **Z** (zero: all result bits are 0, a wide NOR), **N** (negative: the top bit), **C** (carry out) and **V** (overflow). A compare instruction is a subtraction whose result is thrown away and whose flags are kept; a conditional branch then looks at the flags. For `5 - 5` the result is 0000, so Z = 1, and a "jump if equal" is taken. Tutorials 06 to 08 use this.

Two more things an ALU needs:

- **Shifts.** A shift by a variable amount (0 to 31) uses a **barrel shifter**: five layers of 2:1 muxes, where layer *k* either shifts by 2ᵏ (1, 2, 4, 8, 16) or passes the value straight through, and the five bits of the shift amount are the selects. Any shift takes five mux delays, whatever the amount. A rotate is the same network with the shifted-out bits wrapped around.
- **Set-less-than** (`slt`). It reuses the subtractor: compute A − B, throw the difference away, and put the answer "A < B" in bit 0 of the result. For signed numbers A < B exactly when N xor V is 1; for unsigned numbers, when the carry out is 0 (a borrow was needed).

---

## 10. Mistakes that cost points

1. **Mixing up `&&` and `&`** (or `||` and `|`). Bitwise works on every bit, logical on whole values.
2. **Half a De Morgan.** `!(A+B)` is `!A·!B`, not `!A + !B`: you must swap the operator as well as move the NOT.
3. **K-map labels in binary order.** They must be 00, 01, 11, 10 (Gray), or adjacent cells no longer differ by one variable.
4. **K-map groups that are not powers of two, or are not as large as possible,** and forgetting the wrap-around and the don't-cares.
5. **Confusing minterms and maxterms.** Minterm: AND, one row where f = 1. Maxterm: OR, one row where f = 0.
6. **Mixing up carry and overflow.** Carry out of the top bit is for unsigned; carry-in XOR carry-out of the top bit is for signed.
7. **Treating a combinational circuit as if it remembered something.** It does not; the output is a pure function of the current inputs.
8. **Assuming a wide adder is a simple chain.** It would be too slow; real adders use lookahead or prefix trees.

---

## 11. Questions you will be asked

**Why is NAND called a universal gate?** Any Boolean function can be built with NAND gates alone (NOT = NAND with tied inputs, AND = NAND then NOT, OR = NAND of inverted inputs). It is also cheap in CMOS: 4 transistors, versus 6 for AND.

**State De Morgan's laws.** `!(A·B) = !A + !B` and `!(A+B) = !A·!B`: push the NOT inward and swap AND with OR.

**What is the difference between combinational and sequential logic?** Combinational outputs depend only on the current inputs (adders, muxes, decoders). Sequential circuits also have memory, so outputs depend on history (registers, counters, state machines: tutorial 04).

**How does a full adder work, and how do you build a 32-bit adder from it?** Sum = A^B^Cin, Cout = majority(A, B, Cin). Chain 32 of them, each carry-out feeding the next carry-in. That is correct but slow, so real adders use carry-lookahead or a prefix tree.

**How does hardware compute A - B?** As `A + ~B + 1`: invert B with XOR gates, set carry-in to 1, reuse the adder.

**How is signed overflow detected in hardware?** Overflow = carry into the sign bit XOR carry out of the sign bit. Equivalently, two operands of the same sign gave a result of the opposite sign.

**What limits the clock frequency?** The longest combinational path (the critical path) between two registers: the clock period must exceed the sum of the gate and wire delays along it, plus a small setup margin. Speeding up a CPU means shortening that path or splitting it with pipeline registers (tutorial 09).

**What is a mux, and where is one used?** A circuit that picks one of several inputs based on select bits; in CPUs it chooses ALU results and register contents, and in FPGAs a LUT is a mux over stored bits.

---

## 12. Problems

**How to use this section.** Try each problem before looking at the answers at the bottom. A, B and C are paper problems (about 20 minutes each), D is a short C program (about 30 minutes).

### Problem A (paper): the algebra

(a) Simplify `X = !A·B + A·B + A·!B` and `Y = A·B + A·!B·C` using the laws.
(b) Use De Morgan to write `!(A·!B + !C)` as a sum of products.
(c) Show that four NAND gates can compute XOR: write the four gates and test them for all four input combinations.

*Where:* paper. *How:* for (a) look for the rule `P + !P·Q = P + Q` and for factoring out a common variable. For (b) apply De Morgan to the outer NOT first, then to the inner one. For (c) use the construction in section 3, tracing each row of the XOR truth table through it.

### Problem B (paper): K-maps

(a) Simplify f(A,B,C,D) = Σm(0, 1, 2, 5, 8, 9, 10).
(b) A BCD digit (0 to 9; 10 to 15 never occur) goes in. Design a circuit whose output is 1 when the digit is prime (2, 3, 5 or 7). Use don't-cares.

*Where:* paper, on a 4x4 grid with Gray-code labels. *How:* mark the 1s (and X for don't-cares), draw the largest rectangles you can, including ones that wrap around the edges, and read off each group's unchanging variables. For (a) look at the corners first.

### Problem C (paper): building blocks

(a) The top two address bits A15 and A14 select one of four devices. Write the select line for each device (a 2-to-4 decoder). Which device does the address 0x9000 select?
(b) Derive the equations of a 4-input priority encoder (outputs Y1, Y0 and valid V) and check them for the input I3..I0 = 0110.
(c) Build the 3-input majority function using a single 4:1 mux and no other gates. What goes on each data input if A and B are the selects?

*Where:* paper. *How:* (a) each output is an AND of two address bits or their inverses; write 0x9000 in binary and read the top two bits. (b) Write the truth table with "highest set input wins", then read each output off it. (c) Fix A and B, then ask what the output has to be in terms of C for each of the four cases.

### Problem D (hands-on C): a ripple-carry adder in software

Write a function `add4(x, y, cin, &cf, &of)` that adds two 4-bit numbers using only the `full_adder` function from section 8 in a loop, going from bit 0 up. It returns the 4-bit sum and sets the carry flag and the overflow flag (carry into the top bit XOR carry out of it). Then loop over all 256 pairs and compare against ordinary arithmetic: the sum modulo 16, the carry (`x + y > 15`) and signed overflow (`x` and `y` read as -8..7, sum outside -8..7).

*Where:* your editor and terminal. *How:* keep a running carry variable; remember the carry **into** bit 3 before processing it. Compile with `gcc -Wall -Wextra`. Print the number of mismatches; it should be 0.

---

## Answers

Try before reading.

### Problem A

(a) `X = !A·B + A·B + A·!B`: the first two terms combine to `B`, leaving `B + A·!B = A + B`. So **X = A + B**.
`Y = A·B + A·!B·C = A(B + !B·C) = A(B + C)`. So **Y = A·(B + C)**.

(b) `!(A·!B + !C) = !(A·!B) · !(!C) = (!A + B) · C = !A·C + B·C`. So **!A·C + B·C**.

(c) The gates:

```text
n1  = NAND(a, b)
n2  = NAND(a, n1)
n3  = NAND(b, n1)
out = NAND(n2, n3)
```

| a | b | n1 | n2 | n3 | out | a^b |
|---|---|---|---|---|---|---|
| 0 | 0 | 1 | 1 | 1 | 0 | 0 |
| 0 | 1 | 1 | 1 | 0 | 1 | 1 |
| 1 | 0 | 1 | 0 | 1 | 1 | 1 |
| 1 | 1 | 0 | 1 | 1 | 0 | 0 |

### Problem B

(a) The map for Σm(0, 1, 2, 5, 8, 9, 10):

```text
              CD
          00   01   11   10
   AB 00 | 1  | 1  | 0  | 1 |
      01 | 0  | 1  | 0  | 0 |
      11 | 0  | 0  | 0  | 0 |
      10 | 1  | 1  | 0  | 1 |
```

- Left two columns, top and bottom rows (wrap): cells 0, 1, 8, 9, where B=0 and C=0: `!B·!C`.
- The four corners: cells 0, 2, 8, 10, where B=0 and D=0: `!B·!D`.
- Cell 5 needs a group: it pairs with cell 1 (A=0, C=0, D=1): `!A·!C·D`.

**f = !B·!C + !B·!D + !A·!C·D**

(b) The map, with don't-cares for 10 to 15:

```text
              CD
          00   01   11   10
   AB 00 | 0  | 0  | 1  | 1 |
      01 | 0  | 1  | 1  | 0 |
      11 | X  | X  | X  | X |
      10 | 0  | 0  | X  | X |
```

- Columns 11 and 10 in the top and bottom rows (cells 3, 2, 11, 10; two of them are don't-cares): B=0, C=1: `!B·C`. It covers the primes 2 and 3.
- Cells 5, 7, 13, 15 (B=1, D=1): `B·D`. It covers the primes 5 and 7.

**f = !B·C + B·D.** Check the non-primes: 0 gives 0, 1 gives 0 (C=0, B=0), 4 gives 0, 6 (0110) gives `!B·C = 0` and `B·D = 0`, 8 and 9 give 0.

### Problem C

(a) With the two top bits as inputs: `CS0 = !A15·!A14`, `CS1 = !A15·A14`, `CS2 = A15·!A14`, `CS3 = A15·A14`. 0x9000 = 1001 0000 0000 0000 in binary, so A15 A14 = 10 and **device 2** (CS2) is selected. This is address decoding: the top bits choose the device and the remaining 14 bits address inside it.

(b) The truth table gives `Y1 = I3 + I2`, `Y0 = I3 + !I2·I1` and `V = I3 + I2 + I1 + I0`. For I3..I0 = 0110: Y1 = 0+1 = 1, Y0 = 0 + (0·1) = 0, V = 1, so Y = 10 = 2, the highest set input (I2).

(c) With A and B as selects, the data inputs are D0 = 0, D1 = C, D2 = C, D3 = 1: when A=B=0 at most one other input is 1 (output 0); when A=B=1 two are already 1 (output 1); when exactly one of A, B is 1 the answer depends on C (output C).

### Problem D

```c
#include <stdio.h>

int full_adder(int a, int b, int cin, int *cout) {
    *cout = (a & b) | (cin & (a ^ b));
    return a ^ b ^ cin;
}

/* 4-bit ripple-carry adder: returns the sum, sets carry flag and overflow flag */
int add4(int x, int y, int cin, int *cf, int *of) {
    int sum = 0, c = cin, c_into_msb = 0;
    for (int i = 0; i < 4; i++) {
        if (i == 3) c_into_msb = c;                       /* carry going into the top bit */
        int s = full_adder((x >> i) & 1, (y >> i) & 1, c, &c);
        sum |= s << i;
    }
    *cf = c;                                              /* carry out of the top bit */
    *of = c ^ c_into_msb;
    return sum;
}

int main(void) {
    int bad = 0;
    for (int x = 0; x < 16; x++)
        for (int y = 0; y < 16; y++) {
            int cf, of;
            int s = add4(x, y, 0, &cf, &of);
            int sx = x >= 8 ? x - 16 : x;                 /* read as signed 4-bit */
            int sy = y >= 8 ? y - 16 : y;
            int sv = sx + sy;
            if (s != ((x + y) & 15) || cf != (x + y > 15) || of != (sv < -8 || sv > 7))
                bad++;
        }
    printf("mismatches: %d\n", bad);

    int cf, of;
    int s = add4(6, 7, 0, &cf, &of);
    printf("0110 + 0111 = %d (binary 1101), cf=%d of=%d\n", s, cf, of);
    return 0;
}
```

Expected output:

```text
mismatches: 0
0110 + 0111 = 13 (binary 1101), cf=0 of=1
```

This is the hardware in software: the two flags come from exactly the wires described in section 8.

---

## Cheat sheet

- Transistors are voltage-controlled switches. A **NAND** is 4 transistors; AND and OR need an extra inverter stage. NAND (and NOR) are **universal**.
- **XOR** = differ = odd parity = add without carry. `!(A·B) = !A + !B`, `!(A+B) = !A·!B`: push the NOT in and swap AND/OR.
- Useful rules: `A + AB = A`, `A + !A·B = A + B`, `AB + A!B = A`.
- **SOP**: OR of the minterms where f = 1. **K-map**: Gray-code labels (00, 01, 11, 10), wrap-around, power-of-two rectangles, largest groups, don't-cares as 1s when they help.
- **Combinational** = outputs depend only on current inputs. **Mux** selects, **decoder** makes one-hot, **priority encoder** reports the highest set input, **comparator** = XNOR + AND.
- **Full adder**: Sum = A^B^Cin, Cout = AB + Cin(A^B). **CF** = carry out of the top bit; **OF** = carry in to the top bit XOR carry out of it.
- **A - B = A + ~B + 1**. After a subtraction, x86 CF means "borrow", ARM C means "no borrow".
- Ripple-carry delay grows with n. Real adders use **carry-lookahead** (G = AB, P = A^B, c(i+1) = G + P·c(i)) and prefix trees.
- An **ALU** computes all operations and a mux picks one; flags Z, N, C, V come out with the result. A **barrel shifter** is log₂(n) mux layers; `slt` is a subtract that reports N xor V (signed) or no-carry (unsigned). A **tri-state buffer** lets several drivers share a wire (one enabled at a time).
- The **critical path** (longest chain of gates between registers) sets the clock period.

**Next:** tutorial 04, Sequential Logic and State Machines (latches, flip-flops, registers, counters, finite state machines; a Skim tutorial). Say "next" when you are ready, or tell me to jump to a Core one such as tutorial 06.
