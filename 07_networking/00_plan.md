# Networking and Communication: Track Plan

> **Track code:** NET · **Weeks:** 10–15 · **Budget:** ~25 hours · **Prerequisites:** OS 01–03 and 13 (system calls, processes, IPC basics); C 02–04 (pointers, structs, endianness)

This track follows a packet all the way down and back up: from Ethernet frames and IP routing, through TCP's state machine and congestion control, into the socket API and the Linux network stack, out to the protocols the web runs on, and finally into the security devices that inspect it all. Your Wi-Fi platform experience covers parts of L2 already; this track makes the rest as solid, and builds the network-security depth that security roles interview for.

"IPC and communication" is split across two tracks: local IPC (pipes, shared memory, signals, Unix sockets) is in `05_operating_systems` (OS 13 and 14); network communication is here.

---

## 1. Why this track matters

Networking questions arrive from several directions: "what happens when you type a URL" and TCP and HTTP behaviour in system design; "how does a packet move through a firewall" (stateful inspection, session tables, NAT, TLS decryption, intrusion prevention) in security roles; protocol depth (switching, routing, longest-prefix match, data plane versus control plane) in network-equipment roles; and RDMA, kernel bypass, zero-copy and NIC offloads in high-performance and GPU-cluster roles. Some online assessments include writing a socket server.
---

## 2. What you will be able to do after this track

1. Explain what happens, layer by layer, when you type a URL and press Enter, down to ARP and up to TLS.
2. Subnet by hand, explain longest-prefix match, and walk through NAT translating a connection.
3. Draw the TCP state machine, explain retransmission, flow control and congestion control (Reno, CUBIC, BBR), and diagnose TIME_WAIT and slow-start issues.
4. Write a correct, non-blocking TCP server in C with `epoll` that handles partial reads and writes, thousands of connections, and `SIGPIPE`.
5. Trace a packet through the Linux stack from NIC ring to socket and back, naming NAPI, GRO, netfilter, the routing decision and `sk_buff`.
6. Explain the TLS 1.3 handshake, DNS resolution, and HTTP/1.1 vs 2 vs 3.
7. Explain how a next-generation firewall works: session tables, App-ID-style classification, IDS/IPS signatures, TLS decryption, and DDoS defences such as SYN cookies.

---

## 3. Topics

Topics 05 and 07 are older, Staff-level notes; each is rewritten in the same detailed style as the new tutorials before you study it.

| # | File | Topic | Key subtopics | Status | Week |
|---|---|---|---|---|---|
| 01 | `01_network_models_and_layer2.md` | Network models and layer 2 | OSI vs TCP/IP, encapsulation, Ethernet frames, MAC addresses, switching and MAC learning, VLANs (802.1Q), ARP, MTU, spanning tree at a glance, 802.11 framing compared with Ethernet | To write | 10 |
| 02 | `02_ip_routing_and_layer3.md` | IP, routing and layer 3 | IPv4 and IPv6 headers, subnetting and CIDR by hand, routing tables and longest-prefix match, ICMP and traceroute, NAT and PAT, fragmentation and path MTU discovery, OSPF and BGP at a glance | To write | 11 |
| 03 | `03_tcp_udp_and_transport.md` | TCP, UDP and the transport layer | Ports and demultiplexing, UDP, TCP handshake and teardown, the state machine, sequence numbers, retransmission and RTO, flow control, congestion control (Reno, CUBIC, BBR), Nagle and delayed ACK, TIME_WAIT, QUIC | To write | 12 |
| 04 | `04_socket_programming_in_c.md` | Socket programming in C | The socket API end to end, a TCP echo server and client, UDP, byte order, partial reads and writes, framing a message stream, non-blocking sockets, `select` and `poll`, `SIGPIPE` and `SO_REUSEADDR`, a concurrent chat server | To write | 12 |
| 05 | [`05_epoll_and_io_uring.md`](05_epoll_and_io_uring.md) | `epoll` and `io_uring` | Readiness vs completion, level- vs edge-triggered `epoll`, `EPOLLONESHOT` and `EPOLLEXCLUSIVE`, `io_uring` submission and completion rings, choosing, measuring | Exists, rewrite due | 13 |
| 06 | `06_linux_network_stack.md` | The Linux network stack | RX path: NIC → DMA ring → interrupt → NAPI poll → GRO → netfilter → routing → TCP → socket queue; TX path; `sk_buff`; offloads (checksum, TSO, GSO, GRO, RSS, RPS); qdiscs; where packets are dropped and how to see it | To write | 13 |
| 07 | [`07_zero_copy_networking.md`](07_zero_copy_networking.md) | Zero-copy networking | The copies you remove, `AF_PACKET` rings, XDP, `AF_XDP`, DPDK, transmit-side zero-copy | Exists, rewrite due | 14 |
| 08 | `08_application_protocols_dns_http_tls.md` | Application protocols: DNS, HTTP and TLS | DNS resolution and caching, HTTP/1.1 (keep-alive, head-of-line blocking), HTTP/2 (streams, HPACK), HTTP/3 over QUIC, TLS 1.3 handshake, certificates and chains of trust, gRPC, WebSockets, MQTT for IoT | To write | 14 |
| 09 | `09_network_security_firewalls_ips_dpi.md` | Network security: firewalls, IPS and DPI | Stateless vs stateful firewalls, connection tracking and session tables, NAT in a firewall, application identification, IDS vs IPS, signature matching (Aho-Corasick, regex engines), TLS decryption and its trade-offs, IPsec and VPNs, DDoS defences, SYN cookies | To write | 15 |
| 10 | `10_wifi_80211_internals.md` | Wi-Fi (802.11) internals | Optional, your home turf: MAC layer, association and WPA3, aggregation, rate control, mac80211 and cfg80211. Write only if a target team asks | Stretch | — |

---

## 4. Labs

| Lab | Topics | What you build or observe |
|---|---|---|
| `labs/packet_capture/` | 01–03 | Capture an HTTPS page load with `tcpdump` and Wireshark; identify ARP, DNS, the TCP handshake, the TLS handshake and teardown |
| `labs/subnetting_drills/` | 02 | Twenty subnetting and longest-prefix-match exercises with answers |
| `labs/echo_server/` | 04 | A blocking TCP echo server and client in C, then a version that survives partial I/O and `SIGPIPE` (a common online-assessment task) |
| `labs/chat_server/` | 04, 05 | A non-blocking chat server with `epoll`, load-tested with 10,000 connections; precursor to Project 01 (`rkv`) |
| `labs/tcp_experiments/` | 03 | Use `tc netem` to add latency and loss; watch congestion-window behaviour with `ss -ti` under Reno, CUBIC and BBR |
| `labs/netfilter_trace/` | 06, 09 | Trace a packet through iptables/nftables hooks with `nft monitor trace`; build a small stateful ruleset with NAT |
| `labs/xdp_drop/` | 07 | An XDP program that drops packets matching a rule, measured against an nftables rule doing the same thing |

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

---

## 6. How to study this track

1. Follow the five-step method in [`../README.md`](../README.md).
2. Capture traffic for every protocol you study. Seeing the bytes in Wireshark fixes the header layouts in memory faster than any diagram.
3. Write the socket code yourself in C; it is a likely online-assessment task.
4. Topic 09 is the centre of the network-security preparation; read it together with [`../11_hld/03_systems_domain/01_data_plane_vs_control_plane.md`](../11_hld/03_systems_domain/01_data_plane_vs_control_plane.md) and design problem 18 (SSL decryption and NAT) in `11_hld`.

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
| Cloudflare and Palo Alto Networks engineering blogs | DDoS, TLS and firewall internals in practice |

---

## 8. Progress

| # | Topic | Status |
|---|---|---|
| 01–09 | Core topics | 01 is next (week 10); 05 and 07 are rewritten when reached |
| 10 | Wi-Fi internals | Stretch |

Cross-track view: [`../00_MASTER_PLAN.md`](../00_MASTER_PLAN.md).
