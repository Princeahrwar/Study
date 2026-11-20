# NET 09: Network security: firewalls, IPS and deep packet inspection

*Part of the [networking plan](00_plan.md) · Time: about 8 hours · You need: [NET 03](03_tcp_udp_and_transport.md) (the TCP handshake, flags, retransmission), [NET 06](06_linux_network_stack.md) (netfilter hooks, what a packet costs on the host), [NET 08](08_application_protocols_dns_http_tls.md) (the TLS handshake and certificates). A firewall is a program that decides, packet by packet, what may pass. This tutorial builds that decision from the bottom: a list of rules and what it costs, connection tracking and the table that holds it, address translation and the protocols that break it, how a box tells what is inside a flow, inline prevention against detection, the string and regular expression engines behind signatures, decrypting TLS, IPsec arithmetic, floods, and protecting the firewall's own CPU. Every program and script is small, and every output below is pasted from a run.*

"How does a stateful firewall work, and what happens when its table fills?" is asked at Cisco and Palo Alto Networks in some form, and at Nvidia and Google as a data plane design question: what state per flow, where it lives, what an attacker can do to it. The answer that gets a Staff level yes is not a list of product features. It is a **cost model**: what one rule costs per packet, what one connection costs in memory, what an attacker has to send to exhaust each, and what the box does when it runs out. This tutorial gives those numbers where the VM can measure them, and says plainly where it cannot.

**Eight honest limits, stated first.**

1. Everything is measured on **one Linux 6.18 VM** (Firecracker, 2 vCPUs) as **root**, in three network namespaces joined by veth pairs: a client `c` (10.1.0.2), a firewall `fw` (10.1.0.1 and 10.2.0.1) and a server `s` (10.2.0.2). The load generator and the firewall **share the same two CPUs**, so every rate is a lower bound on what the firewall alone would do, and **no rate here is a product figure**.
2. The firewall is **Linux netfilter** with `nft`, plus **Suricata 7.0.3** for inspection. Commercial firewalls (Palo Alto, Cisco, Fortinet) use their own engines, hardware flow tables and ASICs. What transfers is the **shape** of each cost (linear rule lists against hash sets, one table entry per connection, reassembly before matching), not a number.
3. The kernel has **no ESP and no WireGuard** (`CONFIG_INET_ESP` and `CONFIG_WIREGUARD` are both off), so **IPsec and VPN are arithmetic and a Python packet builder**, not a tunnel that carried traffic. IKE, the kernel's xfrm policy and the flowtable fast path are **from memory** and labelled so.
4. There is **no `netem`**, so no delay or loss was added. Everything about round trips is counting, as in NET 08.
5. **Hardware offload, control plane protection in ASICs (CoPP), Hyperscan internals, Encrypted Client Hello, QUIC blocking, vendor application identification and DDoS scrubbing with anycast are from memory.** Suricata here used its software engine with the default Hyperscan-backed multi-pattern matcher, and I did not instrument it.
6. **The TLS proxy is 80 lines of Python**, with the NAT redirect of the first packet done by `nft`. Its **ratios** (it costs about two times a direct connection) are the result; its milliseconds are not a firewall's.
7. **The lab rules are three Suricata rules I wrote**, not a rule set. Nothing here says how good an IPS rule feed is, how many false positives it has, or how to tune one.
8. **Every attack is one I generated myself** (a SYN flood from a C program, a crafted ACK from scapy, a regular expression chosen to be slow). Nothing was run against a network I do not own.

By the end you can:

- **price a rule list and say why sets win** (measured: forwarding fell from **814 k packets/s** with no ruleset to **418 k** at 100 rules, **90 k** at 1,000 and **19 k** at 5,000, about **10 ns per rule**, while one rule matching a set of 5,000 addresses ran at **752 k**);
- **show where a stateless filter fails** (measured: both stateless rules, "inbound with ACK set and SYN clear" and "inbound with ACK set", let a lone ACK through and the client answered it with a `RST`; the stateful rule set answered with **nothing**, and counted the out-of-window ACK as `ct state invalid`);
- **read a conntrack entry through its life** (measured: `SYN_SENT`, `SYN_RECV`, `ESTABLISHED` with `[ASSURED]` and a **432,000 s** timeout, `FIN_WAIT`, `LAST_ACK`, `TIME_WAIT`) **and say which timeouts to lower and why**;
- **size a connection table and say what happens when it fills** (measured with a limit of 1,000: a flood of one-way flows let **3 of 20** legitimate flows in through early drop, a table of **1,000 assured** flows let in **0 of 20**; one entry is **256 bytes**, so 6 million entries are **1.54 GB**);
- **explain why NAT breaks FTP and what a helper does** (measured: active FTP **timed out** through NAT and **worked** once the helper rewrote `PORT 10,1,0,2,150,175` into `PORT 10,2,0,1,150,175` and created an **expectation** for port 38575);
- **identify an application from its first payload** and say how often the port is wrong (measured: **4 of 8** flows used a port that did not match their content, and two Python clients shared one JA3 hash across two sites);
- **say what TLS 1.3 hid from the box** (measured: TLS 1.2 showed the certificate, TLS 1.3 showed **no certificate** and only the two Hello messages, with the server name still visible);
- **tell IDS from IPS by what each can do** (measured: inline Suricata dropped a path traversal and a blocked TLS name; the same packets replayed in IDS mode produced alerts only; killing the IPS **dropped all traffic** fail-closed and let **all** through with `--queue-bypass`; inline throughput fell from **16.35 to 1.04 Gbit/s** on this machine);
- **explain why a per-packet string match is evaded** (measured: `/etc/passwd` in one segment was dropped, split after 11 bytes it **reached the server**, while Suricata dropped both);
- **say when Aho-Corasick beats a loop of searches and how regular expressions fail** (measured: at **100 patterns** the automaton scanned 32 MB in **111 ms** against **460 ms**, at **10,000** it was **22 times** faster but its scan speed fell from **0.40 GB/s** (10 patterns) to **0.02**; `^(a+)+$` took **2.19 s** at 27 characters in a backtracking engine and **0.13 ms** in RE2, which also matched **a million characters in 1.6 ms**);
- **say what decrypting TLS costs and breaks** (measured: clients that did not trust the firewall CA **failed**, a pinned key **failed**, mutual TLS **failed**, a policy on the body **worked**, and the connection cost **4.18 ms against 1.85**);
- **compute an ESP packet size** (measured by building and decrypting real AES-GCM packets: **56 bytes** of overhead in tunnel mode, **3.7 percent** at 1,500 bytes, **140 percent** at 40, and the largest inner packet for a 1,500 byte link is **1,446 bytes**);
- **defend against a SYN flood and say what each defence moves** (measured: under **2,000 SYN/s** a real client got **0 of 5** connections with no defence and **5 of 5** with SYN cookies on the server and with a SYN proxy in the firewall; **17,979** cookies were sent);
- **protect the firewall's own CPU** (measured: a token bucket of 20 per second with a burst of 10 let **73 of 200** pings through; a per-source bucket kept a second sender at **5 of 5**; a connection limit refused **7 of 10**).

## Where the numbers come from

Each experiment is a script `eN_*.sh` in `prog/` with its output frozen in `v/`; the answers to the problems are `ans_a` to `ans_e`. All scripts source `topo.sh`, which builds the three namespaces (`topo_up`) and removes them (`topo_down`). The traffic generator is `gen.c` (UDP flows, UDP flows with a new source each time, and spoofed SYNs, each at a set rate), the servers are small Python programs, the packet captures use `tcpdump --immediate-mode` and `tshark`, and the matching experiments use `Suricata` (on a netfilter queue), `pyahocorasick` and `google-re2`. Rules are written with `nft` (nftables) except the IPS hook, which uses `iptables-nft -j NFQUEUE` because this kernel has no `nft queue` statement.

Every number in the text is copied from `v/*.out` or computed from a printed figure, and the arithmetic is shown. A number that is not measured is marked **from memory**. A rate is the **median of three runs**; the runs themselves are printed so you can see the spread (about 10 percent on the high rates).

## Data and timing

| What | Size or time |
|---|---|
| ACL cost (`e1_acl_cost.sh`) | 8 rule sets, 3 runs of 3 s each, up to 5,000 rules and 2 million table entries |
| Stateless against stateful (`e2_stateful.sh`) | 3 rule sets, 3 tests each, one conntrack event trace |
| Table exhaustion (`e3_conntrack_table.sh`) | a table limit of 1,000, 40,000 packets, 1,000 assured flows |
| NAT and FTP (`e4_nat_alg.sh`) | 2 client addresses, active FTP with and without the helper |
| Application identification (`e5_appid.sh`) | 8 TCP flows, 2 handshake captures |
| IPS (`e6_ips.sh`) | 3 Suricata rules, 5 requests, segment split, IDS replay, fail-open and fail-closed, 300 connections, a 4 s iperf3 run |
| Aho-Corasick (`e7_aho_corasick.sh`) | 5 pattern counts, 32 MB of text |
| Regular expressions (`e8_regex.sh`) | 6 input sizes, one pattern, 2 engines |
| TLS decryption (`e9_tls_intercept.sh`) | a private PKI, 2 servers, 9 requests, 100 timed handshakes per path |
| ESP size (`e12_esp_size.sh`) | 8 inner sizes, built and decrypted |
| SYN flood (`e10_synflood.sh`) | 4 cases, 18,000 SYNs each |
| SYN queue fill (`e13_synrate.sh`) | 4 rates, 8 s each |
| Rate limiting (`e11_ratelimit.sh`) | 4 cases, 200 pings each |

---


## 1. A firewall is a pipeline of decisions, and every decision has a price

### 1.1 Where the decision is made

A packet entering a Linux router passes **hooks**, points where the kernel asks netfilter what to do with it. The ones that matter for a firewall in the middle of two networks:

| Hook | When | What a firewall does there |
|---|---|---|
| `prerouting` | the packet has arrived, before the routing decision | destination NAT, early drops, "do not track this" |
| `forward` | the packet is routed to another machine | **the main policy**: what may pass through the box |
| `input` | the packet is for the box itself | protect the firewall (section 11) |
| `output` | the box sends a packet | rarely a concern |
| `postrouting` | the packet is about to leave | source NAT (masquerade) |

Every experiment in this tutorial uses the same three namespaces:

```text
   c 10.1.0.2 ---- fc [ fw ] fs ---- s 10.2.0.2
      (client)    10.1.0.1  10.2.0.1   (server)
```

The firewall is `fw`; `fc` and `fs` are its interfaces. A rule says **where it attaches** (a chain on a hook), **what it matches** (addresses, ports, flags, interface, connection state) and **what it does** (accept, drop, reject, count, change the packet). The ruleset is a list per chain, and **the first rule that decides wins**; the chain's **policy** (accept or drop) applies when none does.

A commercial firewall has the same structure under other names: **zones** (the interfaces), **security policies** (an ordered rule list, first match wins), and a **default deny**. What differs is the engine below: where the lookup runs (CPU, network processor, ASIC), and how a long list is made fast.

### 1.2 What one rule costs

The first question about any rule list is its **cost per packet**. `e1_acl_cost.sh` forwards one flow of 46 byte UDP packets through `fw` as fast as `gen` can send them, for 3 s, three times per rule set, and counts what arrives at the server. The two ruleset builders are the interesting part: `linear` writes N rules that never match and then lets the packet through, and `setrule` writes **one** rule whose match is a **set** of N addresses.

```bash
linear() { # N non-matching rules, then accept
  local n=$1
  { echo "table inet f { chain acl { type filter hook forward priority 0; policy accept;"
    for ((k = 1; k <= n; k++)); do echo "ip saddr 10.77.$((k / 250)).$((k % 250 + 1)) udp dport 5000 drop"; done
    echo "}"; echo "}"; } | fw
}
setrule() { # one rule, a set of N addresses
  local n=$1
  { echo "table inet f {"; echo "set bad { type ipv4_addr; elements = {"
    for ((k = 1; k <= n; k++)); do echo "10.77.$((k / 250)).$((k % 250 + 1)),"; done
    echo "10.77.255.255 }"; echo "}"
    echo "chain acl { type filter hook forward priority 0; policy accept;"; echo "ip saddr @bad drop"; echo "}"; echo "}"; } | fw
}
```

The rule sets that follow are the ones in the output: none, then 1, 100, 1,000 and 5,000 non-matching rules, then one rule with a set of 5,000, then a stateful policy (section 2).

```text
--- forwarded rate, one flow, UDP 46 bytes (median of 3 runs of 3 s)
no ruleset                                     814 k/s   (runs: 853 814 793)
stateless, 1 rules in a chain                  767 k/s   (runs: 826 750 767)
stateless, 100 rules in a chain                418 k/s   (runs: 415 418 462)
stateless, 1000 rules in a chain                90 k/s   (runs: 90 90 82)
stateless, 5000 rules in a chain                19 k/s   (runs: 19 19 19)
stateless, one rule, a set of 5000             752 k/s   (runs: 790 752 726)
stateful, one flow (conntrack hit)             710 k/s   (runs: 770 710 703)
stateful, a new flow every packet              302 k/s   (runs: 362 299 302)
```

Read it as arithmetic. The rate is packets per second, so one packet costs `1 / rate`:

| Rule set | Rate | Time per packet | Extra over one rule |
|---|---|---|---|
| no ruleset | 814 k/s | 1.23 microseconds | |
| 1 rule | 767 k/s | 1.30 | |
| 100 rules | 418 k/s | 2.39 | 1.09 for 99 more, **11 ns each** |
| 1,000 rules | 90 k/s | 11.1 | 9.8 for 999 more, **9.8 ns each** |
| 5,000 rules | 19 k/s | 52.6 | 51.3 for 4,999 more, **10.3 ns each** |
| 1 rule, a set of 5,000 | 752 k/s | 1.33 | **0.03** (inside the run to run spread) |

Three facts follow, and all three carry to any firewall.

1. **A rule list is linear.** Each non-matching rule cost about **10 ns** here, so the cost scales with the **position** of the matching rule, and a packet that matches the last of 5,000 rules pays for all of them. At 10 Gbit/s of 64 byte frames (14.9 million packets a second) one core has a budget of **67 ns** a packet. Ten nanoseconds per rule would use **15 percent** of that budget for every rule, and this VM's base cost of 1.23 microseconds is already **18 times** over it. This is why real data planes do not walk lists per packet, and why they spread flows over many cores.
2. **A set, a hash or a trie is flat.** The same 5,000 addresses in one set cost **0.03 microseconds** more than one rule: a hash lookup does not care how many entries it holds. A set of prefixes uses a tree or a trie and grows with the logarithm of its size. The practical rule for a rule list is: **put anything of the form "these N addresses or ports" in a set, and put the rules that match most packets first**.
3. **Order is the other lever.** The first matching rule wins, so a common pattern (established traffic, section 2) belongs at the top. A policy of 5,000 specific rules with "accept established" first costs the new connections 5,000 steps, and every later packet of those connections **one**.

The numbers describe **software** with the generator on the same two CPUs; a hardware pipeline (a TCAM, an ASIC) matches all rules at once and costs a **fixed number of cycles** whatever the length, at the price of the memory those entries take and of a limit on the rule count. **Linux's flowtable offload** (from memory) goes the other way: after the first packets of a flow pass the full policy, later packets of the flow take a short path past most of the stack, which is the software version of "decide once, cache the decision per flow". The kernel here was built without it, so I did not measure it.

### 1.3 The decision is a cache of a decision

Section 2 of this tutorial is the answer to the question "what does a firewall remember between packets?" Everything in sections 2 to 5 is a form of the same idea: **decide on the first packet of a flow with the full policy (slow), and remember the decision so that later packets only look it up (fast)**. The rest of the tutorial prices that memory and shows how it is attacked.

---


## 2. Stateless and stateful filtering

### 2.1 A stateless filter sees one packet at a time

A **stateless** filter decides from the fields of the packet in front of it: addresses, ports, protocol, TCP flags, interface. It has **no memory** of the packets before. To let a client use the Web and nothing else, a stateless policy has to say two things: "the client may send to port 80", and "the server's replies may come back". The second is the difficulty, because a reply is just a packet from port 80, and so is an attack that is dressed as one.

`e2_stateful.sh` runs three tests against three policies. Each test asks the same three questions: can the client open a connection to the server, can the server open a connection to the client, and what happens when the server sends a **lone ACK** to a client port with no connection (`inject.py` builds it with scapy). The first policy is the classic stateless one: anything from the client side passes, and from the server side only packets with ACK set and SYN clear, that is, "not a new connection".

```bash
rules() { # a name and the rules of the forward chain
  nse fw nft flush ruleset
  { echo "table inet f {"; echo "chain acl {"; echo "type filter hook forward priority 0; policy drop;"; cat; echo "}"; echo "}"; } | fw
}
echo "--- stateless, rule 1: allow what the client starts, and inbound packets with ACK set and SYN clear"
rules << 'RULES'
iifname "fc" accept
iifname "fs" tcp flags & (syn | ack) == ack accept
RULES
tests
```

```text
--- stateless, rule 1: allow what the client starts, and inbound packets with ACK set and SYN clear
client opens HTTP to the server (new, outbound)      blocked (timed out)
server opens a connection to the client (new, inbound) blocked (timed out)
server sends a lone ACK to the client, to port 9000: reply: 10.1.0.2:9000 flags R
```

The policy has two defects, one visible and one not. Client to server **timed out**: the server's reply to a SYN is a **SYN-ACK**, which has SYN set, and rule 1 had just said that inbound packets with SYN set are not allowed. A stateless rule cannot tell a SYN-ACK that answers the client from a SYN-ACK that does not. The second defect is in the last line: the lone ACK **reached the client**, which answered with a `RST` (flags `R`). Any stateless rule that accepts "ACK set" accepts a packet that belongs to no connection, and that is a **scan**: the reply tells the sender the host is alive and the port closed, with no connection ever made. That is how an ACK scan maps a stateless firewall's rules.

The second policy fixes the first defect by allowing any inbound packet with ACK set, SYN-ACK included:

```text
--- stateless, rule 2: the same, but any inbound packet with ACK set
client opens HTTP to the server (new, outbound)      works
server opens a connection to the client (new, inbound) blocked (timed out)
server sends a lone ACK to the client, to port 9000: reply: 10.1.0.2:9000 flags R
```

The client's connection now **works**, the server's connection **fails** (a plain SYN has no ACK), and the lone ACK **still gets through**. A stateless filter has a trade-off with no good end: allow all packets that **look like** replies and anyone can send one, allow fewer and replies fail.

### 2.2 A stateful filter remembers the connection

A **stateful** filter keeps a **table of connections**. The first packet of a flow goes through the policy, and if it is allowed the filter records the flow's **5-tuple** (source and destination address, source and destination port, protocol) and a state. Every later packet is looked up by its 5-tuple, in **both directions**, and it is handled according to the entry, not by the policy. The rule becomes: "allow what the client starts, then anything that belongs to a connection already in the table".

```text
--- stateful: allow what the client starts, then only packets that belong to a tracked connection
client opens HTTP to the server (new, outbound)      works
server opens a connection to the client (new, inbound) blocked (timed out)
server sends a lone ACK to the client, to port 9000: reply: none
```

Same three tests, three different answers. The client's connection works, the server's new connection is **still blocked**, and the lone ACK is **answered with nothing**: it is not part of any tracked connection, so it is either classed invalid or looks like a new flow from the wrong side, and no rule accepts either (new flows are allowed only from the client side). The client never sees it. The ACK scan learns nothing.

The next test sends a lone ACK **inside a real connection**, from the client side, once with sequence number 1 and once with 999,999,999, far outside the window:

```text
--- the stateful ruleset again, lone ACKs sent inside a real connection (client port 5555)
lone ACK, seq 1 sent
lone ACK, seq 999999999 sent
                ct state invalid counter packets 1 bytes 60 drop
```

The counter on `ct state invalid` shows **one packet of 60 bytes dropped**, so one of the two ACKs was judged invalid and the other was let through as part of the connection (the script does not print which; the far one is the candidate). The kernel keeps a TCP window per direction and, unless told to be liberal, calls a packet whose sequence number is far outside it invalid (from memory). Tracking a connection is therefore more than remembering a 5-tuple: it is a **TCP state machine** that follows the flags, the sequence numbers and the window, which is why a stateful firewall resists injected packets that a stateless one passes.

### 2.3 The life of a conntrack entry

`conntrack -E` prints each change to the table. One HTTP request, from `SYN` to the end, in the order the kernel recorded it:

```text
--- conntrack events for one HTTP request (time wait shortened to 3 s so the end of the entry shows)
table now:
tcp      6 2 TIME_WAIT src=10.1.0.2 dst=10.2.0.2 sport=55918 dport=80 src=10.2.0.2 dst=10.1.0.2 sport=80 dport=55918 [ASSURED] mark=0 use=1
[NEW] tcp 6 120 SYN_SENT src=10.1.0.2 dst=10.2.0.2 sport=55918 dport=80 [UNREPLIED] src=10.2.0.2 dst=10.1.0.2 sport=80 dport=55918
[UPDATE] tcp 6 60 SYN_RECV src=10.1.0.2 dst=10.2.0.2 sport=55918 dport=80 src=10.2.0.2 dst=10.1.0.2 sport=80 dport=55918
[UPDATE] tcp 6 432000 ESTABLISHED src=10.1.0.2 dst=10.2.0.2 sport=55918 dport=80 src=10.2.0.2 dst=10.1.0.2 sport=80 dport=55918 [ASSURED]
[UPDATE] tcp 6 120 FIN_WAIT src=10.1.0.2 dst=10.2.0.2 sport=55918 dport=80 src=10.2.0.2 dst=10.1.0.2 sport=80 dport=55918 [ASSURED]
[UPDATE] tcp 6 30 LAST_ACK src=10.1.0.2 dst=10.2.0.2 sport=55918 dport=80 src=10.2.0.2 dst=10.1.0.2 sport=80 dport=55918 [ASSURED]
[UPDATE] tcp 6 3 TIME_WAIT src=10.1.0.2 dst=10.2.0.2 sport=55918 dport=80 src=10.2.0.2 dst=10.1.0.2 sport=80 dport=55918 [ASSURED]
v1.4.8 (conntrack-tools): 6 flow events have been shown.
```

Read the states in order: `SYN_SENT` after the client's SYN (a timeout of **120 s** if nothing answers), `SYN_RECV` after the server's SYN-ACK (**60 s**), `ESTABLISHED` after the client's ACK (**432,000 s**, five days), then the close: `FIN_WAIT`, `LAST_ACK` and `TIME_WAIT`. The last one was set to **3 s** here so the end of the entry fits the run; the default is **120 s** (from memory). The flag `[ASSURED]` appears when the kernel has seen traffic in **both** directions, and it matters in section 3: an entry that is not assured can be evicted first when the table is full.

The default timeouts are what a firewall engineer tunes:

```text
--- default timeouts in seconds
tcp_syn_sent   120
tcp_syn_recv   60
tcp_established 432000
tcp_fin_wait   120
tcp_close_wait 60
tcp_last_ack   30
tcp_time_wait  3
tcp_close      10
tcp_max_retrans 300
tcp_unacknowledged 300
udp            30
udp_stream     120
icmp           30
generic        600
```

(Here `tcp_time_wait` shows 3 because the script had just set it.) Four of them decide how much memory an attacker can hold:

- **`tcp_established` 432,000 s** (5 days) is what a connection that **goes silent** holds, with no data, after a client that vanishes (a crashed laptop, a dropped Wi-Fi link). A firewall that must close those lowers it, and the application that needs long idle connections (a database, an SSH session) then needs TCP keepalives shorter than the timeout, or it will find its connection gone.
- **`tcp_syn_sent` 120 s** and **`tcp_syn_recv` 60 s** are what a half-open flow holds. A SYN flood fills the table with these (section 10).
- **`udp` 30 s** and **`udp_stream` 120 s**: UDP has no state of its own, so the entry is a timer. It is refreshed by each packet, and a flow that has seen traffic in both directions earns the longer one (from memory; the next section measures when the entry becomes assured).

The table is the firewall's memory, and **memory is finite**. That is the next section.

---


## 3. The connection table is a resource

### 3.1 What one entry costs

Every tracked connection occupies one **entry**: the two 5-tuples (one per direction, because NAT can make them differ), the state, the timers, counters and helper pointers. Linux allocates it from a slab cache, and the size is readable:

```text
--- table limit and object size
nf_conntrack_max seen from the firewall: 1000
slab object size of one conntrack entry: 256 bytes
```

**256 bytes** an entry in this kernel. The arithmetic of sizing is `entries = new connections per second x lifetime in seconds` (Little's law: the number in a system is the arrival rate times the time each stays), and `memory = entries x 256 bytes`. `ans_b.sh` (Problem B) computes three cases:

```text
this kernel: nf_conntrack_max 262144, hash buckets 262144, entry 256 bytes
case                           new/s    life s    entries    memory
web edge, short flows         200000        30    6000000    1.54 GB
campus NAT, long flows         20000       300    6000000    1.54 GB
DNS resolver front, UDP       100000        30    3000000    0.77 GB
web edge with 25 percent headroom: max 7500000, memory 1.92 GB
average chain length at max if buckets stay at this kernel's value: 28.6
buckets for an average chain of 4: 1875000, bucket array 15.0 MB (8 bytes each)
```

A web edge taking **200,000 new connections a second** that live 30 s on average needs **6 million entries and 1.54 GB**; a campus NAT with a tenth of the rate but a lifetime ten times longer needs **the same**. The lifetime matters as much as the rate, and **it is the timeout that sets it for flows that die silently**, which is why section 2.3's `tcp_established` of 5 days is a number to change on a busy box.

The table is a **hash table** with a fixed number of buckets, and each bucket holds a chain. This kernel has 262,144 buckets and a limit of 262,144 entries (a ratio of one). At 7.5 million entries the same 262,144 buckets would give chains of **28.6** entries on average, so a lookup walks about 29 entries instead of one. The right fix is more buckets, **1.875 million** for a mean chain of 4, whose array is only **15 MB** (the last line above).

### 3.2 What tracking costs per packet

The same experiment as section 1.2 measured the stateful policy, in two cases: **one flow** (every packet finds its entry) and **a new flow in every packet** (every packet creates one).

```text
stateful, one flow (conntrack hit)             710 k/s   (runs: 770 710 703)
stateful, a new flow every packet              302 k/s   (runs: 362 299 302)
--- conntrack counters after the last run
insert_failed 0, drop 0, early_drop 0
entries alive now: 443129
```

| Case | Rate | Time per packet |
|---|---|---|
| one stateless rule | 767 k/s | 1.30 microseconds |
| stateful, one flow (an entry hit) | 710 k/s | 1.41 (**0.11** more: one hash lookup) |
| stateful, a new flow every packet | 302 k/s | **3.31** (**1.90** more than a hit, **2.3 times** the hit) |

Looking up an entry costs little. **Creating** one costs about **1.9 microseconds** here: allocate 256 bytes from the slab, insert into the hash, arm a timer, and later free it. This is why a firewall's data sheet has **two** numbers: **throughput**, for packets of long flows, and **new connections per second**, which is lower by a large factor and is the one an attack on state aims at. A client that opens a connection per request (NET 08 section 3.2) stresses the second number, not the first. In this run nothing was lost (`insert_failed 0, drop 0`), and **443,129 entries** were alive when it ended. The script had set the UDP timeout to 1 s, so the order of magnitude is `rate x timeout` (about 300 k x 1 s); the rest is entries that outlive their timer until the kernel's garbage collection reaches them (from memory). The table is **full of the flow rate times the timeout**, which is the arithmetic of section 3.1 on measured numbers.

### 3.3 What a flow table costs to look up

A **flow table** is the data structure behind all of this: a hash table keyed by the 5-tuple. One detail is often missed and it is a common first question at an interview: **the reply travels in the opposite direction, with source and destination swapped, so both directions of a flow must find the same entry**. The standard answer is a **symmetric key**: put the smaller (address, port) endpoint first before hashing, so `A to B` and `B to A` give the same key. `flowtab.c` is that table, small enough to read in one sitting: `make_key` builds the symmetric key, `hash_key` hashes it (always the canonical key, so both directions land in one bucket), `lookup` walks a bucket's chain, `insert` adds or refreshes, and `expire` (not quoted; it walks every bucket and unlinks entries not seen for longer than a timeout) sweeps the table.

```c
/* Put the smaller (ip, port) endpoint first, so A->B and B->A give the same key. */
static Key make_key(uint32_t sip, uint16_t sp, uint32_t dip, uint16_t dp, uint8_t proto) {
  Key k = {0};
  int swap = sip > dip || (sip == dip && sp > dp);
  k.ip_lo = swap ? dip : sip;  k.ip_hi = swap ? sip : dip;
  k.port_lo = swap ? dp : sp;  k.port_hi = swap ? sp : dp;
  k.proto = proto;
  return k;
}
```

```c
static Flow *lookup(Table *t, const Key *k) {
  for (Flow *f = t->b[hash_key(k) % t->nb]; f; f = f->next)
    if (!memcmp(&f->k, k, sizeof *k)) return f;
  return NULL;
}

static Flow *insert(Table *t, const Key *k, uint32_t now) {
  Flow *f = lookup(t, k);
  if (f) { f->last_seen = now; f->pkts++; return f; }
  f = calloc(1, sizeof *f);
  f->k = *k; f->last_seen = now; f->pkts = 1;
  uint32_t i = hash_key(k) % t->nb;
  f->next = t->b[i]; t->b[i] = f; t->count++;
  return f;
}
```

`ans_c.sh` builds and runs it; the nine checks are in Problem C. The benchmark inserts 1 million flows and looks up 4 million of them **by the reverse direction**, at three table sizes:

```text
 1000000 flows,  1048576 buckets (  1.0 per bucket):  104 ns per lookup, 4000000 of 4000000 found
 1000000 flows,    65536 buckets ( 15.3 per bucket):  639 ns per lookup, 4000000 of 4000000 found
 1000000 flows,     4096 buckets (244.1 per bucket): 12112 ns per lookup, 4000000 of 4000000 found
```

Chain length is the whole story: with **1.0 entry per bucket** a lookup took **104 ns** (a cache miss or two); with **15.3** it took **639 ns**, and with **244** it took **12,112 ns**, **116 times** slower. The cost is linear in the chain length because each hop is a pointer chase to a different cache line. This is why a firewall sizes the **bucket array** from the entry limit, why production tables resize, and why a hash function an attacker can predict is a denial of service (it makes every flow land in one chain: the hash flooding of the hashing tutorials in the DSA track; the kernel mixes a random seed into its conntrack hash for this reason, from memory).

### 3.4 When the table is full

`e3_conntrack_table.sh` sets the limit to **1,000 entries** and attacks it two ways. Case 1: 20,000 new one-way UDP flows a second for 2 s (40,000 packets), to a port that never answers, so **every entry is unreplied**. Case 2: 1,000 flows that the server answers twice, so **every entry is assured**. After each case a legitimate client tries 20 new flows to an echo server.

```bash
echo "--- case 1: 2 s of one-way UDP flows to a port that never answers (every entry is unreplied)"
nse fw conntrack -F > /dev/null 2>&1
nse c ./prog/gen udpn 10.1.0.2 10.2.0.2 5001 2 20000 | sed 's/^/gen: /'
echo "entries: $(nse fw conntrack -C)   $(stats)"
echo "a legitimate client now opens 20 new flows to the echo server:"
nse c python3 prog/flows.py 10.2.0.2 5000 20 1
echo "entries: $(nse fw conntrack -C)   $(stats)"
echo "--- case 2: 1,000 flows that the server answers twice, so every entry is assured"
nse fw conntrack -F > /dev/null 2>&1
nse c python3 prog/flows.py 10.2.0.2 5000 1000 2
echo "entries: $(nse fw conntrack -C)   assured: $(nse fw conntrack -L 2> /dev/null | grep -c ASSURED)"
echo "a legitimate client now opens 20 new flows to the echo server:"
nse c python3 prog/flows.py 10.2.0.2 5000 20 1
echo "entries: $(nse fw conntrack -C)   $(stats)"
```

```text
--- case 1: 2 s of one-way UDP flows to a port that never answers (every entry is unreplied)
gen: sent 40000 packets in 2.00 s (20000 per second)
entries: 1000   insert_failed 0, drop 36632, early_drop 2368
a legitimate client now opens 20 new flows to the echo server:
3 of 20 flows were answered
entries: 1000   insert_failed 0, drop 36649, early_drop 2371
--- case 2: 1,000 flows that the server answers twice, so every entry is assured
1000 of 1000 flows were answered
entries: 1000   assured: 1000
a legitimate client now opens 20 new flows to the echo server:
0 of 20 flows were answered
entries: 1000   insert_failed 0, drop 36669, early_drop 2371
--- the kernel message
nf_conntrack: nf_conntrack: table full in netns 4026532315, dropping packet
--- case 3: the same 20 flows after the table is emptied
20 of 20 flows were answered
```

Read the counters. The table stayed at **1,000** entries. Of the 40,000 packets of flood, **2,368** got in by **early drop** (the kernel evicted an old unreplied entry to make room) and **36,632** were **dropped** because no entry could be evicted; the 1,000 that came first, plus the 2,368 evictions, plus the 36,632 drops, account for all 40,000 packets. The legitimate client then got **3 of 20** flows answered: the counters moved by **3** early drops and **17** drops. I expected early drop to admit all 20, since the table was full of unreplied entries, and it did not. The kernel looks for a victim in only a small part of the table on each attempt (from memory), which fits the result, but I did not verify that here.

Case 2 is the sharper result. With **1,000 assured** entries every entry is protected from early drop, and the legitimate client got **0 of 20**, with the kernel logging `table full, dropping packet` (the `dmesg` line in the output). The assured flags protect established traffic from a flood of half-open flows, which is good, and make a table of **assured** flows a hard limit, which is the cost. The same legitimate flows after the table was flushed (case 3 in the output) worked **20 of 20**, so the failure was the table and nothing else.

Two honest limits on this experiment. UDP becomes assured only after **about 2 s** of traffic in both directions (a gotcha found while building it), which is why case 2 answers each flow **twice**, 2.2 s apart. And early drop only helps against **unreplied** entries; it protects nothing when an attacker completes handshakes, which is the harder attack, and for which only per-source limits (section 11) and capacity help.

### 3.5 What to do about it

| Measure | What it does | What it costs |
|---|---|---|
| a bigger table and a bigger bucket array | more entries before any drop | memory (256 bytes an entry) and cache pressure |
| shorter timeouts for half-open and idle flows | entries free up sooner | idle connections break unless the application keeps them alive |
| **do not track** what needs no state (`notrack`) | the flow takes no entry (high-rate UDP, a DNS server, a SYN flood to a protected port) | the policy for that traffic has to be stateless |
| per-source limits on new and concurrent connections | one source cannot take the table | a NAT gateway makes many users look like one source |
| SYN cookies or a SYN proxy (section 10) | half-open flows take **no** entry | extra CPU on the SYN path |
| a table that scales out (a cluster of firewalls, a flow hash that sends one flow to one node) | capacity grows with nodes | state synchronisation or flow affinity |

---


## 4. NAT and the protocols it breaks

### 4.1 Two tuples per flow

**Network address translation** rewrites addresses (and usually ports) as packets pass. A firewall does it so that many private hosts share one public address (**source NAT**, or **masquerade**) or so that a public address reaches a private server (**destination NAT**, port forwarding). The firewall must remember the rewrite, because the **reply** has to be rewritten back. That memory is the same conntrack entry of section 3, which is why NAT lives inside the stateful engine: an entry holds **two tuples**, the original direction and the reply direction, and NAT is what makes them differ.

`e4_nat_alg.sh` gives the client a second address (10.1.0.3) and opens one connection from each address, **both from source port 40000**, to the same server, through a masquerade rule on the firewall's server side:

```bash
table ip nat {
  chain post {
    type nat hook postrouting priority 100;
    oifname "fs" masquerade
  }
}
```

```text
--- NAT: two client addresses use the same source port
CLOSE src=10.1.0.2 dst=10.2.0.2 sport=40000 dport=80 src=10.2.0.2 dst=10.2.0.1 sport=80 dport=40000 [ASSURED]
CLOSE src=10.1.0.3 dst=10.2.0.2 sport=40000 dport=80 src=10.2.0.2 dst=10.2.0.1 sport=80 dport=64907 [ASSURED]
```

Each line is one entry, two tuples each. The first connection kept `sport=40000` on the firewall's address (reply `dport=40000`). The second, from 10.1.0.3, also arrived with source port 40000, which the table already held for 10.2.0.1, so the firewall **chose another port**, and the reply tuple reads `dport=64907`. Neither client noticed. The server saw two connections from 10.2.0.1, ports 40000 and 64907, and its replies were translated back to the right client.

Three costs follow from that.

1. **One public address has about 64,000 ports per destination.** A firewall can reuse a port for different destinations, so the limit is per (public address, destination address, destination port): a service that thousands of clients reach through one NAT address can **exhaust ports** for that one destination, and new connections then fail. The fix is a **pool** of public addresses.
2. **The entry must live as long as the flow.** A NAT mapping that times out in the middle of a connection breaks it (section 2.3), and a NAT gateway is the usual place where the idle timeout of 5 days is lowered to minutes, so a long idle SSH session needs keepalives.
3. **Inbound connections have no entry.** A private host behind NAT cannot be reached until something creates a mapping, by a port forward rule, or by the host sending first (the basis of hole punching, from memory).

### 4.2 The protocol that carries addresses in its payload

NAT rewrites the **headers**. A protocol that writes an address or port **inside its payload** carries the old one through, and the far end acts on it. FTP active mode is the textbook case: the client sends `PORT a,b,c,d,p1,p2` on the control connection, naming **its own address and port** for the server to connect **back** to. Behind NAT the address in that text is **private**, so the server connects to a private address that is not reachable.

The experiment runs active FTP twice, once with no helper and once with the firewall's **FTP helper** assigned to port 21:

```text
--- FTP active mode through the firewall and NAT, no helper
LIST failed: TimeoutError
```

```text
--- the same with the FTP helper assigned to port 21
LIST worked: ['report.txt']
```

Without a helper, the `LIST` **timed out**: the data connection never arrived. With the helper it worked. The helper is a connection tracking module that **reads the control connection**, and does two things. It **rewrites the payload** (the next block), and it creates an **expectation**: a note in the table that says "a connection from the server to this address and port is coming, and it belongs to the flow that announced it", so the later data connection, which would otherwise be a new inbound flow and be blocked, is let in and tracked as **related**.

```text
--- the PORT command on the client side and on the server side of the firewall
client side: PORT 10,1,0,2,150,175
server side: PORT 10,2,0,1,150,175
```

The client sent `PORT 10,1,0,2,150,175`: address 10.1.0.2, port `150 x 256 + 175 =` **38,575**. The server saw `PORT 10,2,0,1,150,175`: the same port, with the **firewall's** address, because the helper rewrote the text.

```text
--- the expectation the helper created from the PORT command
            [NEW] 300 proto=6 src=10.2.0.2 dst=10.2.0.1 sport=0 dport=38575 mask-src=255.255.255.255 mask-dst=255.255.255.255 sport=0 dport=65535 master-src=10.1.0.2 master-dst=10.2.0.2 sport=46750 dport=21
        [DESTROY] 300 proto=6 src=10.2.0.2 dst=10.2.0.1 sport=0 dport=38575 mask-src=255.255.255.255 mask-dst=255.255.255.255 sport=0 dport=65535 master-src=10.1.0.2 master-dst=10.2.0.2 sport=46750 dport=21
```

The expectation is the first line: `[NEW]`, a timeout of **300 s**, the data connection `src=10.2.0.2 dst=10.2.0.1 dport=38575`, and `master-src=10.1.0.2 ... dport=21`, the control flow that owns it. `[DESTROY]` follows when the data connection arrives and is consumed. The final listing shows both flows: the control connection with `helper=ftp`, and the data connection, in the **reverse direction**, translated back to the client at 10.1.0.2.

```text
--- the two flows now in the table
TIME_WAIT src=10.1.0.2 dst=10.2.0.2 sport=46750 dport=21 src=10.2.0.2 dst=10.2.0.1 sport=21 dport=46750 [ASSURED] helper=ftp
TIME_WAIT src=10.2.0.2 dst=10.2.0.1 sport=44277 dport=38575 src=10.1.0.2 dst=10.2.0.2 sport=38575 dport=44277 [ASSURED]
```

This is an **application layer gateway (ALG)**: a piece of protocol knowledge inside the firewall. The set of protocols that need one has not changed in 20 years (from memory: FTP, SIP and H.323 for voice, TFTP, IRC, PPTP), and each is a place where the firewall **parses untrusted text** and **changes the packet from it**. Two lessons for an interview:

- An ALG is a **parser in the data path**. A bug in it is a remote vulnerability in the firewall, and a firewall that opens a hole on the basis of text a client sent can be made to open a hole to the wrong place. Linux **ties the expectation to the control flow's endpoints** (the `master` fields above) and by default **does not follow a request for a third-party address**, which is the answer to the old FTP bounce attack (from memory).
- **Modern protocols avoid the problem.** FTP **passive** mode makes the client open both connections, so no firewall has to read the control channel; most applications use one port, or protocols with a built-in way to find the mapping (STUN, ICE, from memory). If you can choose, choose the protocol that does not need the ALG.

---


## 5. Knowing what is inside a flow: application identification

### 5.1 Why a port number is not an application

A port firewall says "TCP 443 is HTTPS". That was a convention, and now it is a **hiding place**: a tunnel, a proxy, a file transfer and a command channel all look best on port 443 because that port is always open. **Application identification** (app-ID, in Palo Alto's term) classifies a flow by its **content** and applies policy to the application, whatever the port. The same idea appears in Cisco's "application visibility", in DPI engines like nDPI and Suricata's app-layer detection.

The technique that works with no decryption is **reading the first payload packet of the flow**. Clients speak first in most protocols, and the first bytes carry a signature:

```python
def identify(p):
    if re.match(rb'(GET|POST|PUT|HEAD|DELETE|OPTIONS) /', p):
        m = re.search(rb'\r\nHost: ([^\r\n]+)', p); return 'http', 'Host ' + (m.group(1).decode() if m else '')
    h = client_hello(p)
    if h: return 'tls', 'SNI ' + h[0] + ', JA3 ' + hashlib.md5(h[1].encode()).hexdigest()[:12]
    if p.startswith(b'SSH-'): return 'ssh', 'banner ' + p.split(b'\r\n')[0].decode()
    return 'unknown', ''
```

An HTTP request starts with a **method and a path** (`GET /`), a TLS connection starts with a **ClientHello** (record type `0x16`, handshake type 1), an SSH connection starts with the banner `SSH-2.0-`. The server's first reply, when needed, adds a second look. `e5_appid.sh` starts eight TCP flows through the firewall, **four with a port that does not match the content**, captures them, and classifies each from its first client packet (`classify.py`, 45 lines of scapy):

```text
--- every TCP flow the firewall saw, identified from the first client payload
port  server    port says content   bytes  detail
80    .2        http      http         71  Host 10.2.0.2
80    .3        http      tls        1518  SNI vpn.example, JA3 e8df55bdfa49   <-- port is wrong
443   .2        tls       tls         517  SNI shop.example, JA3 0149f47eabf9
443   .2        tls       tls        1517  SNI py.example, JA3 e8df55bdfa49
443   .3        tls       ssh          21  banner SSH-2.0-OpenSSH_9.6   <-- port is wrong
4444  .3        ?         unknown      64
8080  .2        http      tls         517  SNI api.example, JA3 0149f47eabf9   <-- port is wrong
8443  .2        tls       http         76  Host 10.2.0.2:8443   <-- port is wrong
```

Read the `<-- port is wrong` rows. **Port 80 carried TLS** (the server `.3` was an HTTPS server on the wrong port), **port 443 carried SSH**, **port 8080 carried TLS** and **port 8443 carried plain HTTP**. A port-based policy that allowed "HTTP on 80 and 8080" would have **passed a TLS connection to a different server on port 80 as web traffic** and treated an SSH session on port 443 as HTTPS. The eighth flow, port 4444 with 64 random bytes, is `unknown`: an encrypted or custom protocol that has no signature to find.

### 5.2 What the TLS ClientHello gives away

A ClientHello is not encrypted (the keys do not exist yet), and it carries more than the name:

- the **server name** (SNI), the hostname the client wants: `shop.example`, `api.example`, `vpn.example` above. A policy on **names** ("block `vpn.example`") works without decrypting anything.
- the **cipher suites, extensions, curves and point formats** the client offers, in the order it offers them. The **JA3** fingerprint is the MD5 of those lists joined into one string (the script drops the random "GREASE" values; `classify.py` computes it from the packet, and `tshark` computes it independently below):

```text
--- the same ClientHellos, fingerprinted by tshark (JA3 is the MD5 of the same string)
SNI shop.example JA3 0149f47eabf9
SNI py.example   JA3 e8df55bdfa49
```

`classify.py` found **four** ClientHellos (rows `tls` in the table of 5.1), and the hashes fall into two groups: `curl` to `shop.example` on 443 and to `api.example` on 8080 both have JA3 `0149f47eabf9`, and the Python client to `vpn.example` on port 80 and to `py.example` on 443 both have `e8df55bdfa49`. `tshark` found only **two** of the four, `shop.example` and `py.example`: it decodes by port, so the handshakes on ports 80 and 8080 were not shown as TLS. The two it did decode agree with the script, so the JA3 computation is right; and the missing two repeat the lesson of 5.1 inside an analysis tool. A JA3 hash identifies the **client software and its build** (`curl` with its TLS library, Python's `ssl`), not the site. That is its use: a policy "this fingerprint is a known malware family's client", or "this is not a browser". Its weakness is that a client can change the lists it sends, and that the browsers randomise the order of extensions (JA4, from memory, sorts them first for this reason).

### 5.3 What TLS 1.3 took away

Everything above needs the **first packets in the clear**. TLS 1.3 encrypts the handshake **after** the Hellos, and the measurement shows what that does to a box in the middle:

```text
--- what a box in the middle can read of a handshake: TLS 1.3 against TLS 1.2 (curl to 10.2.0.2:443)
TLS 1.3: handshake messages in the clear: type1 x1  type2 x1
         certificate visible: 0    server name visible: shop.example
TLS 1.2: handshake messages in the clear: type1 x1  type2 x1  type11 x1  type12 x1  type14 x1  type16 x1
         certificate visible: 1    server name visible: shop.example
```

With TLS 1.2 the box saw **six handshake messages** in the clear, among them the **Certificate** (type 11): the server's identity, issuer and name, and the length of the whole handshake. With TLS 1.3 it saw **two** (ClientHello and ServerHello) and **no certificate**. The server name stayed visible in the ClientHello. That is the visibility a firewall keeps without decrypting: the name, the fingerprint, the sizes and timing of the records, and the destination address.

Two more shrinkages are from memory. **Encrypted Client Hello (ECH)** encrypts the inner ClientHello, **including the name**, under a key published in DNS, which removes the SNI policy. **QUIC** (HTTP/3 over UDP) encrypts its handshake and headers with keys derived from the connection ID, so the SNI is readable only by a box that derives the initial keys itself, and many enterprise firewalls just **block UDP 443**, so browsers fall back to TCP. Each step is a trade: the privacy of the user against the visibility of the operator, and the next two sections price the alternatives.

---


## 6. IDS and IPS: detect or prevent

### 6.1 The difference is where the box sits

An **intrusion detection system (IDS)** receives a **copy** of the traffic (a mirror port, a tap) and raises alerts. It is **out of band**: it can be slow, it can fail, and the traffic is not affected, but it **cannot stop** anything. An **intrusion prevention system (IPS)** sits **in the path**, so every packet passes through it, and it can **drop** the ones that match. It is the same engine and the same rules; the difference is the **hook**, and with it three costs: **latency** (every packet waits for the verdict), **throughput** (the engine must keep up with the line), and **availability** (when it dies, the link dies, or the protection does).

`e6_ips.sh` runs **Suricata** on a Linux netfilter queue. The firewall's FORWARD chain sends every packet to queue 0 (`NFQUEUE`), Suricata reads them, inspects, and returns a verdict, accept or drop. The three lab rules:

```text
drop http any any -> any any (msg:"LAB path traversal in URI"; flow:established,to_server; http.uri; content:"/etc/passwd"; sid:1000001; rev:1;)
alert http any any -> any any (msg:"LAB scanner user agent"; flow:established,to_server; http.user_agent; content:"sqlmap"; sid:1000002; rev:1;)
drop tls any any -> any any (msg:"LAB TLS to a blocked name"; flow:established,to_server; tls.sni; content:"blocked.example"; sid:1000003; rev:1;)
```

The first **drops** an HTTP request whose URI contains `/etc/passwd` (path traversal), the second only **alerts** on the user agent `sqlmap` (a scanner), the third **drops** a TLS connection whose server name is `blocked.example`. `flow:established,to_server` limits a rule to client packets of an established connection, and `http.uri`, `http.user_agent` and `tls.sni` are **sticky buffers**: the engine has already parsed the protocol, so the match runs on one field, not on a raw packet.

### 6.2 What the IPS did

```bash
tests() {
  get "GET /index.html" http://10.2.0.2/index.html
  get "GET /etc/passwd  (path traversal rule, drop)" http://10.2.0.2/etc/passwd
  get "GET /index.html, User-Agent sqlmap (alert only)" -A sqlmap/1.7 http://10.2.0.2/index.html
  get "TLS to allowed.example" -k --resolve allowed.example:443:10.2.0.2 https://allowed.example/index.html
  get "TLS to blocked.example (SNI rule, drop)" -k --resolve blocked.example:443:10.2.0.2 https://blocked.example/index.html
}
```

```text
--- no IPS: everything reaches the server
GET /index.html                                HTTP 200 (reached the server)
GET /etc/passwd  (path traversal rule, drop)   HTTP 404 (reached the server)
GET /index.html, User-Agent sqlmap (alert only) HTTP 200 (reached the server)
TLS to allowed.example                         HTTP 200 (reached the server)
TLS to blocked.example (SNI rule, drop)        HTTP 200 (reached the server)
```

```text
--- inline Suricata (iptables sends every forwarded packet to queue 0)
GET /index.html                                HTTP 200 (reached the server)
GET /etc/passwd  (path traversal rule, drop)   no answer within 3 s (dropped)
GET /index.html, User-Agent sqlmap (alert only) HTTP 200 (reached the server)
TLS to allowed.example                         HTTP 200 (reached the server)
TLS to blocked.example (SNI rule, drop)        no answer within 3 s (dropped)
alerts written to eve.json:
  alert sid 1000001 LAB path traversal in URI      action blocked  10.1.0.2:54986 -> 10.2.0.2:80
  alert sid 1000002 LAB scanner user agent         action allowed  10.1.0.2:54988 -> 10.2.0.2:80
  alert sid 1000003 LAB TLS to a blocked name      action blocked  10.1.0.2:53082 -> 10.2.0.2:443
```

With no IPS, all five requests, the attack included, **reached the server**. With inline Suricata the two attacks were **dropped** (no answer within 3 s: the connection stalled), the scanner request with `sqlmap` passed because its rule only alerts, and the clean requests passed. The alert records show the action, `blocked` or `allowed`, per rule. The TLS drop is worth a second look: Suricata read **the server name from the ClientHello** and dropped the connection **without decrypting** anything, which is the visibility of section 5.2 put to work.

### 6.3 What a per-packet match misses

The oldest evasion is **splitting the attack across segments**. TCP is a byte stream, and the sender chooses how to cut it. `split_request.py` sends `GET /etc/passwd` either in one segment or in two, cut after 11 bytes: `GET /etc/pa` and `ssword`. First a firewall that matches the **string in each packet** (`iptables -m string "/etc/passwd"`), then Suricata:

```text
--- an attack cut into two TCP segments, 11 bytes in: GET /etc/pa | ssword
per-packet string match in iptables (-m string "/etc/passwd"):
  one segment:  no answer within 3 s (dropped)
  two segments: HTTP/1.0 404 File not found
Suricata inline (it reassembles the stream before it matches):
  one segment:  no answer within 3 s (dropped)
  two segments: no answer within 3 s (dropped)
```

The per-packet match **dropped the single segment and missed the two**: the string `/etc/passwd` is in neither packet. The server reassembled the stream and answered `404`, so the attack **reached it**. Suricata dropped **both**, because it **reassembles the TCP stream** before it matches, in the way the server will. The same defence has to handle **overlapping segments, out of order segments and IP fragments** with the same bytes meant differently, and has to **resolve each the way the end host does**, which differs by operating system. That is the heart of the problem and why an IPS keeps a large amount of state per flow (the `stream: memcap` and `reassembly: depth` lines of `suricata.conf`), which is also what an attacker can fill (section 3 again).

### 6.4 IDS mode, and what failure looks like

The same two segment packets, **replayed from a capture** to Suricata in IDS mode, with nothing in the path:

```text
--- the same packets, replayed from the capture to Suricata in IDS mode (nothing in the way)
  alert sid 1000001 LAB path traversal in URI      action allowed  10.1.0.2:47282 -> 10.2.0.2:80
  alert sid 1000001 LAB path traversal in URI      action allowed  10.1.0.2:47288 -> 10.2.0.2:80
```

Two alerts, both marked **allowed**: the engine saw the attack and could not stop it. This is the IDS contract, and it is also what you get from an IPS whose rule action is `alert`. That is also the answer to "why not run everything as an IPS?": **a false positive in an IDS is a log line; in an IPS it is an outage for a user.**

The harder question is what happens when the IPS **stops**. With the kernel rule `NFQUEUE` and **nobody reading the queue**, the kernel has nowhere to send the packets:

```text
--- the IPS process dies
rule NFQUEUE , nobody on the queue             no answer within 3 s (dropped)
rule NFQUEUE --queue-bypass, nobody on the queue HTTP 200 (reached the server)
```

**Fail-closed** (plain `NFQUEUE`): every packet **dropped**, the link is down. **Fail-open** (`--queue-bypass`): every packet **accepted**, the link works and **nothing is inspected**. Neither is right everywhere. A data centre firewall in front of a database fails **closed**; a carrier's customer link fails **open**; a hardware appliance adds a **bypass relay** that connects the two ports electrically when the power or the software goes, which is a fail-open decision made in hardware (from memory). The point for an interview is that **availability is a design input of an IPS, not an afterthought**, and that an attacker who can crash the engine with one crafted packet chooses which behaviour you get.

### 6.5 What inline costs

```text
--- what an inline IPS costs on this 2 vCPU machine
new connection and one small request, 300 times:
  no IPS:           median 502 us, p99 976 us
  inline Suricata:  median 744 us, p99 1796 us
one TCP stream for 4 s (iperf3):
  no IPS:           16.35 Gbit/s
  inline Suricata:  1.04 Gbit/s
```

For a **new connection and one small request**, the median rose from **502 to 744 microseconds** (+48 percent) and the p99 from **976 to 1,796** (+84 percent): each packet crosses to user space and back. For **bulk throughput**, one TCP stream fell from **16.35 to 1.04 Gbit/s**, **15.7 times** lower. Read the second number as an order of magnitude and not as Suricata's speed: the VM has **two vCPUs**, the load generator ran on them too, with one queue the engine uses one worker thread (from memory), and a real deployment uses many cores with receive-side scaling, **AF_PACKET** or **AF_XDP** capture instead of the queue, and rules selected for the traffic. What stays true everywhere: **inspection is per byte of payload and costs far more than forwarding**, so a policy that inspects only what it must (the flows from the Internet to the servers, the first kilobytes of each flow, the protocols that have rules) is the main tuning there is. The `reassembly: depth` of 1 MB in this configuration is that choice: after a megabyte of a flow, Suricata stops reassembling it for inspection (from memory).

---


## 7. Matching signatures fast: Aho-Corasick and regular expressions

An IPS rule set is thousands of **signatures**: strings and regular expressions, each to be found in the payload of every flow. A loop that searches for each pattern in turn costs `patterns x bytes`. With 10,000 patterns and 10 Gbit/s of traffic that is 12.5 billion byte-pattern pairs a second, which nothing can do. Two ideas make it feasible: **match all the strings in one pass**, and **match regular expressions without backtracking**.

### 7.1 Aho-Corasick: all strings in one pass

**Aho-Corasick** builds one automaton from all the patterns and runs it over the text once. Three steps:

1. **A trie of the patterns**: one state per prefix, an edge per character, and a mark on every state where a pattern ends.
2. **Failure links**: for each state, the state for the **longest proper suffix of its prefix that is also a prefix in the trie**. When the next character has no edge, the automaton does not restart at the root; it follows the failure link, so the text is never re-read.
3. **Output sets**: a state's output also includes the outputs of its failure link, so a match that is a suffix of another (`he` inside `she`) is reported too.

The scan then costs **one table lookup per byte** (`state = next[state][byte]`) whatever the number of patterns. The two functions that matter, from `ac.c`, are `finish`, which fills the failure links in breadth first order **and turns the trie into a complete automaton** (so the scan never follows a failure link at run time), and the scan loop itself:

```c
static void finish(AC *a) {  /* breadth first: fill the failure links and turn the trie into a full automaton */
  int *q = malloc(a->states * sizeof(int)), head = 0, tail = 0;
  for (int c = 0; c < 256; c++) {
    int t = a->next[c];
    if (t < 0) a->next[c] = 0; else { a->fail[t] = 0; q[tail++] = t; }
  }
  while (head < tail) {
    int s = q[head++];
    a->hits[s] += a->hits[a->fail[s]];
    for (int c = 0; c < 256; c++) {
      int t = a->next[(size_t)s * 256 + c], f = a->next[(size_t)a->fail[s] * 256 + c];
      if (t < 0) a->next[(size_t)s * 256 + c] = f; else { a->fail[t] = f; q[tail++] = t; }
    }
  }
  free(q);
}
```

```c
  t0 = now(); long found = 0; int s = 0;
  for (int i = 0; i < TEXT; i++) { s = a.next[(size_t)s * 256 + (unsigned char)text[i]]; found += a.hits[s]; }
```

Problem D builds the automaton for `he`, `she`, `his` and `hers` by hand, runs it over `ushers`, and checks it against a library.

`e7_aho_corasick.sh` times both approaches on 32 MB of random text with N random patterns of 10 to 16 letters, each planted 300 times (so the number of matches is known, and the two methods must find the **same** number: the last column says `equal` every time). One side calls `memmem` once per pattern; the other builds the automaton once and scans once.

```text
     1 patterns | memmem per pattern      12.0 ms | automaton      12 states     0.0 MB, build     0.0 ms, scan   86.1 ms (0.39 GB/s) | matches 300 equal
    10 patterns | memmem per pattern      51.3 ms | automaton     131 states     0.1 MB, build     0.1 ms, scan   84.2 ms (0.40 GB/s) | matches 2996 equal
   100 patterns | memmem per pattern     460.2 ms | automaton    1229 states     1.2 MB, build     1.4 ms, scan  111.2 ms (0.30 GB/s) | matches 29643 equal
  1000 patterns | memmem per pattern    4462.0 ms | automaton   11464 states    11.2 MB, build    13.8 ms, scan  455.9 ms (0.07 GB/s) | matches 268885 equal
 10000 patterns | memmem per pattern   42331.0 ms | automaton  108291 states   105.8 MB, build   184.9 ms, scan 1913.3 ms (0.02 GB/s) | matches 1204102 equal
```

| Patterns | `memmem` per pattern | Automaton scan | Ratio | Automaton memory |
|---|---|---|---|---|
| 1 | 12.0 ms | 86.1 ms | **0.14** (memmem wins) | 0.0 MB |
| 10 | 51.3 ms | 84.2 ms | **0.61** (memmem wins) | 0.1 MB |
| 100 | 460 ms | 111 ms | **4.1** | 1.2 MB |
| 1,000 | 4,462 ms | 456 ms | **9.8** | 11.2 MB |
| 10,000 | 42,331 ms | 1,913 ms | **22** | 105.8 MB |

Four conclusions, each from a column.

1. **There is a crossover, and it is not at 2 patterns.** `memmem` (a tuned library search, with a fast skip loop) beat the automaton for 1 and 10 patterns. Its cost grows by about **5 ms for each pattern** (51.3 ms for 10), the automaton's by almost nothing at first (86.1 ms to 84.2 ms), so the lines cross near **16 patterns** on this text (arithmetic from the table: 84 ms / 5.1 ms). A Staff answer says "one pass for many patterns", and **also** says "for one pattern, use the library".
2. **The ratio keeps growing** because the automaton's cost is flat and the loop's is linear: **22 times** at 10,000 patterns.
3. **The scan itself slows down as the automaton grows**: **0.40 GB/s** for 10 patterns and **0.02 GB/s** for 10,000. Each state here has a full row of 256 integers, **1,024 bytes**, so 10,000 patterns make a **105.8 MB** table that does not fit in any cache, and every byte of text is a cache miss. The cost of a pass is one memory access per byte, **and which level of memory it comes from decides the speed**. Real engines **compress** the table (a sparse or bitmapped row, a few bytes per state), keep the **top levels** in cache, and use **SIMD**: Hyperscan (used by Suricata and Snort 3, from memory) and its Arm port Vectorscan run a whole set of literals and regular expressions together with vector instructions, at several GB/s a core (from memory).
4. **Build time and memory are the price**: 10,000 patterns took **185 ms** and **106 MB** to build. A rule update on a live box rebuilds the automaton, so an engine builds the new one **while the old one still runs** and swaps (from memory), which is also why a rule reload briefly doubles the memory.

The test is random lowercase text with random patterns, which has almost no false prefixes. Real traffic has **long shared prefixes** (`GET /`, `HTTP/1.1`, `Content-`) that make the automaton wander deeper, and real signatures have **wildcards**, so these numbers show the shape and not what a rule set costs.

### 7.2 Regular expressions: backtracking against automata

Real signatures are not all strings: `^GET /[a-z]+\.php\?id=\d+'` is a regular expression. There are two ways to run one.

- **A backtracking engine** (Perl, Python `re`, Java, JavaScript, PCRE) tries one path through the pattern and, on failure, **goes back** and tries another. It supports extras (back references, look-ahead), and in the worst case its time is **exponential** in the input.
- **An automaton engine** (RE2, Hyperscan, the DFA mode of grep) compiles the pattern into a **finite automaton** and reads the input **once**, so the time is linear in the input and **bounded whatever the pattern**. It gives up the extras.

`redos.py` runs the pattern `^(a+)+$` against `n` letters `a` followed by `!`, in Python's backtracking engine and in RE2. The pattern asks "is this all `a`s?" and the `!` makes the answer no, **after** the backtracking engine has tried every way to split the `a`s between the inner and the outer `+`:

```python
# redos.py: the pattern ^(a+)+$ against n letters 'a' followed by '!', in a backtracking engine (Python re) and an automaton engine (RE2)
import re, re2, time
pat = r'^(a+)+$'
bt, dfa = re.compile(pat), re2.compile(pat)
print('%12s %18s %18s' % ('input size', 'backtracking (s)', 'RE2 (s)'))
for n in (16, 18, 20, 22, 24, 26):
    s = 'a' * n + '!'
    t = time.perf_counter(); bt.match(s); t1 = time.perf_counter() - t
    t = time.perf_counter(); dfa.match(s); t2 = time.perf_counter() - t
    print('%12d %18.4f %18.6f' % (n + 1, t1, t2))
s = 'a' * 1_000_000 + '!'
t = time.perf_counter(); r = dfa.match(s); t2 = time.perf_counter() - t
print('%12d %18s %18.6f   (match: %s)' % (len(s), 'not run', t2, r is not None))
```

```text
  input size   backtracking (s)            RE2 (s)
          17             0.0021           0.000026
          19             0.0085           0.000021
          21             0.0302           0.000027
          23             0.1656           0.000046
          25             0.5941           0.000061
          27             2.1904           0.000134
     1000001            not run           0.001617   (match: False)
```

The backtracking time went **0.0021, 0.0085, 0.0302, 0.1656, 0.5941, 2.1904 s** as the input grew by **two characters** each step: about **4 times per step**, so it **doubles with every added character**. The line from 17 to 27 characters is ten characters and a factor of `2.1904 / 0.0021 =` **1,043**, close to `2^10` = 1,024. Extrapolating by the same doubling (arithmetic, not run), **47 characters would take about 27 days**. RE2 took **0.000026 to 0.000134 s** on the same inputs, and **0.0016 s for a million characters**.

This is **ReDoS**, regular expression denial of service, and it is a **data plane** vulnerability: a firewall, a WAF or a proxy that matches a user-supplied string against a vulnerable pattern can be stopped by **one request**. Defences: use an automaton engine for anything that sees untrusted input; if a backtracking engine is unavoidable, set a **step limit or timeout**; review patterns for **nested quantifiers** over overlapping sets (`(a+)+`, `(a|aa)+`, `(.*)*`). The price of an automaton engine is that it cannot do back references (`(\w+) \1`) and some look-around, and a good engine tells you at compile time, which is a feature.

---


## 8. Decrypting TLS: what it buys, what it breaks, and what else exists

Section 5.3 ended on a fact: with TLS 1.3 the box in the middle sees the name, a fingerprint and the sizes of the records, and **nothing inside**. A signature on the HTTP request, a data loss rule on an upload, or an IPS rule on a web application attack cannot match on bytes it cannot read. The firewall's answer is to become one end of two TLS connections, which is **TLS decryption** (also called TLS interception, SSL inspection, or a **forward proxy**).

### 8.1 How a forward proxy works

The firewall stands between the client and the real server and **terminates** the client's TLS itself:

1. The client's ClientHello (with the name `secure.example`) reaches the firewall, which **redirected** the packet to a local proxy process. The proxy reads the **original destination** from the connection tracking entry (`SO_ORIGINAL_DST`, the `getsockopt` line in `handle` below).
2. The proxy **creates a certificate for `secure.example`**, signed by the **firewall's own CA**, and finishes the handshake with the client using it.
3. The proxy opens a **second** TLS connection to the real server, and **validates the server's certificate** against the real roots (the `upstream.wrap_socket` call in `handle`). If it did not, the firewall would be the weakest link of the path.
4. Between the two, the proxy sees the **plain request**, applies policy, and forwards it.

```bash
nse fw nft -f - << 'RULES'
table ip nat {
  chain pre {
    type nat hook prerouting priority -100;
    iifname "fc" ip daddr 10.2.0.2 tcp dport { 443, 4443 } redirect to :8443
  }
}
RULES
```

```python
def handle(raw):
    name = []
    base = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    base.sni_callback = lambda s, n, c: (name.append(n), setattr(s, 'context', context_for(n or 'unknown')))[0]
    ip, port = struct.unpack('!2xH4s8x', raw.getsockopt(socket.SOL_IP, 80, 16))[::-1]  # SO_ORIGINAL_DST
    try:
        client = base.wrap_socket(raw, server_side=True)
    except OSError as e:
        print('client handshake failed:', e.reason if hasattr(e, 'reason') else e, flush=True); return
    try:
        up = upstream.wrap_socket(socket.create_connection((socket.inet_ntoa(ip), port)), server_hostname=name[0])
    except OSError as e:
        print('upstream handshake failed for %s: %s' % (name[0], getattr(e, 'reason', e)), flush=True); client.close(); return
    first = client.recv(65536)
    if not first:
        print('client closed before it sent a request', flush=True); client.close(); up.close(); return
    line = first.split(b'\r\n')[0].decode(errors='replace')
    if b'secret-project' in first:
        print('inspected %s %s -> BLOCKED by policy (found "secret-project")' % (name[0], line), flush=True)
        client.sendall(b'HTTP/1.1 403 Blocked\r\nContent-Length: 0\r\nConnection: close\r\n\r\n'); client.close(); up.close(); return
    print('inspected %s %s' % (name[0], line), flush=True)
    up.sendall(first)
    t = threading.Thread(target=pipe, args=(up, client, 'server side')); t.start(); pipe(client, up, 'client side'); t.join(); client.close(); up.close()
```

The part that makes it work is a **trust decision made on the client**: the client accepts the forged certificate **only if the firewall's CA is in its trust store**. An enterprise puts it there on every managed machine (a group policy, a device management profile), which is why decryption is an **enterprise** technique for traffic from machines the organisation controls, and not something a carrier can do to customers.

### 8.2 What the experiment shows

`e9_tls_intercept.sh` has a private PKI, two servers, and a firewall that **decrypts one of them** (`secure.example`, 10.2.0.2) and **leaves the other alone** (`bank.example`, 10.2.0.3, which the NAT rule does not redirect). The column `issuer` is the certificate the **client** was shown.

```text
--- one server the firewall leaves alone (bank.example, 10.2.0.3) and one it decrypts (secure.example, 10.2.0.2)
bank.example, client trusts Origin CA            issuer CN=Origin CA               HTTP 200
secure.example, client trusts Origin CA only     issuer -                          HTTP 000 | SSL certificate problem: unable to get local issuer certificate
secure.example, client trusts the firewall CA    issuer CN=Firewall Inspection CA  HTTP 200
  same, client pins the real server key          issuer CN=Firewall Inspection CA  HTTP 000 | SSL: public key does not match pinned public key
```

- **`bank.example`, client trusts the origin CA: works**, and the issuer is `Origin CA`: this flow is not decrypted, and this is the **bypass** that every real deployment has for banks, health sites and anything legal or policy says must not be opened.
- **`secure.example`, client trusts only the origin CA: fails** with `unable to get local issuer certificate`. The client got a certificate signed by a CA it does not know. This is what every **unmanaged** device sees: a certificate error on every decrypted site.
- **`secure.example`, client trusts the firewall CA: works**, and the issuer is `Firewall Inspection CA`: the client **sees the forged certificate**, and accepts it.
- **same, with the client pinning the real server's key: fails** with `public key does not match pinned public key`. **Certificate pinning** is an application's decision to accept only one key, and it is **exactly the defence against this**. Mobile apps and some update clients pin, so a firewall has to **exclude** them from decryption, or they stop working.

Then **mutual TLS**, where the server demands a **client certificate**:

```text
--- mutual TLS: the server demands a client certificate (port 4443)
bank.example, client sends its certificate       issuer CN=Origin CA               HTTP 200
secure.example, decrypted, client sends it       issuer CN=Firewall Inspection CA  HTTP 000 | Empty reply from server
```

Direct to `bank.example` with the client certificate: **works**. Through the decrypting proxy: **fails**. The proxy terminated the client's connection and made a **new** one to the server, and the client's certificate **cannot be presented on a connection the client does not own**: its signature is made with a private key only the client holds. The server answered the proxy with `certificate required` (the proxy log below) and the client saw an empty reply. Anything authenticated by a client certificate (many APIs, device to cloud connections, some VPNs) **cannot be decrypted** and has to be excluded.

### 8.3 What it buys

```text
--- what decryption buys: a policy on the request inside TLS
POST secret-project to bank.example (not decrypted) issuer CN=Origin CA               HTTP 200
POST secret-project to secure.example (decrypted) issuer CN=Firewall Inspection CA  HTTP 403
GET /report to secure.example (decrypted)        issuer CN=Firewall Inspection CA  HTTP 200
what the proxy logged:
  client handshake failed: TLSV1_ALERT_UNKNOWN_CA
  inspected secure.example GET / HTTP/1.1
  client closed before it sent a request
  inspected secure.example GET / HTTP/1.1
  server side closed with an error: TLSV13_ALERT_CERTIFICATE_REQUIRED
  inspected secure.example POST /upload HTTP/1.1 -> BLOCKED by policy (found "secret-project")
  inspected secure.example GET /report HTTP/1.1
```

The same request `POST ... note=secret-project-x` was **not blocked** to the bank (not decrypted, the firewall cannot see it) and was **blocked with `403`** to `secure.example`, by the proxy log line `BLOCKED by policy (found "secret-project")`. A `GET /report` passed. That is the benefit in one pair of lines: a **policy on the content of the request**, the same function as the per-packet match of section 6, for traffic the firewall could not otherwise open. A real firewall applies the whole stack of this tutorial to the decrypted stream: signatures, malware scanning, URL categories, data loss rules.

### 8.4 What it costs

```text
--- what it costs: new connection, full TLS handshake and one request, 100 times (an 80 line Python proxy: read the ratio, not the milliseconds)
  direct to bank.example:      median 1.85 ms, p99 2.89 ms
  through the proxy:           median 4.18 ms, p99 6.23 ms
```

A new connection with a full handshake and one request took a **median of 1.85 ms** direct and **4.18 ms** through the proxy (**2.3 times**; p99 **2.89** and **6.23 ms**). The proxy does **two** handshakes (one with the forged certificate, one with the real server) and two copies of the data, and it is 80 lines of Python, so **read the ratio and not the milliseconds**. A real firewall hides the cost with hardware crypto, **session resumption** (the first handshake is the expensive one, NET 08 section 6) and a cache of forged certificates, but it is still a **proxy with per-connection state**: sizing for decryption is sizing for **new TLS connections a second**, and a box that handles 10 Gbit/s of clear traffic may handle a fraction of that decrypted (from memory, vendors publish both figures, and the second is the one to read).

Other costs that are not in the numbers:

- **Privacy and law.** A decrypting firewall reads passwords and personal data. Organisations exclude categories, log less, and tell users.
- **A single point of compromise.** The firewall's CA private key can impersonate **any** site to **every** client that trusts it. It belongs in an HSM, and the CA certificate should carry a **name constraint** where clients support it (from memory).
- **Breakage.** Pinned apps, client certificates, protocols over TLS that are not HTTP, and **newer TLS features** that the proxy does not support (a proxy that does not speak a new extension may **downgrade** the connection, or break it).

### 8.5 Alternatives with no forged certificate

The measurement ends with the cleanest alternative: the **client writes its session keys to a file** (`SSLKEYLOGFILE`, supported by browsers, curl and the common TLS libraries). A capture plus that file lets Wireshark decrypt the session **offline**:

```text
--- the alternative that needs no forged certificate: the client writes its session keys to a file (SSLKEYLOGFILE)
HTTP requests tshark sees without the keys: 0
HTTP requests tshark sees with the key log: GET bank.example /account/12345
```

Without the keys `tshark` saw **0** HTTP requests; with the key log it read `GET bank.example /account/12345`, a request to the one server the firewall had **not** touched. This is a **debugging** tool for the machine you control and not a monitoring one (the keys must be collected from every client), but it is the right way to answer "what is my application sending?" without breaking anything. Two other approaches, from memory: **endpoint agents** that inspect before encryption or after decryption on the machine, and **metadata only** policy (names, JA3, certificates from TLS 1.2, record sizes), which keeps the privacy and loses the content.

---


## 9. IPsec and VPNs: what a tunnel costs on the wire

The firewall's own traffic is not the only thing it carries: most firewalls are also the **VPN gateway**, the box where an encrypted tunnel from a branch office or a laptop ends. This kernel has **no ESP and no WireGuard**, so no tunnel was run. What can be measured is the **packet format**, and that decides most of what an engineer has to know: the overhead, the size limit and what a box in the middle can still see.

```text
CONFIG_INET_ESP is not set in this kernel
CONFIG_WIREGUARD is not set in this kernel
```

### 9.1 The ESP packet

**IPsec** protects IP packets with a pair of protocols (from memory): **AH** authenticates the packet and is rarely used now (it also covers fields that NAT changes), and **ESP** (protocol number 50) **encrypts and authenticates**, and is what everyone means by IPsec. It has two modes:

- **Transport mode** protects the payload of a packet between two hosts and keeps the original IP header.
- **Tunnel mode** puts the **whole original packet** inside a new one with a new outer IP header: the mode of a site to site VPN, because the inner addresses can be private.

An **SA** (security association) is one direction of one tunnel: its keys, its algorithms, and a 32 bit **SPI** that the receiver uses to find them. **IKEv2** (UDP 500, and 4500 when NAT is in the way, from memory) negotiates the SAs, authenticates both ends with certificates or a pre-shared key, and **rekeys** them on a timer or a byte count. The ESP packet in tunnel mode with AES-GCM (RFC 4106) is:

```text
[ outer IP 20 ][ SPI 4 | sequence 4 ][ IV 8 ][ encrypted: inner packet | padding | pad length 1 | next header 1 ][ tag 16 ]
```

`esp_size.py` builds exactly this with a real AES-GCM implementation (the `cryptography` library), **decrypts every packet it makes**, and compares it with the original. The function below is the format:

```python
def esp_tunnel(inner, spi=0x1001, seq=1, udp_encap=False):
    pad_len = (-(len(inner) + 2)) % 4                      # payload + pad + 2 trailer bytes must be a multiple of 4
    plain = inner + bytes(range(1, pad_len + 1)) + bytes([pad_len, 4])   # next header 4: an IPv4 packet inside
    iv = os.urandom(8)
    header = struct.pack('!II', spi, seq)
    sealed = aead.encrypt(salt + iv, plain, header)         # nonce is the 4 byte salt and the 8 byte IV; the ESP header is authenticated
    esp = header + iv + sealed                              # the sealed part ends with the 16 byte tag
    assert aead.decrypt(salt + iv, sealed, header)[:len(inner)] == inner
    return (20 + (8 if udp_encap else 0) + len(esp)), pad_len
```

The nonce is the 4 byte salt from the key material and the 8 byte IV; the ESP header (SPI and sequence number) is **authenticated but not encrypted**, so the receiver can find the SA and check the replay window **before** it decrypts. The padding makes the encrypted part a multiple of 4 bytes.

### 9.2 The overhead, and the size limit

```text
inner bytes  padding   on wire  overhead overhead % with NAT-T
         40        2        96        56     140.0%        104
         64        2       120        56      87.5%        128
        100        2       156        56      56.0%        164
        576        2       632        56       9.7%        640
       1000        2      1056        56       5.6%       1064
       1400        2      1456        56       4.0%       1464
       1444        2      1500        56       3.9%       1508
       1500        2      1556        56       3.7%       1564
largest inner packet that fits a 1500 byte link: 1446 bytes (1438 with UDP encapsulation on port 4500)
every packet above was also decrypted and compared with the original: ok
```

The overhead is **56 bytes** for every packet here: `20` outer IP + `8` SPI and sequence + `8` IV + `16` tag + `2` for the pad length and next header + `2` padding. The padding makes the encrypted part a multiple of 4 bytes, so it is 0 to 3 bytes and the overhead is **54 to 57** in general; every row above needed 2 because every inner size I chose is a multiple of 4. In relative terms:

| Inner packet | Overhead | Why it matters |
|---|---|---|
| **40 bytes** (a TCP ACK) | **140 percent** | the packets that carry no data are the ones that suffer: a flow of ACKs more than doubles |
| 100 bytes | 56 percent | voice, small RPCs |
| 576 bytes | 9.7 percent | |
| 1,500 bytes | **3.7 percent** | bulk data: the overhead is nearly free in bytes |

and, with **UDP encapsulation** (NAT traversal, ESP carried inside UDP port 4500, so a NAT can translate it, +8 bytes), each packet is 8 larger (the last column).

**The size limit is the one that causes outages.** A 1,500 byte link carries at most **1,446 bytes** of inner packet (**1,438** with NAT-T). A host that sends full 1,500 byte packets into the tunnel finds the result 56 bytes **too big** for the path. The packet is either **fragmented** after encryption (two packets for the receiver to put together, and the second fragment is a plain packet that a firewall can drop, from memory), or dropped with an ICMP "fragmentation needed" that **may never arrive** (a firewall blocking ICMP, a classic black hole). The standard fix is **MSS clamping**: the firewall rewrites the TCP MSS option in the handshake to the tunnel's limit. By the arithmetic here: **1,446 minus 40** bytes of inner IP and TCP headers gives an MSS of **1,406** (1,398 with NAT-T). The rule is the nftables `tcp option maxseg size set rt mtu` (from memory) or the vendor's "adjust TCP MSS", and it belongs on **every** tunnel and on every link with a smaller MTU than the hosts assume.

### 9.3 What a firewall in the middle sees, and what scales

- **Nothing inside.** ESP is encrypted, so the middle box sees two addresses, protocol 50 (or UDP 4500), the SPI and the sequence numbers. It can rate limit and account for the tunnel, not inspect it. That is why inspection happens **at the VPN gateway after decryption**, where a firewall policy applies to the inner traffic exactly as in the earlier sections.
- **One tunnel is one SA, and an SA tends to be one flow for the hardware.** A single ESP flow often hashes to one receive queue and one core (the 5-tuple is only the outer address pair and the SPI), so one tunnel's throughput is **one core's worth** of AES-GCM unless the implementation spreads it, a common reason a 10 Gbit/s VPN gateway reaches a few Gbit/s on a single tunnel and the full rate only across many tunnels (from memory).
- **Anti-replay makes reordering visible.** The receiver keeps a sliding window of sequence numbers and **drops a packet older than the window**, so a path that **reorders** packets heavily (multipath, a multi-queue sender) loses traffic that was merely late (from memory).
- **WireGuard is a smaller design** (from memory, not run here): one UDP port, a fixed set of algorithms (Curve25519, ChaCha20-Poly1305), and a 16 byte header with a 16 byte tag, so the overhead over IPv4 is **20 + 8 + 16 + 16 = 60 bytes**, a little more than ESP in tunnel mode, in exchange for a protocol that fits in a few thousand lines.

---


## 10. Floods: the SYN flood and what each defence moves

### 10.1 What a flood attacks

A **denial of service** attack exhausts a resource, and every resource in this tutorial has one: **bandwidth** (fill the link), **packets per second** (the rule list of section 1), **state** (the table of section 3, a server's half-open queue), **CPU** (a regular expression of section 7, a TLS handshake of section 8) and **application capacity** (an expensive web request). A **distributed** attack (DDoS) uses many sources, so that a per-source limit does not help. A **spoofed** attack forges the source address, so a reply goes to a third party and the sender cannot be found. A **reflection** attack sends a small request, with the **victim's** address as the source, to a server that answers with a much larger reply (DNS, NTP, memcached, from memory): the amplification is the ratio of reply to request.

The oldest and most instructive state attack is the **SYN flood**, and it can be shown with one C program (`gen syn` sends SYNs with random source addresses) and a counter file.

### 10.2 The half-open queue

When a SYN arrives at a listening socket, the kernel creates a small record, sends a SYN-ACK, and waits for the client's ACK. The record sits in the **SYN queue**, and it stays there until the ACK arrives or the SYN-ACK retransmissions give up. A flood of SYNs from addresses that never answer **fills the queue**, and the next SYN, a real one, is dropped. The server is up, its CPU is idle, and nobody can connect. `e10_synflood.sh` puts a server with a **backlog of 16** behind the firewall, and floods it with **2,000 SYN per second for 9 s** (18,000 SYNs) from spoofed addresses. A real client tries 5 connections during the flood. Four cases:

```text
--- no flood, tcp_syncookies=0 on the server
a real client, 5 connections:  5 of 5 connections worked, median 0.2 ms, slowest 2 ms
```

```text
--- 2,000 spoofed SYNs per second for 9 s, tcp_syncookies=0
half-open connections on the server after 3 s: 17  (listen backlog 16)
a real client, 5 connections during the flood:  0 of 5 connections worked
flood: sent 18000 packets in 9.00 s (2000 per second)
server counters:
  TcpExtSyncookiesSent         0
  TcpExtSyncookiesRecv         0
  TcpExtSyncookiesFailed       0
  TcpExtListenOverflows        0
  TcpExtListenDrops            17979
  TcpExtTCPReqQFullDoCookies   0
  TcpExtTCPReqQFullDrop        17979
```

With **no defence** the queue held **17 half-open connections** (the backlog of 16, one over: the kernel lets the queue run one past its limit, from memory), and the real client got **0 of 5**. The counters tell the same story: `ListenDrops` and `TCPReqQFullDrop` both **17,979**: nearly every one of the 18,000 SYNs was dropped because the queue was full, and the real client's SYNs were dropped with them. Without the flood the same client got **5 of 5** with a median of **0.2 ms**.

### 10.3 SYN cookies: put the state in the packet

A **SYN cookie** removes the state. When the queue is full, the server does **not** keep a record: it computes the initial sequence number of its SYN-ACK as a **keyed hash of the connection's addresses and ports plus a coarse timestamp** (and a few bits encoding the MSS, from memory), and sends it. A real client's ACK acknowledges that number, so the server can **recompute** it, check it, and **build the connection then**. A spoofed SYN never produces the ACK, and the flood leaves no trace on the server.

```text
--- the same flood, tcp_syncookies=1 (a cookie is sent only when the SYN queue is full)
half-open connections on the server after 3 s: 17  (listen backlog 16)
a real client, 5 connections during the flood:  5 of 5 connections worked, median 0.2 ms, slowest 2 ms
flood: sent 18000 packets in 9.00 s (2000 per second)
server counters:
  TcpExtSyncookiesSent         17979
  TcpExtSyncookiesRecv         5
  TcpExtSyncookiesFailed       0
  TcpExtListenOverflows        0
  TcpExtListenDrops            0
  TcpExtTCPReqQFullDoCookies   17979
  TcpExtTCPReqQFullDrop        0
```

The queue still showed 17 half-open entries after 3 s, because **a cookie is sent only when the queue is full** (`TCPReqQFullDoCookies` **17,979**: the SYNs that the first run dropped were answered with cookies now). The counters: `SyncookiesSent` **17,979**, `SyncookiesRecv` **5** (the five real ACKs, each checked and accepted), `SyncookiesFailed` **0**, and `ListenDrops` **0**. The real client got **5 of 5**. The server is **not** protected from the flood's CPU cost (each cookie is a hash and a packet), and the connection that starts from a cookie has **lost some TCP options**: the server kept nothing to remember what the client offered, so what could not be encoded in the cookie (from memory, window scaling and SACK, unless the timestamp option carries them) is lost for that connection. For a server under attack that is a good trade, which is why the setting is on by default in most distributions and used only under pressure.

### 10.4 A SYN proxy: put the state in the firewall

The same idea can sit in the **firewall**, protecting servers that have no cookies (an embedded device, an old system). With a **SYN proxy** the firewall answers the client's SYN **itself**, with a cookie, and only when a real ACK comes back does it open a connection to the server. The server **never sees the flood**. The rules, as run in the fourth case:

```bash
nse fw sysctl -qw net.ipv4.tcp_syncookies=1 net.netfilter.nf_conntrack_tcp_loose=0
nse fw nft -f - << 'RULES'
table inet f {
  chain raw {
    type filter hook prerouting priority raw;
    tcp dport 8080 tcp flags syn notrack
  }
  chain acl {
    type filter hook forward priority 0;
    tcp dport 8080 ct state invalid,untracked counter synproxy mss 1460 wscale 7 timestamp sack-perm
    ct state invalid drop
  }
}
RULES
```

The first rule marks every SYN to port 8080 **untracked** (no conntrack entry is created for it), and the `synproxy` rule handles packets of untracked or invalid state: it answers the SYN with a cookie, and validates the ACK. `nf_conntrack_tcp_loose=0` keeps the table from accepting a connection it did not see begin.

```text
--- the same flood, cookies off on the server, a SYN proxy in the firewall
half-open connections on the server after 3 s: 0  (listen backlog 16)
a real client, 5 connections during the flood:  5 of 5 connections worked, median 0.2 ms, slowest 2 ms
flood: sent 18000 packets in 9.00 s (2000 per second)
server counters:
  TcpExtSyncookiesSent         0
  TcpExtSyncookiesRecv         0
  TcpExtSyncookiesFailed       0
  TcpExtListenOverflows        0
  TcpExtListenDrops            0
  TcpExtTCPReqQFullDoCookies   0
  TcpExtTCPReqQFullDrop        0
firewall rule counter:
tcp dport 8080 ct state invalid,untracked counter packets 18010 bytes 720560
```

On the server: **0 half-open** entries, **no cookies sent**, **no drops**, because the server was never asked. The real client got **5 of 5**. The firewall rule counter matched **18,010 packets**: the 18,000 flood SYNs plus **10** more, which I read as the real client's 5 SYNs and 5 ACKs (the script does not break them down). The firewall pays for it: it now computes a cookie for **every** SYN and holds **no state** for the flood, which is the right place to pay on a box built for packets, and the wrong place on a small one.

| Defence | Where the state goes | What it costs |
|---|---|---|
| nothing | a half-open record on the server | queue full: **0 of 5** connections |
| SYN cookies on the server | **into the packet** (a hash the client echoes) | CPU per SYN, some TCP options lost, every server needs them |
| SYN proxy in the firewall | **into the firewall's cookie**, no entry anywhere | CPU on the firewall per SYN, a rule per protected service |
| `notrack` alone | no table entry on the firewall | does not protect the server's queue |

### 10.5 How slow can the flood be?

The attack in 10.2 used 2,000 SYN per second. `e13_synrate.sh` repeats it at four rates, with cookies **off**, and counts the half-open entries after 8 s:

```text
  2 SYN per second: half-open entries after 8 s: 16   real client:  1 of 1 connections worked, median 1.6 ms, slowest 2 ms
  5 SYN per second: half-open entries after 8 s: 17   real client:  0 of 1 connections worked
 10 SYN per second: half-open entries after 8 s: 17   real client:  0 of 1 connections worked
 50 SYN per second: half-open entries after 8 s: 17   real client:  0 of 1 connections worked
```

**2 SYN per second** put **16** entries in a queue of 16 after 8 s (2 x 8 = 16: nothing had expired yet), and a real client **got through**, since there was one slot of room. **5 per second and above** filled it, with **17** entries, and the real client got **0 of 1**. For a backlog `B`, a flood at rate `r` fills the queue in `B / r` seconds, and then keeps it full as long as entries die no faster than `r` per second. That depends on the **entry lifetime**, which this run did not measure. By the default retransmission schedule (the SYN-ACK is sent again at 1, 3, 7, 15 and 31 s, and the entry is dropped at **63 s**, from memory), the rate that **holds** a queue full is **backlog / 63 s**. `ans_e.sh` (Problem E) tabulates it:

```text
part 1: the queue of 16 in e13, no entries expire inside the 8 s window (an entry lives much longer)
SYN/s      entries after 8 s      seconds to fill 16       measured in e13
2          16                     8.0                      16, real client got through
5          16                     3.2                      17, queue full
10         16                     1.6                      17, queue full
50         16                     0.3                      17, queue full

part 2: the rate that keeps a queue full for good = backlog / entry lifetime
entry lifetime with 5 SYN-ACK retries: 63 s (from memory, not measured here)
backlog  lifetime   min SYN/s    bandwidth, 84 bytes on the wire per SYN
16       63         0.3          0.000 Mbit/s
128      63         2.0          0.001 Mbit/s
4096     63         65.0         0.044 Mbit/s
65535    63         1040.2       0.699 Mbit/s
4096     3          1365.3       0.918 Mbit/s
```

A backlog of **4,096** is held full by **65 SYN per second**, about **0.04 Mbit/s** of traffic. A **tiny** flood in bandwidth, invisible to a monitor that watches bandwidth, takes the service down: **state attacks are cheap**, and defences that work by rate of **bytes** miss them. Shortening the lifetime (the same table at **3 s**, the last row) raises the cost to the attacker **21 times**, to 1,365 SYN per second, which is why a server under attack lowers its SYN-ACK retries, and why cookies, which make the lifetime **zero**, are the real answer.

### 10.6 Other floods (from memory)

- **Volumetric floods and reflection.** Nothing on the victim's side of the access link helps once the link is full. The defence is **upstream**: the provider **drops** traffic to the victim's address (remotely triggered black hole) or **scrubs** it in a large network of anycast sites that absorb the attack, filter it, and send the clean traffic on. **Ingress filtering** (BCP 38: a network drops packets whose source address does not belong behind the interface) stops spoofing at the source, and is the reason reflection needs unfiltered networks.
- **Application layer floods** (many valid HTTP requests, an expensive search, a login attempt) cannot be told from users by packet fields. The defences are **per-source and per-session rate limits**, a **challenge** that a bot fails, and **caching** at the edge. This is where the limits of section 11 are used against clients.
- **What the firewall can do alone:** drop at the **earliest hook** (the `raw` priority, before conntrack, as the SYN proxy case did), keep **the allow list short** (a set, section 1.2), shorten timeouts under load (section 2.3), and **fail visibly**: a counter that moves tells you what is under attack.

---


## 11. Protecting the firewall itself: policing the control plane

A firewall that forwards at line rate can still be taken down by traffic **addressed to it**: pings, SSH logins, routing protocol packets, management requests, ARP and neighbour discovery. These reach the firewall's **CPU** (the **control plane**) instead of passing through the fast **data plane**, and the CPU is small. Section 1 named the `input` hook for exactly this traffic. The defence is the same everywhere in networking and has a standard name, **control plane policing (CoPP)** on Cisco devices: **classify** the traffic to the control plane, and **limit each class** to a rate the CPU can serve. The pairing with the data plane and control plane split is in [the data plane against the control plane](../11_hld/03_systems_domain/01_data_plane_vs_control_plane.md).

### 11.1 The token bucket

The limiter is a **token bucket**: a bucket holds up to `burst` tokens and gains `rate` tokens a second. Each packet takes one token and passes; a packet that finds the bucket empty is dropped (a **policer**) or delayed (a **shaper**). The two numbers do different jobs: `rate` is the **long run average**, and `burst` is how many packets may arrive **at once** before the limit bites.

`e11_ratelimit.sh` sends 200 pings to the firewall's own address, 100 a second (so the test runs about 3 s). First with **no limit**, then with **one bucket for all echo requests**, **20 a second with a burst of 10**:

```bash
nse fw nft -f - << 'RULES'
table inet f {
  chain in {
    type filter hook input priority 0;
    icmp type echo-request limit rate 20/second burst 10 packets accept
    icmp type echo-request counter drop
  }
}
RULES
```

```text
--- no limit: 200 echo requests at 100 per second to the firewall's own address
  from 10.1.0.2: 200 packets transmitted, 200 received, time 3187ms
--- one token bucket for all echo requests: 20 per second, burst of 10
  from 10.1.0.2: 200 packets transmitted, 73 received, time 3188ms
(expected: the burst of 10 plus 20 for every second the test ran)
```

With no limit all **200** were answered. With the bucket, **73** were: **10** from the initial burst, plus **20 for each second of the test**: `10 + 20 x 3.188 s` = **73.8**. The measurement matches the model to the packet, which is the check that the limiter does what the formula says. The 127 dropped pings are the policy working.

### 11.2 The limit must be per source

One bucket for the whole class has a flaw: **one noisy sender can use all of it, and starve everyone else**. An attacker (or a misconfigured monitor) pinging at 100 a second takes the whole 20 a second, and a legitimate sender finds the bucket empty. The fix is a **bucket per source address**, kept in a dynamic set:

```bash
nse fw nft -f - << 'RULES'
table inet f {
  set senders {
    type ipv4_addr
    flags dynamic
    timeout 1m
    size 65535
  }
  chain in {
    type filter hook input priority 0;
    icmp type echo-request add @senders { ip saddr limit rate over 20/second burst 10 packets } counter drop
  }
}
RULES
```

```text
--- a bucket per source address: the same limit, kept separately for every sender
  from 10.1.0.3: 5 packets transmitted, 5 received, time 818ms
  from 10.1.0.2: 200 packets transmitted, 73 received, time 3190ms
```

The noisy sender, 10.1.0.2, got the same **73 of 200** as before. A second sender, 10.1.0.3, sending **5 pings** at the same time, got **5 of 5**: its bucket was its own. The set has a **size** (65,535 entries) and a **timeout** (1 minute), because per-source state is state, and the table of section 3 applies to it as well: an attacker with a million spoofed addresses can fill a per-source table, and the defence is a **global** limit above the per-source one.

### 11.3 Limit what a source may hold open

Rate is one dimension. **Concurrency** is the other: a source that opens 10,000 connections and holds them takes a server's accept queue and a firewall's table without ever exceeding a rate. `ct count over 3` counts the **concurrent tracked connections** of one source address:

```bash
nse fw nft -f - << 'RULES'
table inet f {
  set conns {
    type ipv4_addr
    flags dynamic
    size 65535
  }
  chain in {
    type filter hook input priority 0;
    tcp dport 9000 ct state new add @conns { ip saddr ct count over 3 } counter reject with tcp reset
  }
}
RULES
```

```text
--- a limit on concurrent connections per source: at most 3 open connections to port 9000
10 connection attempts from 10.1.0.2: 3 accepted, 7 refused
tcp dport 9000 ct state new add @conns { ip saddr ct count over 3 } counter packets 7 bytes 420 reject with tcp reset
```

Ten connection attempts from one address: **3 accepted, 7 refused** with a `RST` (the counter shows the 7). The limit is on **open** connections, so closing one frees a slot, and the number is chosen from what a legitimate client needs. The caveat of section 3.5 applies: behind a NAT gateway **many users share one source address**, and a per-source limit sized for one user starves the rest. A gateway needs a higher limit, or a limit keyed on something finer than the address (an authenticated identity, a session cookie at layer 7).

### 11.4 What real boxes do (from memory)

- **Hardware CoPP.** Network devices classify control plane traffic in the **ASIC** (an ACL per class: routing protocols, management, ARP, everything else) and apply a **hardware policer** per class **before** the packet reaches the CPU, so that a flood of one class cannot starve another. The principle is the same as `limit rate` in nftables, with the limit enforced at line rate in silicon.
- **Priority.** The protocols that keep the network up (BGP, OSPF, BFD, LACP) get a **guaranteed** share, **management** a smaller one, and **unclassified** traffic the least. A box that policed everything with one limit would drop its own routing hellos in an ICMP flood, and a network that loses its routing protocol during an attack is a network that has lost the argument.
- **Separate planes.** A **dedicated management port** on a separate network, with its own policy, keeps administration reachable when the data ports are saturated.

---


## 12. Common mistakes

1. **Writing a stateless "reply" rule.** "Accept inbound packets with ACK set" lets a lone ACK reach the client, which answered with a `RST` (section 2.1), and "ACK set and SYN clear" **blocked the SYN-ACK** and broke the connection it was meant to allow. Use `ct state established,related accept` and let the table decide what a reply is.
2. **Putting a rate limit below `ct state established accept`.** My first `ans_a.sh` limited echo requests **after** the established rule, and **all 40 of 40** pings passed: the pings of one burst share an ICMP id, so after the first reply the flow was `established` and later requests never reached the limiter (Problem A). Put the limit **above** the established rule, and give the excess its own `drop`.
3. **Naming an nft chain or table with a reserved word.** `chain fwd {` and `table inet policy {` are syntax errors in nftables (both words are keywords), the ruleset **did not load**, and the checks that depended on it failed while the others passed. A script that does not check the exit status of `nft -f` can run with **no policy at all**. Always check it, and print the loaded ruleset.
4. **A long list where a set would do.** About **10 ns per rule** that does not match, so 5,000 rules took the rate from 767 k to **19 k** packets/s, while the same 5,000 addresses in a set cost nothing (section 1.2). Group by set, order by hit rate.
5. **Counting entries and forgetting the buckets and the timeouts.** The table size is `rate x lifetime x 256 bytes` (**1.54 GB** for 6 million entries), and **28.6 entries per bucket** at 7.5 million with this kernel's 262,144 buckets. The default `tcp_established` of **5 days** on a NAT gateway keeps entries for clients that vanished (sections 2.3 and 3.1).
6. **Believing early drop protects the table.** With a table of 1,000 and a flood of one-way flows, only **3 of 20** legitimate flows got in, and with **1,000 assured** flows, **0 of 20** (section 3.4). Early drop helps only against **unreplied** entries. Per-source limits, `notrack` and capacity are the defences.
7. **Trusting a per-packet string match.** `iptables -m string "/etc/passwd"` dropped the single segment and **missed the same request split in two** (section 6.3). Match on the **reassembled stream**, in the way the end host reads it.
8. **Not deciding fail-open or fail-closed.** With nobody on the queue, plain `NFQUEUE` **dropped every packet** and `--queue-bypass` **let every packet through uninspected** (section 6.4). Make the choice per link, write it down, and test the crash.
9. **Quoting an inline rate as the product's.** **16.35 to 1.04 Gbit/s** was measured with the generator on the same two vCPUs and one worker; it is a ratio of 15.7 for this machine, and says nothing about Suricata on real hardware (section 6.5).
10. **Letting a port number name the application.** **4 of 8** flows used a port that did not match their content (section 5.1). The same trap hides in tools: `tshark` decodes by port, so it found **2 of the 4** ClientHellos in `e5_appid.sh` and missed the TLS on ports 80 and 8080 (section 5.2).
11. **Decrypting everything.** Pinned apps **failed** (`public key does not match pinned public key`), mutual TLS **failed**, and every unmanaged device got a certificate error (section 8.2). Keep a bypass list, and keep the CA key in an HSM.
12. **Running untrusted input through a backtracking regular expression.** `^(a+)+$` took **2.19 s at 27 characters** and doubles with each added one, against **0.13 ms** in RE2 (section 7.2). Use an automaton engine, or a step limit.
13. **Watching bandwidth to detect a SYN flood.** A backlog of **4,096** is held full by **65 SYN per second**, about **0.04 Mbit/s** (section 10.5). Watch **half-open entries**, `ListenDrops` and connection failures.
14. **Forgetting the MSS clamp on a tunnel.** ESP in tunnel mode takes **56 bytes**, so a 1,500 byte link carries **1,446** bytes of inner packet and TCP needs an MSS of **1,406**. Without the clamp, large packets fragment or vanish where ICMP is filtered (section 9.2).

---

## 13. Twelve interview questions

1. **What is the difference between a stateless and a stateful firewall?** Stateless decides from one packet's fields, so a reply is only a packet that looks like one: "ACK set" let a lone ACK through, and the stricter rule blocked the SYN-ACK. Stateful keeps a **table of connections** keyed by the 5-tuple, both directions (a **symmetric key**: smaller endpoint first), follows the TCP state machine and window, and treats what belongs to an entry differently from a new flow. Measured: the lone ACK got **no answer** and an out-of-window ACK was counted `invalid`.
2. **What happens when the connection table is full?** New flows are dropped (`table full, dropping packet`). The kernel tries **early drop** of an unreplied entry, and measured **3 of 20** legitimate flows got in against a flood of one-way flows, and **0 of 20** against **assured** entries. The levers: a bigger table and bucket array, shorter timeouts, `notrack` for traffic that needs no state, per-source limits, cookies and proxies for SYNs, and a cluster.
3. **Size a connection table for 200,000 new connections a second.** `entries = rate x lifetime`: with a 30 s lifetime, **6 million**; at **256 bytes** an entry, **1.54 GB**; with 25 percent headroom **7.5 million**, **1.92 GB**, and **1.875 million buckets** (a mean chain of 4, **15 MB** of bucket array). State that the lifetime is set by the timeouts, which an attacker can stretch, and that lookups cost **104 ns** at one entry per bucket against **12,112 ns** at 244 (measured).
4. **Why is a long ACL slow and what is the fix?** A list is walked in order: **10 ns** per non-matching rule, so 5,000 rules cost **52.6 microseconds** a packet against **1.3**. Fixes: **sets or tries** (flat: one rule with 5,000 addresses was as fast as one rule), **order by hit rate** with `established` first, **decide once per flow** (the connection table, a flowtable), and **hardware** (a TCAM matches all rules at once, at a limit on the entry count).
5. **How does NAT work in a firewall and what does it break?** A conntrack entry holds **two tuples**, original and reply, and NAT makes them differ. A port collision is resolved by picking another port (**64907** for the second client). It breaks protocols that carry an address in the payload (FTP active mode: **timeout** without the helper), which needs an **ALG** that rewrites the text and creates an **expectation** for the data connection. State the costs: port exhaustion, a parser in the data path, no inbound connections.
6. **What is the difference between an IDS and an IPS?** Position. An IDS reads a **copy** and alerts (**allowed** on the same packets, measured); an IPS is **in the path** and can drop. The IPS adds latency (median **502 to 744 microseconds**), a throughput ceiling (**16.35 to 1.04 Gbit/s** on this small VM), and a failure mode (**fail-closed**: all dropped; **fail-open**: all uninspected). False positives cost an outage, not a log line.
7. **How does an IPS handle evasion by segmentation?** It **reassembles the TCP stream** (and IP fragments) before matching, **in the way the destination will**. Measured: the string split across two segments passed a per-packet match and was **dropped by Suricata**. The cost is per-flow state, which is bounded by a memory cap and a reassembly depth, and is itself an attack surface.
8. **How does Aho-Corasick work and when is it the wrong tool?** A **trie** of the patterns plus **failure links** (the longest proper suffix that is also a prefix), so the text is read **once**, one table step per byte, whatever the number of patterns. Measured: **22 times** faster than one `memmem` per pattern at 10,000 patterns, but **slower** at 1 and 10 (crossover near 16), and its scan slowed from **0.40 to 0.02 GB/s** as the table outgrew the caches. Real engines compress the table and use SIMD.
9. **What is ReDoS and how do you prevent it?** A backtracking regular expression engine takes **exponential** time on a crafted input: `^(a+)+$` doubled for each added character and took **2.19 s** at 27. An automaton engine (RE2, Hyperscan) is linear: **0.13 ms**, and a million characters in **1.6 ms**. Use it for untrusted input, or set a step limit, and review nested quantifiers.
10. **How do you inspect TLS traffic, and what are the limits?** Without decrypting: the **name** (SNI), the **JA3** fingerprint, and from TLS 1.2 the certificate. TLS 1.3 hid the certificate (measured: 2 handshake messages visible against 6 in TLS 1.2) and ECH would hide the name (from memory). With a **forward proxy**: forge a certificate per name from a CA the clients trust. It breaks **pinning**, **mutual TLS** and unmanaged devices, costs a second handshake (**1.85 to 4.18 ms**), and concentrates risk in the CA key. Alternatives: endpoint inspection, key logging for debugging.
11. **How do you defend against a SYN flood?** The queue fills: **0 of 5** real connections under **2,000 SYN/s**. **SYN cookies** keep the state in the sequence number (**17,979** cookies sent, **5 of 5** real clients served, some options lost). A **SYN proxy** in the firewall answers with cookies and opens the server's connection only after a real ACK (**0** half-open on the server). And note the flood is cheap: **65 SYN/s** holds a 4,096 queue, so watch queues, not bandwidth.
12. **Design the data path of a 100 Gbit/s stateful firewall.** Split into a **fast path** and a **slow path**. The first packet of a flow goes to the slow path (policy lookup in a set or trie, NAT choice, app-ID on the first payload), which **installs a flow entry** in a hash table keyed by a **symmetric** 5-tuple; later packets hit the entry and are handled without the policy. Spread flows across cores by a **symmetric RSS hash**, so both directions of one flow meet on one core without sharing the entry. Size memory for entries (256 bytes each), expire by timer wheel, protect the box with **CoPP** (token buckets per class) and handle floods with cookies and a per-source limit. Inspection (IPS, decryption) is a separate, slower, **opt-in** path for the flows that need it. This is the shape of the design in [the data plane against the control plane](../11_hld/03_systems_domain/01_data_plane_vs_control_plane.md) and of HLD design problem 18 (SSL decryption and NAT).

---


## 14. Problems

Each problem says where to solve it and how to approach it. Problem A needs a Linux machine with `nftables`, root, and the three-namespace helper `prog/topo.sh` (a VM or a container with `NET_ADMIN` works); the others run anywhere with Python 3 or a C compiler (Problem D also uses `pyahocorasick`, `pip install pyahocorasick`). Predict before you run.

**Problem A. Write the forward policy.** In the lab (client 10.1.0.2, server 10.2.0.2, firewall between them) write an `nft` ruleset for the forward hook with **default drop** that: lets the client open TCP connections to the server on ports **80 and 443**, and send UDP to **53**, with the replies passing; **blocks everything else**, including TCP **22** from the client and any connection the **server** starts towards the client; counts and drops packets in `ct state invalid`; and limits echo requests from the client to **10 per second with a burst of 5**, so that **40 pings sent in 0.8 s** let between 5 and 15 through. Check it with six tests. **Where:** the lab, as root (`. prog/topo.sh; topo_up`, then `nse fw nft -f -`). **Approach:** write the rules in the order the packet meets them; decide first what must be above `ct state established,related accept` and what must be below; avoid the words `fwd` and `policy` as names; check the exit status of `nft -f` and list the loaded ruleset before testing.

**Problem B. Size a connection table.** Three cases: a web edge with **200,000** new connections a second living **30 s**; a campus NAT with **20,000** a second living **300 s**; a DNS front with **100,000** a second living **30 s**. Compute the entries and the memory at **256 bytes** an entry. Then, for the web edge with **25 percent headroom**, compute the limit, the memory, the average chain length if the hash table keeps **262,144** buckets, and the bucket count for an average chain of **4** with the memory of that bucket array at **8 bytes** a bucket. **Where:** paper first, then Python. **Approach:** the number of entries in a system is the arrival rate times the time each one stays; the memory is a product; the chain length is entries divided by buckets.

**Problem C. Build the flow table.** Write `flowtab.c`: a hash table with chaining, keyed by the **5-tuple** with a **symmetric** key (the smaller endpoint first), functions `lookup`, `insert` (adds a flow, or refreshes and counts a packet on one that exists) and `expire` (removes entries not seen for longer than a timeout), and a main that prints **nine checks**: the keys and hashes of A to B and B to A are equal; a reverse lookup finds the flow; a different source port and a different protocol are different flows; a reverse packet updates the same entry (one flow, two packets); **100,000** flows in **1,024** buckets are all found; a sweep at time 130 with a timeout of 25 removes the 100,000 old flows (last seen at 100) and keeps one refreshed at 105. Then benchmark 4 million reverse-direction lookups over 1 million flows at 2^20, 2^16 and 2^12 buckets. **Where:** any machine with `gcc` or `clang`. **Approach:** write `make_key` first and test it alone; the hash must read only the canonical key; `expire` is a loop over the buckets with a pointer to the pointer; predict the three lookup times from the mean chain length (1, 15, 244) before you run.

**Problem D. Aho-Corasick by hand.** Build the automaton for **he, she, his, hers**: the trie (number the states in the order you create them), the failure link of each state, and the output set of each state (with the outputs of its failure link added). Run it over **ushers** and list the matches with the position of their last letter. Check against `pyahocorasick`. **Where:** paper first, then Python. **Approach:** build the trie by inserting the words one letter at a time; compute the failure links in breadth first order, each from its parent's failure link (`fail[child] = goto(fail[parent], letter)`, trying shorter suffixes until an edge exists); merge outputs along the failure links; on a missing edge follow the failure link and try again.

**Problem E. How fast does a SYN flood fill a queue?** A listen backlog of **16** is flooded at **2, 5, 10 and 50** SYN per second with no entry expiring within the 8 s of the test: compute the entries after 8 s and the time to fill, and say which rates fill the queue in the 8 s. Then compute the rate that **holds** a queue full for good, for backlogs **16, 128, 4,096 and 65,535**, if an entry lives **63 s**, and for **4,096** with a lifetime of **3 s**, with the bandwidth at **84 bytes** on the wire per SYN. Compare part 1 with `v/e13_synrate.out`. **Where:** Python. **Approach:** the time to fill is `backlog / rate`; the holding rate is `backlog / lifetime`; bandwidth is `rate x 84 x 8` bits.

---


## 15. Answers

### Problem A

```bash
# Problem A: a forward policy for the three-namespace lab, with six checks
. prog/topo.sh
trap topo_down EXIT
topo_up
nse s python3 prog/srv_tcp.py 10.2.0.2 80 &
nse s python3 prog/srv_tcp.py 10.2.0.2 443 &
nse s python3 prog/srv_tcp.py 10.2.0.2 22 &
nse s python3 prog/srv_udp.py 10.2.0.2 53 &
nse c python3 prog/srv_tcp.py 10.1.0.2 9000 &
waitlisten s 3; waitlisten c 1
nse fw nft -f - << 'RULES'
table inet lab {
  chain acl {
    type filter hook forward priority 0; policy drop;
    ct state invalid counter drop
    iifname "fc" icmp type echo-request limit rate 10/second burst 5 packets accept
    iifname "fc" icmp type echo-request drop
    ct state established,related accept
    iifname "fc" ip daddr 10.2.0.2 tcp dport { 80, 443 } ct state new accept
    iifname "fc" ip daddr 10.2.0.2 udp dport 53 ct state new accept
  }
}
RULES
pass=0; total=0
check() { # name, expected, got
  total=$((total + 1)); local ok=FAIL; [ "$2" = "$3" ] && { ok=ok; pass=$((pass + 1)); }
  printf '%-44s expected %-8s got %-8s %s\n' "$1" "$2" "$3" "$ok"
}
tcp() { nse "$1" python3 -c "
import socket, sys
s = socket.socket(); s.settimeout(1.5)
try: s.connect(('$2', $3)); print('connects')
except OSError: print('blocked')"; }
check "client to server TCP 80" connects "$(tcp c 10.2.0.2 80)"
check "client to server TCP 443" connects "$(tcp c 10.2.0.2 443)"
check "client to server TCP 22" blocked "$(tcp c 10.2.0.2 22)"
check "client to server UDP 53" "1 of 1" "$(nse c python3 prog/flows.py 10.2.0.2 53 1 1 | sed 's/ flows.*//')"
check "server to client TCP 9000 (server initiates)" blocked "$(tcp s 10.1.0.2 9000)"
got=$(nse c ping -q -I 10.1.0.2 -c 40 -i 0.02 -W 1 10.2.0.2 2> /dev/null | grep -oE "[0-9]+ received" | cut -d' ' -f1)
check "40 pings in 0.8 s: at most 5 + 10 get through" yes "$([ "$got" -le 15 ] && [ "$got" -ge 5 ] && echo yes || echo "no ($got)")"
echo "$pass/$total checks passed"
```

```text
client to server TCP 80                      expected connects got connects ok
client to server TCP 443                     expected connects got connects ok
client to server TCP 22                      expected blocked  got blocked  ok
client to server UDP 53                      expected 1 of 1   got 1 of 1   ok
server to client TCP 9000 (server initiates) expected blocked  got blocked  ok
40 pings in 0.8 s: at most 5 + 10 get through expected yes      got yes      ok
6/6 checks passed
```

**All six checks pass.** The order of the chain is the answer. `ct state invalid counter drop` comes first, so a malformed packet is counted and gone. The **echo limiter and its drop come next**, **above** `ct state established,related accept`: the first version of this ruleset had the limiter at the bottom, and the same burst of 40 pings passed **40 of 40**, because the burst shares one ICMP id, and after the first reply the flow was `established` and matched the accept rule on the way down. The two echo rules cannot share a rule with the stateful accept: the limit must see the packet first. Only then do the established and related replies pass, and the three allow rules match **new** flows from the client interface to the server on exact ports. Everything else meets the chain's `policy drop`: TCP 22, and the server's own connection to port 9000 on the client. The first loading attempt also taught the third mistake of section 12: a chain named `fwd` is a syntax error, so the ruleset did not load and only three of the six checks passed.

### Problem B

```bash
# Problem B: size a conntrack table from rate, lifetime and entry size
obj=$(grep -E "^nf_conntrack " /proc/slabinfo | awk '{ print $4 }')
buckets=$(cat /sys/module/nf_conntrack/parameters/hashsize)
max=$(cat /proc/sys/net/netfilter/nf_conntrack_max)
echo "this kernel: nf_conntrack_max $max, hash buckets $buckets, entry $obj bytes"
python3 - "$obj" "$buckets" << 'PY'
import sys
obj, buckets = int(sys.argv[1]), int(sys.argv[2])
cases = [
    ("web edge, short flows", 200_000, 30),
    ("campus NAT, long flows", 20_000, 300),
    ("DNS resolver front, UDP", 100_000, 30),
]
print("%-26s %9s %9s %10s %9s" % ("case", "new/s", "life s", "entries", "memory"))
for name, rate, life in cases:
    n = rate * life
    mem = n * obj
    print("%-26s %9d %9d %10d %7.2f GB" % (name, rate, life, n, mem / 1e9))
rate, life = 200_000, 30
n = rate * life
need = int(n * 1.25)
print("web edge with 25 percent headroom: max %d, memory %.2f GB" % (need, need * obj / 1e9))
print("average chain length at max if buckets stay at this kernel's value: %.1f" % (need / buckets))
want = need // 4
print("buckets for an average chain of 4: %d, bucket array %.1f MB (8 bytes each)" % (want, want * 8 / 1e6))
PY
```

```text
this kernel: nf_conntrack_max 262144, hash buckets 262144, entry 256 bytes
case                           new/s    life s    entries    memory
web edge, short flows         200000        30    6000000    1.54 GB
campus NAT, long flows         20000       300    6000000    1.54 GB
DNS resolver front, UDP       100000        30    3000000    0.77 GB
web edge with 25 percent headroom: max 7500000, memory 1.92 GB
average chain length at max if buckets stay at this kernel's value: 28.6
buckets for an average chain of 4: 1875000, bucket array 15.0 MB (8 bytes each)
```

The web edge and the campus NAT both need **6 million entries** (`200,000 x 30` and `20,000 x 300`) and **1.54 GB** (`6,000,000 x 256`); the DNS front needs **3 million**, **0.77 GB**. Two very different networks ask for the same table, and the way to cut it is the lifetime (a shorter timeout for idle flows) as much as the capacity. With 25 percent headroom the web edge needs a limit of **7.5 million** and **1.92 GB**. At the kernel's 262,144 buckets that is a mean chain of **28.6**, and to get a mean of 4 the table needs **1,875,000 buckets**, whose array is only **15 MB** (`1,875,000 x 8`): the buckets are cheap, and the lookups they save (section 3.3: **104 ns against 639 ns against 12,112 ns**) are the point.

### Problem C

```c
/* flowtab.c: a 5-tuple flow table with a symmetric hash, expiry and a lookup benchmark. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

typedef struct { uint32_t ip_lo, ip_hi; uint16_t port_lo, port_hi; uint8_t proto; } Key;
typedef struct Flow { Key k; uint32_t last_seen; uint64_t pkts; struct Flow *next; } Flow;
typedef struct { Flow **b; uint32_t nb; uint32_t count; } Table;

/* Put the smaller (ip, port) endpoint first, so A->B and B->A give the same key. */
static Key make_key(uint32_t sip, uint16_t sp, uint32_t dip, uint16_t dp, uint8_t proto) {
  Key k = {0};
  int swap = sip > dip || (sip == dip && sp > dp);
  k.ip_lo = swap ? dip : sip;  k.ip_hi = swap ? sip : dip;
  k.port_lo = swap ? dp : sp;  k.port_hi = swap ? sp : dp;
  k.proto = proto;
  return k;
}

static uint32_t hash_key(const Key *k) {
  uint64_t h = 1469598103934665603ULL;
  uint64_t parts[3] = { ((uint64_t)k->ip_lo << 32) | k->ip_hi,
                        ((uint64_t)k->port_lo << 16) | k->port_hi, k->proto };
  for (int i = 0; i < 3; i++) { h ^= parts[i]; h *= 1099511628211ULL; h ^= h >> 29; }
  return (uint32_t)h;
}

static Flow *lookup(Table *t, const Key *k) {
  for (Flow *f = t->b[hash_key(k) % t->nb]; f; f = f->next)
    if (!memcmp(&f->k, k, sizeof *k)) return f;
  return NULL;
}

static Flow *insert(Table *t, const Key *k, uint32_t now) {
  Flow *f = lookup(t, k);
  if (f) { f->last_seen = now; f->pkts++; return f; }
  f = calloc(1, sizeof *f);
  f->k = *k; f->last_seen = now; f->pkts = 1;
  uint32_t i = hash_key(k) % t->nb;
  f->next = t->b[i]; t->b[i] = f; t->count++;
  return f;
}

static uint32_t expire(Table *t, uint32_t now, uint32_t timeout) {
  uint32_t removed = 0;
  for (uint32_t i = 0; i < t->nb; i++) {
    Flow **pp = &t->b[i];
    while (*pp) {
      if (now - (*pp)->last_seen > timeout) { Flow *d = *pp; *pp = d->next; free(d); removed++; t->count--; }
      else pp = &(*pp)->next;
    }
  }
  return removed;
}

static Table *table_new(uint32_t nb) {
  Table *t = calloc(1, sizeof *t);
  t->nb = nb; t->b = calloc(nb, sizeof(Flow *));
  return t;
}

static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec * 1e-6; }

static int pass, total;
static void check(const char *name, int ok) { total++; pass += ok; printf("%-58s %s\n", name, ok ? "ok" : "FAIL"); }

static void bench(uint32_t nflows, uint32_t nb) {
  Table *t = table_new(nb);
  for (uint32_t i = 0; i < nflows; i++) {
    Key k = make_key(0x0a010000 + (i >> 12), 1024 + (i & 4095), 0x0a020002, 443, 6);
    insert(t, &k, 0);
  }
  uint64_t x = 88172645463325252ULL; uint32_t found = 0, lookups = 4000000;
  double t0 = now_ms();
  for (uint32_t n = 0; n < lookups; n++) {
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    uint32_t i = x % nflows;
    Key k = make_key(0x0a020002, 443, 0x0a010000 + (i >> 12), 1024 + (i & 4095), 6);   /* reverse direction */
    found += lookup(t, &k) != NULL;
  }
  double ms = now_ms() - t0;
  printf("%8u flows, %8u buckets (%5.1f per bucket): %4.0f ns per lookup, %u of %u found\n",
         nflows, nb, (double)nflows / nb, ms * 1e6 / lookups, found, lookups);
}

int main(void) {
  Table *t = table_new(1024);
  Key fwd = make_key(0x0a010002, 40000, 0x0a020002, 443, 6);
  Key rev = make_key(0x0a020002, 443, 0x0a010002, 40000, 6);
  check("forward and reverse give the same key", !memcmp(&fwd, &rev, sizeof fwd));
  check("same hash in both directions", hash_key(&fwd) == hash_key(&rev));
  insert(t, &fwd, 100);
  check("lookup by the reverse direction finds the flow", lookup(t, &rev) != NULL);
  Key other = make_key(0x0a010002, 40001, 0x0a020002, 443, 6);
  check("a different source port is a different flow", lookup(t, &other) == NULL);
  Key udp = make_key(0x0a010002, 40000, 0x0a020002, 443, 17);
  check("the same ports over UDP is a different flow", lookup(t, &udp) == NULL);
  insert(t, &rev, 105);
  check("a reverse packet updates the same entry", t->count == 1 && lookup(t, &fwd)->pkts == 2);
  for (int i = 0; i < 100000; i++) { Key k = make_key(0x0a010000 + i, 5000, 0x0a020002, 80, 6); insert(t, &k, 100); }
  int all = 1;
  for (int i = 0; i < 100000; i++) { Key k = make_key(0x0a010000 + i, 5000, 0x0a020002, 80, 6); all &= lookup(t, &k) != NULL; }
  check("100000 flows in 1024 buckets are all found", all && t->count == 100001);
  uint32_t gone = expire(t, 130, 25);
  check("sweep at t=130, timeout 25 removes the 100000 old flows", gone == 100000 && t->count == 1);
  check("the flow refreshed at t=105 survives", lookup(t, &fwd) != NULL);
  printf("%d/%d checks passed\n", pass, total);
  bench(1000000, 1 << 20);
  bench(1000000, 1 << 16);
  bench(1000000, 1 << 12);
  return 0;
}
```

```bash
# Problem C: build and run the flow table, checks and benchmark
gcc -O2 -Wall -o prog/flowtab prog/flowtab.c && ./prog/flowtab
```

```text
forward and reverse give the same key                      ok
same hash in both directions                               ok
lookup by the reverse direction finds the flow             ok
a different source port is a different flow                ok
the same ports over UDP is a different flow                ok
a reverse packet updates the same entry                    ok
100000 flows in 1024 buckets are all found                 ok
sweep at t=130, timeout 25 removes the 100000 old flows    ok
the flow refreshed at t=105 survives                       ok
9/9 checks passed
 1000000 flows,  1048576 buckets (  1.0 per bucket):  104 ns per lookup, 4000000 of 4000000 found
 1000000 flows,    65536 buckets ( 15.3 per bucket):  639 ns per lookup, 4000000 of 4000000 found
 1000000 flows,     4096 buckets (244.1 per bucket): 12112 ns per lookup, 4000000 of 4000000 found
```

Nine of nine pass. The symmetric key is `make_key`: it compares the two endpoints and puts the smaller first, and the hash reads only the canonical key, so a packet and its reply hash alike (checks 1 to 3). Checks 4 and 5 show that the port and the protocol are part of the key. Check 6 is the reason a flow table counts packets per flow and not per direction: the reverse packet found the same entry. `expire` walks every bucket with a pointer to the previous link (`Flow **pp`), so it can unlink the head and the middle of a chain with the same code. The first version of check 8 failed: it used a timeout of 30 at time 130 for entries last seen at 100, and `now - last_seen > timeout` is **false** for exactly 30. That is a boundary decision, and a real table needs a timer wheel in place of a full sweep for the same reason: a sweep of a billion entries cannot run in the data path. The benchmark: **104 ns** at 1.0 per bucket, **639 ns** at 15.3 (6 times), **12,112 ns** at 244 (116 times), and the cost follows the chain length because each hop is a cache miss.

### Problem D

```python
# Problem D: Aho-Corasick for he, she, his, hers on "ushers", built by hand and checked against pyahocorasick
import ahocorasick
from collections import deque

words = ["he", "she", "his", "hers"]
goto, out, fail = [{}], [set()], [0]
for w in words:
    s = 0
    for c in w:
        if c not in goto[s]:
            goto.append({}); out.append(set()); fail.append(0)
            goto[s][c] = len(goto) - 1
        s = goto[s][c]
    out[s].add(w)
q = deque(goto[0].values())
while q:
    r = q.popleft()
    for c, s in goto[r].items():
        q.append(s)
        f = fail[r]
        while f and c not in goto[f]:
            f = fail[f]
        fail[s] = goto[f].get(c, 0)
        out[s] |= out[fail[s]]

print("states:", len(goto))
for i, g in enumerate(goto):
    print("  state %d  goto %-18s fail %d  output %s" % (i, dict(g), fail[i], sorted(out[i])))

text = "ushers"
s, mine = 0, []
for i, c in enumerate(text):
    while s and c not in goto[s]:
        s = fail[s]
    s = goto[s].get(c, 0)
    for w in sorted(out[s]):
        mine.append((i, w))
print("by hand:       ", mine)

A = ahocorasick.Automaton()
for w in words:
    A.add_word(w, w)
A.make_automaton()
ref = sorted((i, w) for i, w in A.iter(text))
print("pyahocorasick: ", ref)
print("same matches:", sorted(mine) == ref)
```

```text
states: 10
  state 0  goto {'h': 1, 's': 3}   fail 0  output []
  state 1  goto {'e': 2, 'i': 6}   fail 0  output []
  state 2  goto {'r': 8}           fail 0  output ['he']
  state 3  goto {'h': 4}           fail 0  output []
  state 4  goto {'e': 5}           fail 1  output []
  state 5  goto {}                 fail 2  output ['he', 'she']
  state 6  goto {'s': 7}           fail 0  output []
  state 7  goto {}                 fail 3  output ['his']
  state 8  goto {'s': 9}           fail 0  output []
  state 9  goto {}                 fail 3  output ['hers']
by hand:        [(3, 'he'), (3, 'she'), (5, 'hers')]
pyahocorasick:  [(3, 'he'), (3, 'she'), (5, 'hers')]
same matches: True
```

The trie has **10 states** (the root and one per new letter: `h`, `he`, `s`, `sh`, `she`, `hi`, `his`, `her`, `hers`). Look at the failure links. `sh` (state 4) fails to state 1, `h`, because `h` is the longest proper suffix of `sh` that is a prefix in the trie. `she` (state 5) fails to state 2, `he`, which is why its output set holds **both** `he` and `she`: when the automaton reaches `she` it has also seen `he`. `his` (7) and `hers` (9) fail to state 3, `s`, since the last letter `s` is a prefix. Running `ushers`: `u` stays at the root, `s` goes to state 3, `h` to 4, `e` to 5, which outputs `he` and `she` at position 3; then `r` has no edge from state 5, so the automaton follows the failure link to state 2 (`he`) and takes `r` to state 8, and `s` reaches state 9, `hers` at position 5. Three matches, the same as the library. Nothing was re-read: that is the point of the failure links.

### Problem E

```python
# Problem E: SYN queue arithmetic. Time to fill a queue = backlog / rate. Rate to keep it full = backlog / entry lifetime.
print("part 1: the queue of 16 in e13, no entries expire inside the 8 s window (an entry lives much longer)")
print("%-10s %-22s %-24s %s" % ("SYN/s", "entries after 8 s", "seconds to fill 16", "measured in e13"))
measured = {2: "16, real client got through", 5: "17, queue full", 10: "17, queue full", 50: "17, queue full"}
for rate in (2, 5, 10, 50):
    print("%-10d %-22d %-24.1f %s" % (rate, min(16, rate * 8), 16 / rate, measured[rate]))
print()
print("part 2: the rate that keeps a queue full for good = backlog / entry lifetime")
retx = 1 + 2 + 4 + 8 + 16 + 32   # the SYN-ACK is retransmitted at 1, 3, 7, 15, 31 s and given up after 32 s more: from memory, 5 retries
print("entry lifetime with 5 SYN-ACK retries: %d s (from memory, not measured here)" % retx)
print("%-8s %-10s %-12s %s" % ("backlog", "lifetime", "min SYN/s", "bandwidth, 84 bytes on the wire per SYN"))
for backlog, lt in [(16, retx), (128, retx), (4096, retx), (65535, retx), (4096, 3)]:
    rate = backlog / lt
    print("%-8d %-10d %-12.1f %.3f Mbit/s" % (backlog, lt, rate, rate * 84 * 8 / 1e6))
```

```text
part 1: the queue of 16 in e13, no entries expire inside the 8 s window (an entry lives much longer)
SYN/s      entries after 8 s      seconds to fill 16       measured in e13
2          16                     8.0                      16, real client got through
5          16                     3.2                      17, queue full
10         16                     1.6                      17, queue full
50         16                     0.3                      17, queue full

part 2: the rate that keeps a queue full for good = backlog / entry lifetime
entry lifetime with 5 SYN-ACK retries: 63 s (from memory, not measured here)
backlog  lifetime   min SYN/s    bandwidth, 84 bytes on the wire per SYN
16       63         0.3          0.000 Mbit/s
128      63         2.0          0.001 Mbit/s
4096     63         65.0         0.044 Mbit/s
65535    63         1040.2       0.699 Mbit/s
4096     3          1365.3       0.918 Mbit/s
```

Part 1: at 2 per second the queue holds **16 entries** after 8 s and needs exactly 8 s to fill, so it is **not yet full** (and `e13` let the real client through); at **5, 10 and 50 per second** it fills in **3.2, 1.6 and 0.3 s** and stays full, matching the **17** entries and the failed client in the measurement (the kernel lets a full queue run one over, from memory). Part 2 is the number that matters: `backlog / lifetime`. A backlog of **4,096** with the 63 s default is held full at **65 SYN/s**, **0.04 Mbit/s**; the same queue with a **3 s** lifetime needs **1,365 SYN/s**, 21 times more. The 63 s is arithmetic on the default SYN-ACK retransmission schedule and was not measured; the lifetime is the number to measure on a real server, with `ss -tan state syn-recv` and a flood slow enough to see entries die.

---

## 16. Cheat sheet

| Topic | Remember |
|---|---|
| Hooks | `prerouting`, `forward` (policy), `input` (protect the box), `output`, `postrouting` (NAT) |
| Rule cost | list: **about 10 ns per rule**, linear; set or trie: flat (5,000 addresses: **0.03 microseconds** more than one rule) |
| Order | `invalid` drop, rate limits that must see every packet, `established,related` accept, then new-flow allow rules |
| Stateless | cannot tell a reply from an attack that looks like one: "ACK set" passes a lone ACK, "SYN clear" blocks the SYN-ACK |
| Stateful | table keyed by the 5-tuple, **symmetric** (smaller endpoint first), a TCP state machine with window check |
| Conntrack states | `SYN_SENT` 120 s, `SYN_RECV` 60 s, `ESTABLISHED` **432,000 s**, `TIME_WAIT` 120 s (from memory), UDP 30 s (120 s with replies) |
| Entry | **256 bytes** here; entries = rate x lifetime; **200 k/s x 30 s = 6 M = 1.54 GB**; buckets for a chain of 4 |
| Lookup | **104 ns** at 1 per bucket, **639** at 15, **12,112** at 244 |
| Full table | early drop helps only against **unreplied** entries: **3 of 20** in, **0 of 20** with assured entries |
| NAT | two tuples per flow; port collision picks another port; port exhaustion per destination; entry must live as long as the flow |
| ALG | FTP `PORT 10,1,0,2,150,175` is port 150 x 256 + 175 = **38,575**; helper rewrites and creates an **expectation** (300 s) |
| App-ID | first client payload: `GET /`, ClientHello, `SSH-2.0-`; **4 of 8** ports were wrong; JA3 names the client software |
| TLS 1.3 | after the Hellos nothing is readable; the name (SNI) stays, the certificate does not; ECH hides the name (from memory) |
| IDS / IPS | IDS reads a copy, alerts only; IPS is in the path and drops; fail-closed drops all, fail-open (`--queue-bypass`) inspects none |
| Evasion | split segments pass a per-packet match; the IPS must **reassemble** the stream in the way the host does |
| Inline cost | median **502 to 744 microseconds**, **16.35 to 1.04 Gbit/s** on this 2 vCPU machine |
| Aho-Corasick | trie + failure links, one step per byte; crossover near **16** patterns; **22 times** at 10,000; **0.40 to 0.02 GB/s** as the table outgrows the cache |
| Regex | backtracking doubles per character (**2.19 s** at 27); RE2 **0.13 ms**; no back references in automaton engines |
| TLS decryption | forge a certificate per name; breaks pinning, mutual TLS, unmanaged clients; **4.18 against 1.85 ms**; keep the CA key in an HSM |
| ESP tunnel | **56 bytes** overhead (20 + 8 + 8 + 16 + 2 + padding); 1,500 byte link carries **1,446** (**1,438** with NAT-T); MSS **1,406** |
| WireGuard | **60 bytes** over IPv4 (from memory) |
| SYN flood | no defence: **0 of 5**; cookies: **5 of 5**, **17,979** cookies; SYN proxy: **0** half-open on the server |
| Flood economics | backlog / lifetime: **4,096 / 63 s = 65 SYN/s = 0.04 Mbit/s** |
| Policing | token bucket: `burst + rate x t` (**10 + 20 x 3.188 = 73.8**, measured **73 of 200**); per-source buckets; `ct count over N`; CoPP does this in silicon |

---

## 17. Sources

- nftables wiki: https://wiki.nftables.org/ (rules, sets, `ct`, `limit`, meters, `synproxy`, `notrack`). The Linux kernel's `nf_conntrack` sysctls: https://docs.kernel.org/networking/nf_conntrack-sysctl.html (the timeouts and limits of sections 2 and 3).
- RFC 4987, TCP SYN flooding attacks and common mitigations: https://www.rfc-editor.org/rfc/rfc4987 (queues, cookies, proxies). RFC 2827 (BCP 38), network ingress filtering: https://www.rfc-editor.org/rfc/rfc2827.
- RFC 4303, IP Encapsulating Security Payload: https://www.rfc-editor.org/rfc/rfc4303. RFC 4106, AES-GCM in ESP: https://www.rfc-editor.org/rfc/rfc4106. RFC 3948, UDP encapsulation of ESP: https://www.rfc-editor.org/rfc/rfc3948. RFC 7296, IKEv2: https://www.rfc-editor.org/rfc/rfc7296.
- WireGuard whitepaper (Donenfeld): https://www.wireguard.com/papers/wireguard.pdf.
- RFC 8446, TLS 1.3: https://www.rfc-editor.org/rfc/rfc8446. JA3 (Salesforce): https://github.com/salesforce/ja3.
- Suricata documentation (rules, `nfq` mode, app-layer, stream reassembly): https://docs.suricata.io/.
- A. Aho and M. Corasick, "Efficient string matching: an aid to bibliographic search", Communications of the ACM, 1975. Hyperscan: https://github.com/intel/hyperscan, and its paper, "Hyperscan: A Fast Multi-pattern Regex Matcher for Modern CPUs", NSDI 2019.
- Russ Cox, "Regular Expression Matching Can Be Simple And Fast": https://swtch.com/~rsc/regexp/regexp1.html. RE2: https://github.com/google/re2.
- Cisco, control plane policing best practices (the CoPP of section 11.4, from memory; read the vendor's current document for your platform).
- Measured on this VM: `v/*.out` for experiments `e1` to `e13` and answers `ans_a` to `ans_e`. Everything marked "from memory" is **not** measured here: verify it against the RFCs and the kernel documentation above before relying on it.
