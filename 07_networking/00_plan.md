# Networking and Communication: Track Plan

> **Track code:** NET · **Weeks:** 10–15 for NET 01–09, 16–19 for NET 11–14, 24 for NET 15 · **Budget:** ~40 hours · **Prerequisites:** OS 01–03 and 17 (system calls, processes, IPC basics); C 02–04 (pointers, structs, endianness); LX 08 (the Linux networking toolbox: `ip`, `ss`, `tcpdump`, `netns`, `nft`), which NET uses from the first tutorial

This track follows a packet all the way down and back up: from Ethernet frames and IP routing, through TCP's state machine and congestion control, into the socket API and the Linux network stack, out to the protocols the web runs on, and into the security devices that inspect it all. NET 11 to 14 then widen from one host to the fabric: datacenter routing, RDMA and lossless Ethernet, load balancers, and queueing and QoS. Your Wi-Fi platform experience covers parts of L2 already; this track makes the rest as solid, and builds the network-security depth that security roles interview for.

"IPC and communication" is split across two tracks: local IPC (pipes, shared memory, signals, Unix sockets) is in `05_operating_systems` (OS 17 and 18); network communication is here.

---

## 1. Why this track matters

Networking questions arrive from several directions: "what happens when you type a URL" and TCP and HTTP behaviour in system design; "how does a packet move through a firewall" (stateful inspection, session tables, NAT, TLS decryption, intrusion prevention) in security roles; protocol depth (switching, routing, longest-prefix match, data plane versus control plane) in network-equipment roles; and RDMA, kernel bypass, zero-copy, NIC offloads, ECMP, PFC and congestion control in high-performance and GPU-cluster roles (Nvidia, hyperscaler AI networking). Some online assessments include writing a socket server or parsing a packet header in C.

---

## 2. What you will be able to do after this track

1. Explain what happens, layer by layer, when you type a URL and press Enter, down to ARP and up to TLS.
2. Subnet by hand, explain longest-prefix match, and walk through NAT translating a connection.
3. Draw the TCP state machine, explain retransmission, flow control and congestion control (Reno, CUBIC, BBR), and diagnose TIME_WAIT and slow-start issues.
4. Write a correct, non-blocking TCP server in C with `epoll` that handles partial reads and writes, thousands of connections, and `SIGPIPE`.
5. Trace a packet through the Linux stack from NIC ring to socket and back, naming NAPI, GRO, netfilter, the routing decision and `sk_buff`.
6. Explain the TLS 1.3 handshake, DNS resolution, and HTTP/1.1 vs 2 vs 3.
7. Explain how a next-generation firewall works: session tables, App-ID-style classification, IDS/IPS signatures, TLS decryption, and DDoS defences such as SYN cookies.
8. Describe a leaf-spine datacenter fabric: why ECMP hashing polarises, how BGP is used between switches, what VXLAN/EVPN adds, and how fast failures are detected (BFD) and converged.
9. Explain RoCEv2 and why it needs a lossless fabric: PFC, ECN and DCQCN, head-of-line blocking, PFC deadlock, incast, and how NCCL traffic stresses a network.
10. Design a Layer 4 load balancer (consistent hashing as in Maglev, DSR, connection tracking, anycast) and say where XDP or IPVS fits.
11. Explain queueing in the network: token bucket, policing against shaping, DSCP, DRR and WFQ, buffers and bufferbloat, CoDel and FQ-CoDel.
12. Write a header parser and checksum in C, and implement longest-prefix match with a trie, the data-structure question network-equipment interviews like.

---

## 3. Topics

| # | File | Topic | Key subtopics | Status | Week |
|---|---|---|---|---|---|
| 01 | `01_network_models_and_layer2.md` | Network models and layer 2 | OSI vs TCP/IP, encapsulation, Ethernet frames, MAC addresses, switching and MAC learning, VLANs (802.1Q), ARP, MTU, spanning tree at a glance, LAG/LACP, IPv6 neighbour discovery against ARP, 802.1X port authentication, 802.11 framing compared with Ethernet, reading a capture with `tcpdump` and BPF filters | Written, [open](01_network_models_and_layer2.md) | 10 |
| 02 | [`02_ip_routing_and_layer3.md`](02_ip_routing_and_layer3.md) | IP, routing and layer 3 | IPv4 and IPv6 headers, subnetting and CIDR by hand, routing tables and longest-prefix match, ICMP and traceroute, NAT and PAT, fragmentation and path MTU discovery, IP and TCP header parsing and the Internet checksum in C, longest-prefix-match structures (binary and multibit tries, DIR-24-8, why a hash table does not work), multicast and IGMP, MPLS labels, OSPF and BGP at a glance (the full treatment is NET 11) | Written | 11 |
| 03 | [`03_tcp_udp_and_transport.md`](03_tcp_udp_and_transport.md) | TCP, UDP and the transport layer | Ports and demultiplexing, UDP, TCP handshake and teardown, the state machine, sequence numbers, retransmission and RTO, flow control, congestion control (Reno, CUBIC, BBR), Nagle and delayed ACK, TIME_WAIT, keepalives, timeouts, retries and exponential backoff with jitter, `ss -ti` and `TCP_INFO`, QUIC | Written | 12 |
| 04 | [`04_socket_programming_in_c.md`](04_socket_programming_in_c.md) | Socket programming in C | The socket API end to end, a TCP echo server and client, UDP, byte order, partial reads and writes, framing a message stream, non-blocking sockets, `select` and `poll`, `SIGPIPE` and `SO_REUSEADDR`, deadlines and timeouts on sockets, raw and `AF_PACKET` sockets, a concurrent chat server | Written | 12 |
| 05 | [`05_epoll_and_io_uring.md`](05_epoll_and_io_uring.md) | `epoll` and `io_uring` | Readiness vs completion, level- vs edge-triggered `epoll`, `EPOLLONESHOT` and `EPOLLEXCLUSIVE`, `io_uring` submission and completion rings, choosing, measuring | Written | 13 |
| 06 | `06_linux_network_stack.md` | The Linux network stack | RX path: NIC → DMA ring → interrupt → NAPI poll → GRO → netfilter → routing → TCP → socket queue; TX path; `sk_buff`; offloads (checksum, TSO, GSO, GRO, RSS, RPS); qdiscs; conntrack and netfilter hooks; where packets are dropped and how to see it (`nstat`, `ethtool -S`, `/proc/net/softnet_stat`, `dropwatch`); the socket buffers and `rmem`/`wmem` tuning | Written | 13 |
| 07 | [`07_zero_copy_networking.md`](07_zero_copy_networking.md) | Zero-copy networking | The copies you remove, `AF_PACKET` rings, XDP, `AF_XDP`, DPDK, transmit-side zero-copy | Written (Nov 2026) | 14 |
| 08 | [`08_application_protocols_dns_http_tls.md`](08_application_protocols_dns_http_tls.md) | Application protocols: DNS, HTTP and TLS | DNS resolution and caching, HTTP/1.1 (keep-alive, head-of-line blocking), HTTP/2 (streams, HPACK), HTTP/3 over QUIC, TLS 1.3 handshake, certificates and chains of trust, gRPC, WebSockets, MQTT for IoT, DHCP, NTP and PTP at a glance, SNMP and gNMI/streaming telemetry for network devices | Written (Nov 2026) | 14 |
| 09 | [`09_network_security_firewalls_ips_dpi.md`](09_network_security_firewalls_ips_dpi.md) | Network security: firewalls, IPS and DPI | Stateless vs stateful firewalls, connection tracking and session tables, NAT in a firewall, application identification, IDS vs IPS, signature matching (Aho-Corasick, regex engines), TLS decryption and its trade-offs, IPsec and VPNs, DDoS defences, SYN cookies, rate limiting and policing of control-plane traffic | Written (Nov 2026) | 15 |
| 10 | [`10_wifi/`](#10-the-wi-fi-sub-track-wf-01-to-wf-10) | Wi-Fi (802.11) sub-track, WF 01 to WF 10 | Expanded Nov 2026 after comparing with the Candela Wi-Fi Technology Fundamentals course: see section 10. Your home turf, so read for gaps and for the interview framing | Stretch (do WF 01 to 04 first if a target team is wireless) | after 15 |
| 11 | [`11_routing_protocols_and_datacenter_fabrics.md`](11_routing_protocols_and_datacenter_fabrics.md) | Routing protocols and datacenter fabrics | Link-state against distance-vector, OSPF (areas, LSA flooding, SPF), BGP (sessions, path attributes, best-path selection, eBGP and iBGP, route reflectors, update processing), BGP as the datacenter IGP, leaf-spine and Clos topologies, oversubscription, ECMP and hash polarisation, flowlets, VXLAN and EVPN, BFD, convergence after a link failure, how a route reaches the hardware (FIB, SONiC at a glance) | Written (Nov 2026) | 15 |
| 12 | [`12_rdma_roce_and_lossless_fabrics.md`](12_rdma_roce_and_lossless_fabrics.md) | RDMA, RoCE and lossless fabrics | Why RDMA (kernel bypass, zero-copy, offloaded transport), verbs (QP, CQ, MR, send, write, read), InfiniBand against RoCEv2, PFC and head-of-line blocking, ECN and DCQCN, PFC storms and deadlock, incast, packet spraying and adaptive routing, collectives and how NCCL ring and tree traffic loads a fabric. Cross-reference CO 29 for the topology maths | Written (Nov 2026) | 17 |
| 13 | [`13_load_balancing_proxies_and_anycast.md`](13_load_balancing_proxies_and_anycast.md) | Load balancing, proxies and anycast | L4 against L7, NAT mode against DSR, consistent hashing and Maglev hashing, connection tracking and the problem of backend changes, health checks, IPVS and `nftables` load balancing, XDP load balancers (Katran-style), anycast and BGP, reverse proxies and connection pooling, TLS termination. Cross-reference the load-balancing design problem in `11_hld` | Written (Nov 2026) | 18 |
| 14 | [`14_queueing_qos_and_traffic_management.md`](14_queueing_qos_and_traffic_management.md) | Queueing, QoS and traffic management | Queueing delay and Little's law applied to a link, buffer sizing and bufferbloat, tail drop, RED and ECN marking, CoDel and FQ-CoDel, token bucket and leaky bucket, policing against shaping, strict priority, DRR and WFQ, DSCP and 802.1p, `tc` qdiscs on Linux (`tbf`, `htb`, `fq_codel`, `cake`) | Written (Nov 2026) | 19 |
| 15 | [`15_networking_interview_question_bank.md`](15_networking_interview_question_bank.md) | Networking interview question bank | Questions with answers across the track, a "network is slow" and "connection fails" triage tree, 10 to 15 troubleshooting scenarios reproduced in network namespaces (as in LX 14), design-style questions (load balancer, firewall data path, packet router), a one-page cheat sheet | Written (Nov 2026) | 24 |

---

## 4. Experiments and drills (inside the tutorials)

There are no separate lab folders. Each tutorial carries its own small runnable experiments and measured output, built in network namespaces so nothing touches the host network. Planned experiments by topic:

| Topic | What you run and observe |
|---|---|
| 01 | Two namespaces joined by a bridge; `tcpdump` shows ARP, MAC learning in `bridge fdb`, and a VLAN tag |
| 02 | A three-namespace routed chain; `traceroute` shows TTL expiry; a NAT rule translating two hosts that pick the same source port; twenty subnetting and LPM drills with answers; an LPM trie in C of about 60 lines |
| 03 | The handshake and teardown in `tcpdump`; `ss -ti` showing the congestion window; loss and delay with `tc netem` where the kernel has it (the sandbox VM does not, so the tutorial says which numbers were measured and which need your own Linux machine) |
| 04 | A blocking echo server, then one that survives partial I/O and `SIGPIPE`; a length-prefixed framing exercise |
| 05 | An `epoll` server in both trigger modes, then a raw `io_uring` ring and echo server; system call counts and round trips measured for both |
| 06 | Kernel stacks for the receive and transmit paths with `bpftrace`; offloads on and off with skb counts; NAPI work per poll; `nft monitor trace` through the hooks; six drops found by `kfree_skb` reason and counter; RPS, socket buffers and qdisc queueing measured |
| 07 | A receive ladder (`recvfrom`, `recvmmsg`, AF_PACKET ring, AF_XDP copy mode) measured by thread and machine CPU; XDP drop against `nft` drop with `perf`; a verifier rejection; file senders (`read`+`write`, `sendfile`, `splice`); `MSG_ZEROCOPY` cost by send size. DPDK, real-NIC XDP and AF_XDP zero-copy are from memory |
| 08 | A DNS query built by hand and decoded; a recursive resolver (`unbound`) walking a toy three-level hierarchy with caching, negative caching, truncation and QNAME minimisation; HTTP/1.1, HTTP/2 (`nghttp -nv` frames, HPACK) and the multiplexing and 2 percent loss runs; a TLS 1.3 handshake record by record, a private chain, mutual TLS, resumption, 0-RTT and handshake CPU; a QUIC client and server in `aioquic`; WebSocket frames and MQTT QoS packet counts; DHCP DORA, NTP offset from four timestamps, SNMP walk against bulk walk. gRPC, PTP, gNMI, DNSSEC and HTTP/3 loss recovery are from memory, and no round trip time could be added |
| 09 | ACL cost against rule count and sets; stateless against stateful filtering (a lone ACK, the conntrack life cycle and timeouts); a table limited to 1,000 entries under a one-way flood and under assured flows; NAT port collision and an FTP helper with its expectation; application identification from the first payload (JA3, TLS 1.3 against 1.2); Suricata inline on a netfilter queue (drop, alert, segmentation evasion, IDS replay, fail-open and fail-closed, cost); Aho-Corasick against `memmem`; a backtracking regular expression against RE2; a TLS forward proxy (forged certificate, pinning, mutual TLS, policy, cost, key log); ESP packet sizes built and decrypted in Python; a SYN flood against no defence, cookies and `synproxy`; control-plane policing (token bucket, per-source meter, connection limit); a flow table in C. No ESP, WireGuard or flowtable in the sandbox kernel, so IPsec and offload are marked "from memory" |
| 11 | Real FRR daemons in network namespaces on a three leaf, two spine fabric: OSPF adjacencies, database and SPF; eBGP sessions captured and decoded, best path steered by local preference and prepending, iBGP with and without a route reflector; a datacenter AS plan and the valley path after a failure (OSPF against BGP); an ECMP hash across three tiers (addresses against 5-tuple, hash polarisation and per-tier seeds); ordinary against resilient next hop groups; VXLAN overhead, MTU and outer source port entropy; EVPN type 2 and 3 routes; convergence after carrier loss and silent failure with hold timers and BFD; distance vector counting to infinity; fabric sizing arithmetic. No MPLS in the sandbox kernel, no `netem`, and hardware FIBs, SONiC and flowlets are marked "from memory" |
| 12 | Software RoCE (`rdma_rxe`) if the kernel has it, otherwise the verbs flow is explained from the API and marked as not run here |
| 13 | A small consistent-hashing table in C or Python, measuring disruption when a backend is removed |
| 14 | M/D/1 delay against load with Poisson traffic and a sampled backlog; a token bucket after idle and the burst a 100 Mbit bucket needs; a shaper against nftables and ingress policers and an `ifb` shaper under TCP; bufferbloat at four buffer sizes for cubic, reno and BBR; HTB sharing, strict priority and starvation; DSCP, TOS, 802.1Q priority and an ingress remark; a three class uplink design. `fq_codel`, `codel`, `red`, `drr`, `prio`, `netem` and `act_police` are missing from the sandbox kernel, so RED, CoDel, FQ-CoDel, DRR and WFQ run in a small packet level model that is checked against the kernel in one case only (Reno through tail drop) |
| 15 | Eleven troubleshooting scenarios reproduced in namespaces (refused against rejected against dropped with the SYN schedule, a path MTU black hole and its two fixes, reverse path filtering, the 40 ms Nagle and delayed ACK stall, an overflowing accept queue, ephemeral port exhaustion, resolver timeouts, a slow reader, loss against throughput, a full conntrack table, dead peer detection) plus computed answers for windows, Mathis, subnetting, the checksum, port and conntrack sizing and timeout arithmetic. Four more scenarios point at NET 06, 11 and 14 or are from memory; no `netem`, so no propagation delay |

Experiments that need hardware or kernel options the sandbox lacks are marked "from memory" or "needs your machine", never presented as measured.

---

## 4a. How the NET tutorials are made practical and graphical

Every NET tutorial follows these rules, so the track is something you run and look at, not only read:

- **Run it:** each concept has a small command or program whose output is shown, taken from a real run in network namespaces (`ip netns`, `veth`, bridges), `tcpdump`, `ss`, `nstat` or `tc`. Output that was not measured here is marked.
- **See it:** every packet exchange is drawn twice, as a Mermaid sequence diagram (renders on GitHub) and as the real `tcpdump` lines beside it, so the picture and the bytes agree. Header layouts are drawn as bit-field boxes and then checked against a real capture with offsets.
- **Draw it:** topologies (switch, router chain, leaf-spine, load-balancer path) are drawn as diagrams and then built as namespaces you can recreate with a few commands.
- **Plot it where a number matters:** congestion window over time, queue depth and latency under load, ECMP imbalance, LPM lookup cost. One small script produces a text table; a plot is added only when a shape matters, and the data stays in the tutorial.
- **Capture files:** Wireshark is used for reading, but the tutorial prints the decoded fields it needs, so nothing depends on opening a GUI.
- **Programs stay small**, per your standing rule: no simulators, no harnesses.

---

## 5. Representative interview questions

1. What happens when you type `https://example.com` into a browser and press Enter? Go as deep as you can.
2. How does a switch learn where to send a frame? What does a router do differently?
3. Given 192.168.10.0/22, how many hosts does it hold, and which addresses are the network and broadcast addresses?
4. Why does TCP need a three-way handshake? Why does TIME_WAIT exist, and why can it be a problem on a busy server?
5. Explain TCP congestion control. Why does BBR behave differently from CUBIC on a lossy link?
6. Your TCP server reads 100 bytes, but the client sent 300. What happened, and how do you frame messages?
7. Level-triggered vs edge-triggered `epoll`: what goes wrong if you use edge-triggered without draining the socket?
8. Walk a packet from the wire to `recv()` in Linux. Where can it be dropped, and how would you know?
9. How does NAT handle two internal hosts using the same source port to reach the same server?
10. How does a stateful firewall decide whether a packet belongs to an existing connection?
11. How does TLS decryption on a firewall work? What breaks, and what are the privacy trade-offs?
12. A SYN flood is hitting your server. What happens to the backlog, and how do SYN cookies help?
13. Implement longest-prefix match for IPv4. What changes when the table has a million routes and lookups must take a few memory accesses?
14. A leaf-spine fabric uses ECMP, yet one uplink is saturated and others are idle. Why, and what would you change?
15. Why does RoCEv2 need PFC, what goes wrong with PFC, and how do ECN and DCQCN help?
16. Design a Layer 4 load balancer that survives a backend being added or removed without breaking existing connections.
17. What is bufferbloat, and how do CoDel and fair queueing fix it?
18. A service sees random 200 ms stalls on small requests. Walk through Nagle, delayed ACK, retransmission timeouts and how you would tell them apart.

---

## 6. How to study this track

1. Follow the five-step method in [`../README.md`](../README.md).
2. Capture traffic for every protocol you study. Seeing the bytes in Wireshark fixes the header layouts in memory faster than any diagram.
3. Write the socket code yourself in C; it is a likely online-assessment task.
   Also write a packet-header parser and an LPM trie yourself.
4. Topic 09 is the centre of the network-security preparation; read it together with [`../11_hld/03_systems_domain/01_data_plane_vs_control_plane.md`](../11_hld/03_systems_domain/01_data_plane_vs_control_plane.md) and design problem 18 (SSL decryption and NAT) in `11_hld`.
5. NET 11 and 12 overlap CO 29 (interconnection networks, fat-tree, ECMP, incast, RDMA and DCQCN). Read CO 29 first; the NET tutorials add the protocols and the switch behaviour, not the topology maths again.
6. NET 13 pairs with the load-balancing and proxy design problem in `11_hld`; NET 04 and 05 feed Project 01 (`rkv`), and NET 02 feeds Project 03 (`pfilt`, the packet classifier).
7. For Nvidia and AI-infrastructure roles do NET 11 and 12 fully; for security roles (Palo Alto Networks) do 09 and 13 fully; for Google and Meta system-design loops 03, 08 and 13 matter most.

---

## 7. Resources

| Resource | Use it for |
|---|---|
| *Computer Networking: A Top-Down Approach*, Kurose & Ross | The main textbook, layer by layer |
| Beej's Guide to Network Programming (free) | The socket API with working examples |
| *TCP/IP Illustrated, Vol. 1* (2nd ed.), Fall & Stevens | TCP behaviour in detail, with packet traces |
| *Unix Network Programming, Vol. 1*, W. Richard Stevens | The classic on sockets and I/O models |
| *High Performance Browser Networking*, Ilya Grigorik (free online) | TCP, TLS, HTTP/2 and HTTP/3 from a performance view |
| *Understanding Linux Network Internals*, Christian Benvenuti; kernel `Documentation/networking/` | The Linux stack (the book is old; verify against current docs) |
| *Computer Networks: A Systems Approach*, Peterson & Davie (free online) | Protocol design and the control plane, a good second textbook beside Kurose & Ross |
| Stanford CS144 (labs: reliable transport, router, NAT) | A course whose projects are the best drills for 02, 03 and 11: build a TCP and an IP router |
| Berkeley CS168 (layering, addressing, routing, reliable transport, congestion control) | A syllabus to check your coverage against; its projects include traceroute and routing |
| MIT 6.829 (congestion control, datacenter networks, SDN, programmable routers) | Paper-driven depth for 12 and 14: read the datacenter and congestion-control papers |
| RFCs: 793 and 9293 (TCP), 5681 (congestion control), 8446 (TLS 1.3), 9000 (QUIC), 4271 (BGP-4) | The sources behind the questions; read the sections the tutorials cite |
| *Linux Kernel Networking*, Rami Rosen; the kernel `Documentation/networking/` | A more current view of the stack than Benvenuti |
| Maglev (Google, NSDI 2016) and Katran (Meta) write-ups | The load-balancer datapath for 13 |
| DCQCN (SIGCOMM 2015) and the NCCL documentation | Lossless fabrics and collective traffic for 12 |
| Nexthop AI and similar AI-networking interview write-ups | The shape of questions Nvidia-type roles ask: BGP best path, ECMP, PFC, ECN, LPM tables |
| Candela Technologies, *Wi-Fi Technology Fundamentals* (free, slides, videos and notes per session) | The syllabus and slides behind the Wi-Fi sub-track in section 10 |
| Cloudflare and Palo Alto Networks engineering blogs | DDoS, TLS and firewall internals in practice |

---

## 8. Progress

| # | Topic | Status |
|---|---|---|
| 01–09, 11–15 | Core topics | NET 01 to 09 and 11 to 15 written (Nov 2026; NET 10 is the Wi-Fi stretch sub-track) |
| 10 | Wi-Fi sub-track (WF 01 to WF 10) | Stretch; planned in section 10 |
| 14 | Queueing | Written (Nov 2026, added after the audit) |
| 15 | Question bank | Written (Nov 2026) |

Cross-track view: [`../00_MASTER_PLAN.md`](../00_MASTER_PLAN.md).

---

## 9. Coverage audit (November 2026)

The plan was compared with: Stanford CS144 and its lab list, Berkeley CS168's syllabus and projects, MIT 6.829's topic list, Kurose & Ross and Peterson & Davie chapter lists, and an AI-networking interview write-up (BGP best path, leaf-spine, ECMP, RoCEv2, PFC, ECN and DCQCN, incast, LPM tables). I did not fetch every syllabus in full; where I relied on memory of a book's contents, the tutorial will say so.

| Gap found | Fix |
|---|---|
| Routing protocols stopped at "OSPF and BGP at a glance", yet BGP best path, ECMP, leaf-spine and VXLAN are standard in network-equipment and AI-infrastructure loops | New NET 11 |
| RDMA, RoCE, PFC, ECN and DCQCN appeared only as a CO 29 section; no tutorial on the protocol and the failure modes (PFC storms, deadlock) | New NET 12, cross-referenced to CO 29 |
| Load balancers (Maglev, DSR, XDP, anycast) had no home outside a design problem | New NET 13 |
| Queueing, shaping, policing, CoDel and bufferbloat were missing | New NET 14 |
| No question bank or troubleshooting drill, unlike the Linux, C and OS tracks | New NET 15 |
| LPM was explained but its data structures were not, and header parsing and the checksum are common coding questions | Added to NET 02 (tries, DIR-24-8, parser and checksum in C) |
| Timeouts, retries and backoff, keepalives and `TCP_INFO` were missing from transport and sockets | Added to NET 03 and 04 |
| IPv6 neighbour discovery, LACP, 802.1X and capture reading were missing from layer 2 | Added to NET 01 |
| DHCP, NTP and PTP, and device telemetry (SNMP, gNMI) were missing from application protocols | Added to NET 08 |
| Drop counters and socket-buffer tuning were missing from the Linux stack | Added to NET 06 |
| The labs table promised separate lab folders, against the standing rule of small in-tutorial programs | Replaced with section 4, in-tutorial experiments; netem limits noted |
| Prerequisites omitted LX 08, which the tutorials use | Added to the header |
| Hours were ~25 and weeks ended at 15 | ~40 hours; NET 11 to 14 in weeks 16 to 19, NET 15 in week 24 |

| The Wi-Fi item was one stretch line, while the Candela course (sixteen weeks, eight modules) covers a full Wi-Fi syllabus that your target teams (wireless, access-point and controller roles) test | Expanded into the WF sub-track, section 10, mapped session by session |
| Tutorials were described in prose only | Section 4a fixes the practical and graphical format for every NET tutorial |

**Left out on purpose:** MPLS and segment routing beyond a label-stack overview, SD-WAN, 5G core, cellular beyond what OpenRoaming needs, and BGP policy design at ISP scale. Add them only if a target team asks.

---

## 10. The Wi-Fi sub-track (WF 01 to WF 10)

Source compared: the Candela Technologies *Wi-Fi Technology Fundamentals* course page (Part 1 basics, Modules 1 to 4, sessions 1a to 4d; Part 2 advanced, Modules 5 to 8, sessions 5a to 8d). I read the page's outline; I did not open the slide decks or videos, so the tutorials will be written from the topic lists plus the standards and your platform knowledge, and will say where they rely on memory. The course targets junior and mid-level engineers, so these tutorials go deeper than its sessions where a Senior or Staff interview does (frame formats, 4-way handshake keys, airtime and rate maths, roaming timings, driver interfaces).

Priority: stretch, after NET 15. Do WF 01 to 04 and WF 10 first if a target team is wireless (Cisco, Ruckus-like AP and controller roles, Meta and Google device connectivity teams); the rest are read for gaps. About 20 hours in total.

| # | File (in `10_wifi/`) | Topic | Candela sessions covered | Key subtopics beyond the session titles |
|---|---|---|---|---|
| WF 01 | `01_wifi_landscape_and_ap_anatomy.md` | Landscape, topologies, standards, AP anatomy | 1a, 1b, 1c, 1d | Generations and what each changed; infrastructure, mesh, bridge and ad hoc; backhaul; IEEE against Wi-Fi Alliance; PHY, baseband, lower and upper MAC split in an AP and where the CPU, the radio and the offload engine sit |
| WF 02 | `02_wifi_phy.md` | The PHY | 2a, 2b, 2c, 2d | ISM and UNII bands, channels and widths, DFS; modulation, coding, multipath, MIMO, OFDM and OFDMA; the MCS table and a worked data-rate calculation (subcarriers, bits per symbol, coding rate, guard interval); PLCP and PMD, preamble and headers |
| WF 03 | `03_wifi_mac.md` | The MAC | 3a, 3b, 3c, 3d | Beacons, BSSID, scanning, probe and association; management, control and data frames and the four-address header; carrier sense, NAV, DCF, backoff, IFS, EDCA access categories; aggregation (A-MPDU, A-MSDU), block ack, rate adaptation; airtime fairness and why one slow client hurts everyone |
| WF 04 | `04_wifi_security.md` | Security | 4a, 4b, 4c, 4d | WEP to WPA3 (SAE); personal against enterprise; 802.1X, EAP methods, RADIUS; TKIP, CCMP, GCMP; the PMK, PTK and GTK key hierarchy and the 4-way handshake message by message; deauthentication and DoS, evil twin, KRACK-class and dictionary attacks, PMF; captive portal, WPS, OpenRoaming and EAP-SIM/AKA |
| WF 05 | `05_wifi_roaming_and_radio_management.md` | Roaming and radio resource management | 5a | 802.11k, v and r, 802.1X re-authentication cost and why 11r removes it, OKC and PMKSA caching, band steering and client steering, RRM, channel and power selection, DFS events and radar; measured roam times as a worked example |
| WF 06 | `06_wifi6_6e_7_features.md` | Wi-Fi 6, 6E and 7 | 5b, 5c, 5d | OFDMA and resource units, MU-MIMO, BSS colouring and spatial reuse, TWT; 6 GHz, AFC, RNR, FILS, PCS, standard power and low-power indoor; 320 MHz, 4096-QAM, MLO and its modes, multi-RU, preamble puncturing |
| WF 07 | `07_wifi_architectures_mesh_and_services.md` | Controller architectures, mesh and services | 6a, 6b, 6c, 6d | Thick against thin AP, physical and cloud controllers, CAPWAP and tunnelling, the control plane against data plane split (links to `11_hld/03_systems_domain/01_data_plane_vs_control_plane.md`); traffic shaping and policing (links to NET 14), parental controls, analytics, telemetry at fleet scale (links to Project 05 `telemetrix`); mesh access, backhaul and roaming; sensing, location and IT-to-OT |
| WF 08 | `08_wifi_capture_tools_and_debug.md` | Capture, tools and debug | 7a, 7b, 7c | Monitor mode, radiotap headers, Wireshark WLAN filters, information elements, I/O graphs; `iw`, `iperf3`, site survey and heatmap ideas; supplicant and AP logs, serial, Telnet and REST management, reading a failed association from the log |
| WF 09 | `09_wifi_testing.md` | Testing | 8a, 8b, 8c, 8d | Lab against field and automated against manual testing, benchmarking, scale and stress, repeatability, test houses, writing and running a test plan; throughput, roaming and latency metrics |
| WF 10 | `10_linux_wireless_stack_and_openwrt.md` | Linux wireless stack and OpenWRT | 7d (and beyond) | nl80211, cfg80211, mac80211, the driver and firmware split, `hostapd` and `wpa_supplicant`, `iw` and `iwconfig`, virtual interfaces, OpenWRT building blocks (`netifd`, UCI, `procd`), where the data path leaves the kernel for offload; `mac80211_hwsim` for a software radio |

**Practical and graphical for Wi-Fi:** the sandbox has no radio, but the Linux `mac80211_hwsim` module gives virtual radios, so `hostapd` and `wpa_supplicant` can run an association and a 4-way handshake in this environment if the kernel has the module; otherwise the tutorials use captured example frames and mark them. Each tutorial has a frame or handshake sequence diagram, an airtime or data-rate calculation done in a short script, and a channel and band chart as a table or drawing.

**Coverage check against Candela:** every one of the 32 listed sessions (1a to 4d, 5a to 8d) maps to a row above. Candela session 7d (OpenWRT basics) maps to WF 10. Nothing in the course is left out; items I added are the key hierarchy and handshake detail, airtime fairness, 802.1X re-authentication cost, `mac80211_hwsim`, and the Linux driver and firmware interfaces.
