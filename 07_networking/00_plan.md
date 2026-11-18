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

Topics 05 and 07 are older, Staff-level notes; each is rewritten in the same detailed style as the new tutorials before you study it.

| # | File | Topic | Key subtopics | Status | Week |
|---|---|---|---|---|---|
| 01 | `01_network_models_and_layer2.md` | Network models and layer 2 | OSI vs TCP/IP, encapsulation, Ethernet frames, MAC addresses, switching and MAC learning, VLANs (802.1Q), ARP, MTU, spanning tree at a glance, LAG/LACP, IPv6 neighbour discovery against ARP, 802.1X port authentication, 802.11 framing compared with Ethernet, reading a capture with `tcpdump` and BPF filters | To write | 10 |
| 02 | `02_ip_routing_and_layer3.md` | IP, routing and layer 3 | IPv4 and IPv6 headers, subnetting and CIDR by hand, routing tables and longest-prefix match, ICMP and traceroute, NAT and PAT, fragmentation and path MTU discovery, IP and TCP header parsing and the Internet checksum in C, longest-prefix-match structures (binary and multibit tries, DIR-24-8, why a hash table does not work), multicast and IGMP, MPLS labels, OSPF and BGP at a glance (the full treatment is NET 11) | To write | 11 |
| 03 | `03_tcp_udp_and_transport.md` | TCP, UDP and the transport layer | Ports and demultiplexing, UDP, TCP handshake and teardown, the state machine, sequence numbers, retransmission and RTO, flow control, congestion control (Reno, CUBIC, BBR), Nagle and delayed ACK, TIME_WAIT, keepalives, timeouts, retries and exponential backoff with jitter, `ss -ti` and `TCP_INFO`, QUIC | To write | 12 |
| 04 | `04_socket_programming_in_c.md` | Socket programming in C | The socket API end to end, a TCP echo server and client, UDP, byte order, partial reads and writes, framing a message stream, non-blocking sockets, `select` and `poll`, `SIGPIPE` and `SO_REUSEADDR`, deadlines and timeouts on sockets, raw and `AF_PACKET` sockets, a concurrent chat server | To write | 12 |
| 05 | [`05_epoll_and_io_uring.md`](05_epoll_and_io_uring.md) | `epoll` and `io_uring` | Readiness vs completion, level- vs edge-triggered `epoll`, `EPOLLONESHOT` and `EPOLLEXCLUSIVE`, `io_uring` submission and completion rings, choosing, measuring | Exists, rewrite due | 13 |
| 06 | `06_linux_network_stack.md` | The Linux network stack | RX path: NIC → DMA ring → interrupt → NAPI poll → GRO → netfilter → routing → TCP → socket queue; TX path; `sk_buff`; offloads (checksum, TSO, GSO, GRO, RSS, RPS); qdiscs; conntrack and netfilter hooks; where packets are dropped and how to see it (`nstat`, `ethtool -S`, `/proc/net/softnet_stat`, `dropwatch`); the socket buffers and `rmem`/`wmem` tuning | To write | 13 |
| 07 | [`07_zero_copy_networking.md`](07_zero_copy_networking.md) | Zero-copy networking | The copies you remove, `AF_PACKET` rings, XDP, `AF_XDP`, DPDK, transmit-side zero-copy | Exists, rewrite due | 14 |
| 08 | `08_application_protocols_dns_http_tls.md` | Application protocols: DNS, HTTP and TLS | DNS resolution and caching, HTTP/1.1 (keep-alive, head-of-line blocking), HTTP/2 (streams, HPACK), HTTP/3 over QUIC, TLS 1.3 handshake, certificates and chains of trust, gRPC, WebSockets, MQTT for IoT, DHCP, NTP and PTP at a glance, SNMP and gNMI/streaming telemetry for network devices | To write | 14 |
| 09 | `09_network_security_firewalls_ips_dpi.md` | Network security: firewalls, IPS and DPI | Stateless vs stateful firewalls, connection tracking and session tables, NAT in a firewall, application identification, IDS vs IPS, signature matching (Aho-Corasick, regex engines), TLS decryption and its trade-offs, IPsec and VPNs, DDoS defences, SYN cookies, rate limiting and policing of control-plane traffic | To write | 15 |
| 10 | `10_wifi_80211_internals.md` | Wi-Fi (802.11) internals | Optional, your home turf: MAC layer, association and WPA3, aggregation, rate control, mac80211 and cfg80211. Write only if a target team asks | Stretch | — |
| 11 | `11_routing_protocols_and_datacenter_fabrics.md` | Routing protocols and datacenter fabrics | Link-state against distance-vector, OSPF (areas, LSA flooding, SPF), BGP (sessions, path attributes, best-path selection, eBGP and iBGP, route reflectors, update processing), BGP as the datacenter IGP, leaf-spine and Clos topologies, oversubscription, ECMP and hash polarisation, flowlets, VXLAN and EVPN, BFD, convergence after a link failure, how a route reaches the hardware (FIB, SONiC at a glance) | To write | 16 |
| 12 | `12_rdma_roce_and_lossless_fabrics.md` | RDMA, RoCE and lossless fabrics | Why RDMA (kernel bypass, zero-copy, offloaded transport), verbs (QP, CQ, MR, send, write, read), InfiniBand against RoCEv2, PFC and head-of-line blocking, ECN and DCQCN, PFC storms and deadlock, incast, packet spraying and adaptive routing, collectives and how NCCL ring and tree traffic loads a fabric. Cross-reference CO 29 for the topology maths | To write | 17 |
| 13 | `13_load_balancing_proxies_and_anycast.md` | Load balancing, proxies and anycast | L4 against L7, NAT mode against DSR, consistent hashing and Maglev hashing, connection tracking and the problem of backend changes, health checks, IPVS and `nftables` load balancing, XDP load balancers (Katran-style), anycast and BGP, reverse proxies and connection pooling, TLS termination. Cross-reference the load-balancing design problem in `11_hld` | To write | 18 |
| 14 | `14_queueing_qos_and_traffic_management.md` | Queueing, QoS and traffic management | Queueing delay and Little's law applied to a link, buffer sizing and bufferbloat, tail drop, RED and ECN marking, CoDel and FQ-CoDel, token bucket and leaky bucket, policing against shaping, strict priority, DRR and WFQ, DSCP and 802.1p, `tc` qdiscs on Linux (`tbf`, `htb`, `fq_codel`, `cake`) | To write | 19 |
| 15 | `15_networking_interview_question_bank.md` | Networking interview question bank | Questions with answers across the track, a "network is slow" and "connection fails" triage tree, 10 to 15 troubleshooting scenarios reproduced in network namespaces (as in LX 14), design-style questions (load balancer, firewall data path, packet router), a one-page cheat sheet | To write | 24 |

---

## 4. Experiments and drills (inside the tutorials)

There are no separate lab folders. Each tutorial carries its own small runnable experiments and measured output, built in network namespaces so nothing touches the host network. Planned experiments by topic:

| Topic | What you run and observe |
|---|---|
| 01 | Two namespaces joined by a bridge; `tcpdump` shows ARP, MAC learning in `bridge fdb`, and a VLAN tag |
| 02 | A three-namespace routed chain; `traceroute` shows TTL expiry; a NAT rule translating two hosts that pick the same source port; twenty subnetting and LPM drills with answers; an LPM trie in C of about 60 lines |
| 03 | The handshake and teardown in `tcpdump`; `ss -ti` showing the congestion window; loss and delay with `tc netem` where the kernel has it (the sandbox VM does not, so the tutorial says which numbers were measured and which need your own Linux machine) |
| 04 | A blocking echo server, then one that survives partial I/O and `SIGPIPE`; a length-prefixed framing exercise |
| 05, 07 | The existing notes, rewritten with measured output |
| 06 | `nft monitor trace` through the hooks; reading `nstat` and `softnet_stat` while dropping packets |
| 08 | `dig +trace`, `curl -v` showing the TLS handshake, `openssl s_client` and a certificate chain |
| 09 | A stateful ruleset with `conntrack -L`; a SYN flood against a listening socket and the SYN-cookie counters |
| 11 | An ECMP hash experiment across several equal-cost paths; link failure and reconvergence timing in namespaces |
| 12 | Software RoCE (`rdma_rxe`) if the kernel has it, otherwise the verbs flow is explained from the API and marked as not run here |
| 13 | A small consistent-hashing table in C or Python, measuring disruption when a backend is removed |
| 14 | `tbf` and `fq_codel` against a loaded link, measuring latency under load |

Experiments that need hardware or kernel options the sandbox lacks are marked "from memory" or "needs your machine", never presented as measured.

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
| Cloudflare and Palo Alto Networks engineering blogs | DDoS, TLS and firewall internals in practice |

---

## 8. Progress

| # | Topic | Status |
|---|---|---|
| 01–09 | Core topics | 01 is next (week 10); 05 and 07 are rewritten when reached |
| 10 | Wi-Fi internals | Stretch |
| 11–14 | Fabrics, RDMA, load balancing, queueing | To write (added after the November 2026 audit) |
| 15 | Question bank | To write (week 24) |

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

**Left out on purpose:** MPLS and segment routing beyond a label-stack overview, SD-WAN, 5G core, wireless beyond 802.11 framing (NET 10 is the stretch), and BGP policy design at ISP scale. Add them only if a target team asks.
