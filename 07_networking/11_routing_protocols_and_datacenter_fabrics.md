# NET 11: Routing protocols and datacenter fabrics

*Part of the [networking plan](00_plan.md) · Time: about 8 hours · You need: [NET 02](02_ip_routing_and_layer3.md) (longest prefix match, the routing table, OSPF and BGP at a glance), [NET 06](06_linux_network_stack.md) (the kernel FIB and what a forwarded packet costs), and the topology maths of fat trees, ECMP and incast in [CO 29](../04_computer_organization/29_interconnection_networks_and_warehouse_scale_computing.md), which this tutorial does not repeat. A router is a computer that keeps a map and forwards by it. This tutorial is about how the map is built and kept right while links fail: link state and distance vector, OSPF, BGP as the protocol of the internet and of the datacenter, the leaf-spine fabric and its arithmetic, equal-cost multipath and the ways its hash goes wrong, VXLAN and EVPN for the overlay, BFD and convergence, and how a route reaches the forwarding hardware. The routers are real: FRR daemons (`zebra`, `ospfd`, `bgpd`, `bfdd`) run in network namespaces, and every output below is pasted from a run.*

"Design the routing for a datacenter fabric" is asked at Google, Meta and Microsoft as a network design question, at Nvidia (Spectrum, SONiC) and Cisco as a protocol question, and at Palo Alto Networks as "what happens to the firewall's routes when a link flaps". A Staff level answer is not "run BGP". It is a **choice with reasons**: why BGP rather than OSPF in a Clos, how the AS numbers are laid out and why that stops paths that bounce off a leaf, what the hash does at each tier, what has to happen between a link failing and traffic moving, and which timer sets that time. This tutorial measures each of those where the VM can, and says plainly where it cannot.

**Eight honest limits, stated first.**

1. Everything is measured on **one Linux 6.18 VM** (Firecracker, 2 vCPUs) as **root**. Each router is a **network namespace**, each link a veth pair, and every daemon and every traffic generator shares the **same two CPUs**. Times here are for a **five router fabric** and have a resolution of about **5 ms** (the polling loop in `waitroute.py`); none is a product figure, and none says how a fabric of thousands of routers converges.
2. The routing software is **FRR 8.4.4** (`zebra`, `ospfd`, `bgpd`, `bfdd`) from the Ubuntu package. Cisco, Juniper, Arista and SONiC have their own defaults and engines (SONiC itself runs FRR). To keep each run short I set **OSPF hello 1 s and dead 3 s** and **BGP keepalive 1 s and hold 3 s**. The usual defaults (OSPF 10 s and 40 s on broadcast links, BGP 60 s and 180 s on many platforms) are **from memory** and differ by vendor.
3. The kernel has **no MPLS** (`zebra` printed `Disabling MPLS support (no kernel support)`), no `dummy` device (a veth pair stands in for a server subnet), and I did not test VRF. **MPLS, segment routing, LDP and VRF routing are from memory**, and everything is IPv4.
4. There is **no `netem`**, so no delay or loss was added. A failure is injected in only two ways: the link goes **down** (carrier lost, both ends see it at once) or an `nft` rule **drops everything** while the link stays up (a **silent** failure, which is the hard case).
5. **Hardware is from memory**: how an ASIC stores a FIB (TCAM and hash tables), SAI, the SONiC database pipeline, the ASIC's ECMP hash and its **resilient hashing**, **flowlet** switching, and EVPN multihoming (ESI, type 1 and 4 routes). The VXLAN and EVPN here run in the **Linux software bridge**.
6. The traffic is **single UDP packets from a small Python program**, one per flow, varying the source port. There is no TCP, no elephant flow, no queueing and no congestion. The kernel's hash is **not** a switch ASIC's hash: the distributions show what the **class** of algorithm does, not what a given vendor's function does.
7. Wire formats are shown only as far as `tshark` decoded them (OPEN capabilities, UPDATE attributes). The full list of BGP attribute codes, OSPF packet types and LSA types is **from memory**.
8. The fabric is **three leaves and two spines**. Scale effects (a million routes, update storms, route flap damping, convergence with 100,000 prefixes) are **not measured** and are called out where they matter.

By the end you can:

- **say why link state is used inside a network and what distance vector gets wrong** (measured: after a link failure two distance vector routers needed **15 exchanges** to count up to infinity, and split horizon with poison reverse stopped it in **2**);
- **read an OSPF adjacency and database** (measured: **5 router LSAs** for 5 routers and **0 network LSAs** on point-to-point links, a full SPF in **87 microseconds**, equal-cost routes with two next hops for **10.10.2.0/24** at cost **30**, and a second path that appeared only **15 s** after start because of the default LSA and SPF timers);
- **read a BGP session and an UPDATE** (measured: **2 OPEN, 8 UPDATE and 20 KEEPALIVE** messages on one link, the capabilities in the OPEN, and the `ORIGIN`, `AS_PATH`, `NEXT_HOP` and prefix in an UPDATE);
- **steer BGP and name the step that decided** (measured: with equal paths the winner was `best (Router ID)`, with local preference 200 it was `best (Local Pref)`, with one prepended AS number it was `best (AS Path)`, and each of the last two cut the kernel route from **2 next hops to 1**);
- **explain route reflection** (measured: with plain iBGP the second leaf learned **1 route** of 2, with the reflector it learned both, carrying `Originator: 10.255.0.1` and `Cluster list: 10.255.1.1`);
- **say why datacenters run BGP and why the spines share one AS number** (measured: after a spine to leaf link failed, OSPF sent traffic on a **four hop** valley path through another leaf at cost **50**, while BGP rejected that path through its own AS number and used the **two hop** path through the other spine);
- **size a leaf-spine fabric** (measured by arithmetic: 3,000 servers on 64 port switches at 3:1 oversubscription need **63 leaves, 16 spines, 1,008 cables**);
- **explain ECMP hashing and hash polarisation** (measured: with addresses only, **2,001 of 2,001** packets took one path; with the 5-tuple the split was **1,046 and 955**; with the same hash at the second tier **b1 sent all 1,046 flows to one next hop and b2 all 955 to the other**, so **half of the second tier links carried nothing**; different seeds restored **512, 504, 487 and 499**);
- **say what resilient hashing buys** (measured: removing one of four next hops moved **110 of 316** flows that were not on it with the ordinary group, and **0 of 303** with a resilient group);
- **compute VXLAN overhead and say where the entropy comes from** (measured: a 142 byte inner frame became a **178 byte** outer IP packet, **50 bytes** of overhead, a ping of **1,500 bytes of IP failed** and **1,450 worked**, and **393 distinct outer source ports** came from 397 captured flows);
- **show what EVPN changes** (measured: before any traffic BGP already carried the **flood list** as type 3 routes; one ping later it carried each host's MAC as a type 2 route and the bridge entry was marked `extern_learn`);
- **put numbers on convergence** (measured, time until the other leaf's route lost the failed path: **0.13 s** link down, **2.84 s** silent failure with a hold timer of 3 s, **0.94 s** with BFD at 300 ms times 3, **0.24 s** with BFD at 50 ms times 3, and **0.39 s** at 100 ms times 3).

## Where the numbers come from

Each experiment is a script `eN_*.sh` in `prog/` with its output frozen in `v/`; the answers to the problems are `ans_a` to `ans_e`. `frr.sh` starts one `zebra` and the routing daemons of a router inside its namespace, with its own configuration file and its own control sockets, and `vt` runs `vtysh` against one router. `fabric.sh` builds the fabric (`fab_up`) and starts BGP (`bgp_fabric`) or OSPF (`ospf_fabric`) on it. `ecmp.sh` builds the separate three tier path used for the hash experiments, with static equal-cost routes, so that nothing but the kernel's hash decides. `waitroute.py` polls one router's kernel route table until the number of next hops for a prefix reaches a target and prints the time since the fault, and `udpflows.py` sends one UDP packet from each of N source ports. Captures use `tcpdump --immediate-mode` and `tshark`.

Every number in the text is copied from `v/*.out` or computed from a printed figure, and the arithmetic is shown. A number that is not measured is marked **from memory**. The distribution counts are of 2,000 or 400 packets, so a count differs from the ideal half by the usual random spread (the standard deviation of 2,000 coin flips is about 22), and one extra packet in a count of 2,001 is a neighbour resolution packet, not a flow.

## Data and timing

| What | Size or time |
|---|---|
| Distance vector and link state (`dv.sh`) | 2 runs of a 2 router counting loop |
| OSPF on the fabric (`e1_ospf.sh`) | 5 routers, 6 links, 30 s settle, hello 1 s and dead 3 s |
| BGP on the fabric (`e2_bgp.sh`) | 5 routers, 6 sessions, a capture of one link, 10 s settle |
| Steering (`e3_bestpath.sh`) | local preference and AS path prepend, 3 stages |
| ECMP hash (`e4_ecmp_hash.sh`) | 6 namespaces, 4 cases of 2,000 flows each |
| Convergence (`e5_converge.sh`) | 6 trials: 2 protocols, 2 failure kinds, 3 detection settings, 25 s settle each |
| VXLAN (`e6_vxlan.sh`) | 2 VTEPs, 2 hosts, 400 flows, a capture and 200 route lookups |
| EVPN (`e7_evpn.sh`) | the same overlay with a control plane |
| Sizing (`sizing.sh`) | arithmetic only |
| Route reflection (`e9_rr.sh`) | 3 routers in one AS |
| Next hop groups (`e10_nexthop.sh`) | 4 next hops, 400 flows, ordinary and resilient group |
| Valley path with BGP (`e11_valley.sh`) | the failure of Problem E, with BGP |

---


## 1. The problem and the two families

A router needs, for every destination prefix, the **next hop** to use. On a handful of links you write them by hand (**static routes**). Past that, you let the routers find them, and the protocol has four jobs: find its **neighbours**, **exchange** what it knows, **compute** the best path, and **react** when a link or a router fails. Every routing protocol is a different answer to what is exchanged and who computes.

### 1.1 Distance vector

In a **distance vector** protocol each router tells its neighbours "I can reach X at distance d". A router picks, for each X, the neighbour whose advertised distance plus the cost of the link to it is smallest. This is the **Bellman-Ford** algorithm spread across routers, and it needs no map: a router knows only distances and the neighbour that gave each. **RIP** is the classic one, with a metric that counts hops and an infinity of 16. **BGP is a path vector protocol**: a distance vector protocol that attaches the **whole path** (the list of AS numbers) to each route, which is how it avoids the problem below (section 3).

The problem is **counting to infinity**. Take `A - B - C`, with C cut off from B. B's route to C has just gone, but A still advertises "C at 2, through B". B believes A has a path to C, so B adopts it at 3, and tells A. A had its route through B, so it adopts 4, and so on. `dv.py` runs that loop one advertisement at a time, with and without **split horizon with poison reverse** (a router advertises infinity for a route to the neighbour it learned it from).

```python
# dv.py: distance vector counting to infinity on the chain A - B - C, after the B-C link fails
INF = 16
def advert(me, to, split_horizon):
    metric, nexthop = me
    return INF if (split_horizon and nexthop == to) else metric
def run(split_horizon, steps=40):
    A = (2, "B")        # A reaches C through B at metric 2
    B = (INF, None)     # B has just lost its direct link to C
    trace = []
    for k in range(1, steps + 1):
        if k % 2 == 1:  # B hears A's advertisement
            m = min(advert(A, "B", split_horizon) + 1, INF)
            if B[1] in (None, "A") or m < B[0]: B = (m, "A") if m < INF else (INF, None)
        else:           # A hears B's advertisement
            m = min(advert(B, "A", split_horizon) + 1, INF)
            if A[1] == "B" or m < A[0]: A = (m, "B") if m < INF else (INF, None)
        trace.append((k, "B" if k % 2 else "A", A[0], B[0]))
        if A[0] >= INF and B[0] >= INF: break
    return trace
for sh in (False, True):
    print("split horizon with poison reverse:", "on" if sh else "off")
    print("  step  heard by  A's metric  B's metric")
    for k, who, a, b in run(sh):
        print("  %4d  %8s  %9s  %9s" % (k, who, a if a < INF else "16 inf", b if b < INF else "16 inf"))
```

```text
split horizon with poison reverse: off
  step  heard by  A's metric  B's metric
     1         B          2          3
     2         A          4          3
     3         B          4          5
     4         A          6          5
     5         B          6          7
     6         A          8          7
     7         B          8          9
     8         A         10          9
     9         B         10         11
    10         A         12         11
    11         B         12         13
    12         A         14         13
    13         B         14         15
    14         A     16 inf         15
    15         B     16 inf     16 inf
split horizon with poison reverse: on
  step  heard by  A's metric  B's metric
     1         B          2     16 inf
     2         A     16 inf     16 inf
```

Without split horizon the two routers **counted from 2 to 16 over 15 steps**, and each step is one advertisement, which in RIP is on a 30 s timer (from memory). All that time packets for C circulate between A and B until the TTL runs out. With split horizon, B refuses to take A's route back because A learned it from B: **2 steps**. Split horizon fixes the loop of two routers only. A loop through three or more routers can still count up, which is why RIP also needs **hold-down timers** and **triggered updates** (from memory), and why it converges in minutes.

### 1.2 Link state

In a **link state** protocol each router floods a small description of **its own links** (its neighbours and the cost of each), and every router collects all of them into the same **database**. Each router then runs **Dijkstra's algorithm** on that database, with itself as the root, and gets the shortest path tree. After a failure the router that saw it floods **one new description**, and every router recomputes. Nothing is counted up; the database is the same everywhere, so the paths are consistent. **OSPF** and **IS-IS** are link state.

| | Distance vector (RIP, and BGP with paths) | Link state (OSPF, IS-IS) |
|---|---|---|
| What is sent | distances to destinations (BGP: the path too) | the router's own links |
| Who knows the map | nobody; each router knows its neighbours' answers | every router, the same database |
| Computation | a comparison per update | Dijkstra per router, on a change |
| After a failure | updates ripple hop by hop (loops possible) | one flood, then all recompute |
| State | routes only | the whole topology of the area |
| Scale | BGP: the internet; RIP: small | one organisation, tens of areas |
| Policy | attached per route (BGP) | little; the metric decides |

The last row needs care: a path vector protocol sends each route as a separate message with attributes, so a policy can accept, reject or change one route. A link state protocol sends **topology**, and every router must agree on it, so a router cannot hide or change what it announces for one neighbour without breaking the shared map. That is why link state runs **inside** one organisation and BGP runs **between** organisations and, with care, inside a datacenter.

---


## 2. OSPF

**OSPF** (Open Shortest Path First, version 2 for IPv4, RFC 2328) is the standard link state protocol of enterprise and service provider networks. It runs directly on IP (protocol 89), not on TCP or UDP, and does its own reliable delivery.

### 2.1 The pieces

- **Neighbours.** Routers on a link send **Hello** packets every *hello interval* (10 s on broadcast links by default, from memory) and declare a neighbour dead after the *dead interval* (40 s, from memory) of silence. Two neighbours go through the states **Down, Init, 2-Way, ExStart, Exchange, Loading, Full** (from memory) and then are **adjacent**: they have swapped databases and keep them in step. The Hello is the **liveness check**, which makes the dead interval the failure detector (section 10).
- **Network types.** On a shared segment (`broadcast`) the routers elect a **designated router** (DR) and a backup, and everyone is adjacent only to those two, to cut the flooding. On a point-to-point link there is no election. The fabric uses `ip ospf network point-to-point` on every link, and the neighbour state in the output is `Full/-`: the dash is where `DR` or `Backup` would be.
- **LSAs.** A **link state advertisement** is one record of the database, identified by type, an ID and the router that originated it, with a **sequence number** and an **age**. A new version has a higher sequence number (`0x80000006` below), and its owner refreshes it every 30 minutes, since an LSA reaching an age of 3,600 s is discarded (from memory). The types are: 1 **router** (a router's links), 2 **network** (a broadcast segment, from the DR), 3 **summary** (a prefix from another area), 4 **ASBR summary**, 5 **external** (a prefix redistributed from outside), and 7 **NSSA external** (from memory). **Flooding** is reliable: an LSA is sent to every neighbour except the one it came from, retransmitted until acknowledged, and a router floods an LSA only if it is newer than the copy it holds.
- **Cost and SPF.** Each link has a **cost** (by default derived from bandwidth, from memory), and a router runs **Dijkstra** on the database to find the lowest total cost to every prefix. A tie gives **equal-cost** paths, and the router installs them all.
- **Areas.** Routers in one **area** share one database. Area 0 is the **backbone**; every other area attaches to it through an **area border router** (ABR), which passes **summaries** (type 3) and hides the inside of an area. Areas limit how far a change floods and how big the database for SPF is. A small fabric uses **one area**, as here.

### 2.2 OSPF on the leaf-spine fabric

`ospf_fabric` writes one configuration file per router: a `network` statement that puts the interfaces in area 0, the point-to-point type, and (with `fast`) a hello of 1 s and a dead interval of 3 s.

```bash
ospf_fabric() { # $1 = "fast" for hello 1 / dead 3, otherwise the defaults (10 / 40)
  local tim=""; [ "$1" = fast ] && tim=" ip ospf hello-interval 1
 ip ospf dead-interval 3"
  for j in 1 2; do
    { for i in 1 2 3; do echo "interface s${j}l$i"; echo " ip ospf network point-to-point"; [ -n "$tim" ] && echo "$tim"; done
      echo "router ospf"; echo " ospf router-id 10.255.1.$j"; echo " network 10.$j.0.0/16 area 0"; echo " network 10.255.1.$j/32 area 0"; } > /tmp/frr/s$j.conf
  done
  for i in 1 2 3; do
    { for j in 1 2; do echo "interface l${i}s$j"; echo " ip ospf network point-to-point"; [ -n "$tim" ] && echo "$tim"; done
      echo "router ospf"; echo " ospf router-id 10.255.0.$i"; echo " network 10.0.0.0/8 area 0"; } > /tmp/frr/l$i.conf
  done
  for r in s1 s2 l1 l2 l3; do frr_start $r ospfd; done
}
```

`e1_ospf.sh` builds the fabric, starts OSPF, waits 30 s and reads the state from leaf `l1`.

```bash
# e1: OSPF on the leaf-spine fabric: adjacencies, link state database, shortest path first, ECMP
. prog/fabric.sh
trap fab_down EXIT
fab_up; sleep 0.5
ospf_fabric fast 2>&1 | grep -v ZEBRA
sleep 30
echo "== 1. neighbours of leaf l1"
vt l1 -c 'show ip ospf neighbor'
echo "== 2. link state database seen by l1"
vt l1 -c 'show ip ospf database'
echo "== 3. routes l1 computed"
vt l1 -c 'show ip route ospf'
echo "== 4. kernel FIB of l1"
nse l1 ip route | grep -v kernel
echo "== 5. SPF runs"
vt l1 -c 'show ip ospf' | grep -iE 'spf|LSA'
```

**Neighbours and database.**

```text
== 1. neighbours of leaf l1

Neighbor ID     Pri State           Up Time         Dead Time Address         Interface                        RXmtL RqstL DBsmL
10.255.1.1        1 Full/-          29.205s            2.795s 10.1.1.0        l1s1:10.1.1.1                        0     0     0
10.255.1.2        1 Full/-          29.158s            2.851s 10.2.1.0        l1s2:10.2.1.1                        0     0     0

== 2. link state database seen by l1

       OSPF Router with ID (10.255.0.1)

                Router Link States (Area 0.0.0.0)

Link ID         ADV Router      Age  Seq#       CkSum  Link count
10.255.0.1     10.255.0.1        29 0x80000006 0xf459 6
10.255.0.2     10.255.0.2        25 0x80000006 0xd86d 6
10.255.0.3     10.255.0.3        21 0x80000006 0xbc81 6
10.255.1.1     10.255.1.1        27 0x80000007 0x70b2 7
10.255.1.2     10.255.1.2        25 0x80000007 0xa376 7

```

Leaf `l1` has **two** neighbours, both `Full`, one over each uplink, and the dead time counting down from 3 s shows the 1 s hello (`2.795s` left). The database holds **5 router LSAs, one per router**, and **no network LSAs**, because there is no broadcast segment and so no DR. The `Link count` is each router's links as it advertises them: a point-to-point link is **two entries** (the link to the neighbour and a stub for the subnet), so a leaf with two uplinks has `2 x 2 = 4`, plus its loopback and its server subnet: **6**; a spine with three links has `3 x 2 = 6` plus its loopback: **7**. All five routers hold **the same** LSAs, which is the point of link state.

**What l1 computed.**

```text
== 3. routes l1 computed
Codes: K - kernel route, C - connected, S - static, R - RIP,
       O - OSPF, I - IS-IS, B - BGP, E - EIGRP, N - NHRP,
       T - Table, v - VNC, V - VNC-Direct, A - Babel, F - PBR,
       f - OpenFabric,
       > - selected route, * - FIB route, q - queued, r - rejected, b - backup
       t - trapped, o - offload failure

O   10.1.1.0/31 [110/10] is directly connected, l1s1, weight 1, 00:00:30
O>* 10.1.2.0/31 [110/20] via 10.1.1.0, l1s1, weight 1, 00:00:24
O>* 10.1.3.0/31 [110/20] via 10.1.1.0, l1s1, weight 1, 00:00:24
O   10.2.1.0/31 [110/10] is directly connected, l1s2, weight 1, 00:00:30
O>* 10.2.2.0/31 [110/20] via 10.2.1.0, l1s2, weight 1, 00:00:24
O>* 10.2.3.0/31 [110/20] via 10.2.1.0, l1s2, weight 1, 00:00:24
O   10.10.1.0/24 [110/10] is directly connected, srv, weight 1, 00:00:30
O>* 10.10.2.0/24 [110/30] via 10.1.1.0, l1s1, weight 1, 00:00:15
  *                       via 10.2.1.0, l1s2, weight 1, 00:00:15
O>* 10.10.3.0/24 [110/30] via 10.1.1.0, l1s1, weight 1, 00:00:19
  *                       via 10.2.1.0, l1s2, weight 1, 00:00:19
O   10.255.0.1/32 [110/0] is directly connected, lo, weight 1, 00:00:30
O>* 10.255.0.2/32 [110/20] via 10.1.1.0, l1s1, weight 1, 00:00:15
  *                        via 10.2.1.0, l1s2, weight 1, 00:00:15
O>* 10.255.0.3/32 [110/20] via 10.1.1.0, l1s1, weight 1, 00:00:19
  *                        via 10.2.1.0, l1s2, weight 1, 00:00:19
O>* 10.255.1.1/32 [110/10] via 10.1.1.0, l1s1, weight 1, 00:00:24
O>* 10.255.1.2/32 [110/10] via 10.2.1.0, l1s2, weight 1, 00:00:24
```

The cost of every link is **10**. The route to `10.10.2.0/24`, the server subnet behind leaf `l2`, has metric **30**: `10 + 10` for the two links and `10` for the stub network at the end. There are **two** next hops because the paths through `s1` and `s2` cost the same, and the kernel FIB holds them as one **next hop group** (`nhid 25`, section 11):

```text
== 4. kernel FIB of l1
10.1.2.0/31 nhid 19 via 10.1.1.0 dev l1s1 proto ospf metric 20
10.1.3.0/31 nhid 19 via 10.1.1.0 dev l1s1 proto ospf metric 20
10.2.2.0/31 nhid 20 via 10.2.1.0 dev l1s2 proto ospf metric 20
10.2.3.0/31 nhid 20 via 10.2.1.0 dev l1s2 proto ospf metric 20
10.10.2.0/24 nhid 25 proto ospf metric 20
        nexthop via 10.1.1.0 dev l1s1 weight 1
        nexthop via 10.2.1.0 dev l1s2 weight 1
10.10.3.0/24 nhid 25 proto ospf metric 20
        nexthop via 10.1.1.0 dev l1s1 weight 1
        nexthop via 10.2.1.0 dev l1s2 weight 1
10.255.0.2 nhid 25 proto ospf metric 20
        nexthop via 10.1.1.0 dev l1s1 weight 1
        nexthop via 10.2.1.0 dev l1s2 weight 1
10.255.0.3 nhid 25 proto ospf metric 20
        nexthop via 10.1.1.0 dev l1s1 weight 1
        nexthop via 10.2.1.0 dev l1s2 weight 1
10.255.1.1 nhid 19 via 10.1.1.0 dev l1s1 proto ospf metric 20
10.255.1.2 nhid 20 via 10.2.1.0 dev l1s2 proto ospf metric 20
```

**What SPF cost.**

```text
== 5. SPF runs
 OSPF Routing Process, Router ID: 10.255.0.1
 Initial SPF scheduling delay 0 millisec(s)
 Minimum hold time between consecutive SPFs 50 millisec(s)
 Maximum hold time between consecutive SPFs 5000 millisec(s)
 SPF algorithm last executed 14.296s ago
 Last SPF duration 87 usecs
 SPF timer is inactive
 LSA minimum interval 5000 msecs
 LSA minimum arrival 1000 msecs
 Number of external LSA 0. Checksum Sum 0x00000000
 Number of opaque AS LSA 0. Checksum Sum 0x00000000
   SPF algorithm executed 8 times
   Number of LSA 5
   Number of router LSA 5. Checksum Sum 0x00039d6f
   Number of network LSA 0. Checksum Sum 0x00000000
   Number of summary LSA 0. Checksum Sum 0x00000000
   Number of ASBR summary LSA 0. Checksum Sum 0x00000000
   Number of NSSA LSA 0. Checksum Sum 0x00000000
   Number of opaque link LSA 0. Checksum Sum 0x00000000
   Number of opaque area LSA 0. Checksum Sum 0x00000000
```

The last SPF took **87 microseconds** for 5 routers, so the algorithm is never the problem at this size. Look instead at the other lines: `Initial SPF scheduling delay 0`, `Minimum hold time between consecutive SPFs 50`, `Maximum hold time 5000`, **`LSA minimum interval 5000 msecs`** and `LSA minimum arrival 1000`. SPF ran **8 times** during start-up, because each new LSA changed the database. The throttles are there to stop a flapping link from making every router run SPF without end: after the first run, the next waits `hold` ms, and the hold doubles up to the maximum while changes keep coming. The price is visible in the route ages above: the adjacencies were up within about **2 s** (hello 1 s), but the routes to the far leaves appeared **6 to 15 s after start** (the listing was taken at about 30 s and the ages are 24, 19 and 15 s), and the second path to `10.10.2.0/24` last of all. **OSPF's convergence is set by its throttle timers as much as by its hello.** Vendors tune the SPF delay to a few tens of milliseconds, and this is what "fast convergence" means in an OSPF design (from memory).

### 2.3 What to remember about OSPF in a Clos

OSPF works in a leaf-spine fabric and many fabrics ran it. Its three weaknesses are the reason the large datacenters left it:

1. **Every change goes everywhere.** One link flap floods an LSA to every router and makes every router run SPF. At thousands of routers that is a **fabric-wide event** for one cable. BGP can confine a change to the routers whose best path changed.
2. **It has little policy.** The metric decides, and there is no per-prefix way to say "never carry this prefix through that router".
3. **It does not know what a valley is.** Its shortest path may go up a spine, down into a leaf, and back up to another spine, if the costs say so. Section 6.3 measures a four hop path like that after a failure, where BGP chose the two hop path.

---


## 3. BGP

**BGP** (Border Gateway Protocol, version 4, RFC 4271) is the protocol of the internet: it carries reachability **between autonomous systems** (ASes, each with a number, and a network under one administration). It is a **path vector** protocol (section 1.1): each route carries the list of ASes it has crossed. Where OSPF discovers its neighbours by multicast and floods without being asked, BGP peers are **configured explicitly**, each pair talks over a **TCP connection on port 179**, and it sends only **changes**.

### 3.1 The session

A BGP session goes through the states **Idle, Connect, Active, OpenSent, OpenConfirm, Established** (from memory). Four message types carry everything:

| Message | Job |
|---|---|
| `OPEN` | first message each way: AS number, hold time, BGP identifier (router ID), and **capabilities** |
| `UPDATE` | announces routes (a prefix with its path attributes) and **withdraws** routes |
| `KEEPALIVE` | sent every *keepalive interval* so the peer knows the session is alive; the hold timer is the failure detector |
| `NOTIFICATION` | an error, after which the session closes |

`e2_bgp.sh` brings up eBGP on the fabric (section 6 explains the design) with a capture on the `l1` to `s1` link started **before** BGP, so the capture holds the whole session.

```bash
# e2: eBGP on the leaf-spine fabric: OPEN and UPDATE on the wire, best path, ECMP, loop prevention
. prog/fabric.sh
trap fab_down EXIT
fab_up; sleep 0.5
nse l1 tcpdump -i l1s1 -w /tmp/bgp.pcap --immediate-mode -U tcp port 179 2> /dev/null &
sleep 0.5
bgp_fabric "timers 1 3" 2>&1 | grep -v ZEBRA
sleep 10
pkill -x tcpdump; sleep 0.3
echo "== 1. session summary on l1"
vt l1 -c 'show bgp summary' | grep -E '^10\.|Neighbor'
echo "== 2. BGP message types on the l1 to s1 link, both directions"
tshark -r /tmp/bgp.pcap -Y bgp -T fields -e bgp.type 2> /dev/null | tr ',' '\n' | sort | uniq -c | sed "s/^ *//" | awk '{n=$2; t=(n==1?"OPEN":n==2?"UPDATE":n==3?"NOTIFICATION":n==4?"KEEPALIVE":n); print $1, t}'
echo "== 3. the OPEN message from l1"
tshark -r /tmp/bgp.pcap -Y 'bgp.type==1 && ip.src==10.1.1.1' -V 2> /dev/null | grep -E 'My AS|Hold time|BGP identifier|Capability: |Type: ' | sed 's/^ *//' | head -14
echo "== 4. an UPDATE from s1: attributes"
tshark -r /tmp/bgp.pcap -Y 'bgp.type==2 && ip.src==10.1.1.0' -V 2> /dev/null | grep -E 'Origin:|AS_PATH:|NEXT_HOP:|NLRI|prefix|Path attributes' | sed 's/^ *//' | head -12
echo "== 5. best path and multipath for 10.10.2.0/24 on l1"
vt l1 -c 'show bgp ipv4 unicast 10.10.2.0/24' | grep -vE 'Last update|Advertised|^  10\.'
echo "== 6. kernel FIB for it"
nse l1 ip route show 10.10.2.0/24
echo "== 7. what l1 sends to a spine (loop prevention)"
vt l1 -c 'show bgp ipv4 unicast neighbors 10.1.1.0 advertised-routes' | grep -E '^\*|Total'
vt s1 -c 'show bgp ipv4 unicast' | grep -E '^\*|Displayed'
```

### 3.2 On the wire

```text
== 1. session summary on l1
Neighbor        V         AS   MsgRcvd   MsgSent   TblVer  InQ OutQ  Up/Down State/PfxRcd   PfxSnt Desc
10.1.1.0        4      65000        15        15        0    0    0 00:00:10            4        6 N/A
10.2.1.0        4      65000        15        15        0    0    0 00:00:10            4        6 N/A
== 2. BGP message types on the l1 to s1 link, both directions
2 OPEN
8 UPDATE
20 KEEPALIVE
== 3. the OPEN message from l1
Type: IPv4 (0x0800)
Type: OPEN Message (1)
My AS: 65001
Parameter Type: Capability (2)
Capability: Multiprotocol extensions capability
Type: Multiprotocol extensions capability (1)
Parameter Type: Capability (2)
Capability: Route refresh capability (Cisco)
Type: Route refresh capability (Cisco) (128)
Parameter Type: Capability (2)
Capability: Route refresh capability
Type: Route refresh capability (2)
Parameter Type: Capability (2)
Capability: Enhanced route refresh capability
== 4. an UPDATE from s1: attributes
Path attributes
Origin: IGP (0)
Path Attribute - AS_PATH: 65000 65001
Path Attribute - NEXT_HOP: 10.1.1.0
Network Layer Reachability Information (NLRI)
NLRI prefix length: 24
NLRI prefix: 10.10.1.0
NLRI prefix length: 32
NLRI prefix: 10.255.0.1
Path attributes
Origin: IGP (0)
Path Attribute - AS_PATH: 65000 65002
```

In the 10 s since start, `l1` had sent **15 messages** and received **15** from each spine. The capture of the `l1` to `s1` link, both directions, holds **2 OPEN** (one each way), **8 UPDATE** and **20 KEEPALIVE**: with a 1 s keepalive each end sends one a second, and the rest of the traffic is the routes.

The **OPEN** from `l1` names its AS (`My AS: 65001`) and a list of **capabilities**: multiprotocol extensions, route refresh (the Cisco form, code 128, and the standard one, code 2) and enhanced route refresh. The **multiprotocol** capability is why one BGP session can carry IPv4 routes and, as in section 9.3, EVPN routes, without a new protocol. The hold time and the BGP identifier are also in the OPEN, and so is the 4 byte AS number capability (from memory; the filter in the script did not print them).

An **UPDATE** has two parts. The **path attributes**, which apply to every prefix in the message, and the **NLRI** (network layer reachability information), the list of prefixes. The one printed shows `Origin: IGP`, `AS_PATH: 65000 65001`, `NEXT_HOP: 10.1.1.0`, and the prefixes `10.10.1.0/24` and `10.255.0.1/32`. A second UPDATE announces another path (`AS_PATH: 65000 65002`) under its own attributes. **One UPDATE carries one set of attributes and many prefixes**, so routes with the same attributes are packed together. Also notice that `s1` announced `10.10.1.0/24`, **`l1`'s own prefix**, back to `l1`, with `AS_PATH: 65000 65001`. `l1` finds its own AS number, 65001, in that path and discards the route. This is the loop prevention of a path vector protocol, and section 6 uses it on purpose.

### 3.3 Path attributes

| Attribute | Kind | Meaning |
|---|---|---|
| `ORIGIN` | well-known, mandatory | how the route entered BGP: IGP, EGP or incomplete |
| `AS_PATH` | well-known, mandatory | the ASes crossed; a router **prepends its own AS** when it sends over eBGP; used for loop prevention and as a metric (shorter is better) |
| `NEXT_HOP` | well-known, mandatory | the address to forward to; changed to the sender's address on eBGP |
| `LOCAL_PREF` | well-known, discretionary | preference **inside one AS** (higher wins); carried over iBGP, never sent to another AS |
| `MED` | optional, non-transitive | a hint to a neighbouring AS about which entry to use (lower wins), compared only between paths from the same neighbouring AS |
| `COMMUNITY` | optional, transitive | a 32 bit tag a policy attaches or matches, such as "do not export" |
| `ORIGINATOR_ID`, `CLUSTER_LIST` | optional, non-transitive | added by route reflectors, section 5 |

All but the first three, and the ones shown in the captures and in section 5, are **from memory**.

### 3.4 eBGP and iBGP

A session between routers in **different** ASes is **eBGP**; between routers in the **same** AS it is **iBGP**. They differ in rules that matter:

- **AS_PATH.** eBGP prepends the sender's AS; iBGP does not. A router rejects a route whose `AS_PATH` already holds its own AS.
- **NEXT_HOP.** eBGP sets the next hop to the sender; **iBGP leaves it unchanged**, so inside an AS each router must be able to reach the next hop of every route, which is the job of an IGP (or, in a datacenter, of directly connected links).
- **LOCAL_PREF** is carried on iBGP and not on eBGP.
- **The iBGP rule: a route learned from an iBGP peer is not passed to another iBGP peer.** iBGP has no AS_PATH change to catch a loop, so this rule prevents one, at a cost: every router in an AS must peer with **every other** router (a full mesh of `n(n-1)/2` sessions) or use a route reflector (section 5).

### 3.5 Best path selection

For each prefix a router may hold several paths, from several peers. It picks **one best path** by comparing in order and stopping at the first difference (the exact list varies a little by vendor, and what follows is the common one, **from memory**, except where a step is measured below):

1. highest **weight** (a local, Cisco style setting);
2. highest **LOCAL_PREF**;
3. prefer a route the router **originated**;
4. shortest **AS_PATH**;
5. lowest **ORIGIN** (IGP before EGP before incomplete);
6. lowest **MED**, if from the same neighbouring AS;
7. prefer **eBGP** over iBGP;
8. lowest **IGP metric** to the next hop;
9. (eBGP) prefer the **oldest** path, for stability;
10. lowest **router ID** of the peer that sent it;
11. shortest **cluster list**, then lowest neighbour address.

FRR names the deciding step in `show bgp` (`best (Local Pref)`, `best (AS Path)`, `best (Router ID)`), which makes the list checkable. In the baseline run of `e2_bgp.sh` the two paths to `10.10.2.0/24` were equal in every step down to the router ID, and the output says so:

```text
== 5. best path and multipath for 10.10.2.0/24 on l1
BGP routing table entry for 10.10.2.0/24, version 3
Paths: (2 available, best #1, table default)
  65000 65002
    10.1.1.0 from 10.1.1.0 (10.255.1.1)
      Origin IGP, valid, external, multipath, best (Router ID)
  65000 65002
    10.2.1.0 from 10.2.1.0 (10.255.1.2)
      Origin IGP, valid, external, multipath
== 6. kernel FIB for it
10.10.2.0/24 nhid 12 proto bgp metric 20
        nexthop via 10.1.1.0 dev l1s1 weight 1
        nexthop via 10.2.1.0 dev l1s2 weight 1
```

Both paths are marked **multipath** and only one is marked `best`. The best path is what BGP **advertises** onward; the multipath set is what goes into the **FIB**. That is the difference between "best" and "ECMP" in BGP: with `maximum-paths 2` the router installs both paths in the kernel (`nexthop via 10.1.1.0 ...` and `nexthop via 10.2.1.0 ...`), and still announces just one. For paths to count as equal for multipath, the steps up to the IGP metric must tie, including the AS_PATH **content** in some implementations (a `multipath-relax` option compares only the length; from memory). In this fabric both spines are in one AS, so the paths are identical in every attribute and no relaxing is needed.

### 3.6 Update processing

A route goes through a fixed pipeline (from memory, the shape common to all implementations): an UPDATE enters the **Adj-RIB-In** of the peer; **inbound policy** accepts, rejects or changes it; the survivors go to the **Loc-RIB**, where the best path is chosen per prefix; the best path (and the multipath set) is handed to the routing table manager (`zebra` in FRR) and from there to the **FIB**; **outbound policy** filters what is advertised; and the result goes into the **Adj-RIB-Out** of each peer. A change in the best path produces an UPDATE (or a withdrawal) to every peer allowed to hear it. **A router that loses a path withdraws it, and so do the routers that depended on it, one hop at a time**: this is the ripple behind BGP's convergence time, and section 10 measures it.

---


## 4. Steering BGP with policy

BGP's value over a protocol where the metric decides is **policy**: the operator chooses which routes to accept, which to prefer and which to announce, with a **route map** attached to a neighbour in one direction. Two attributes cover most of what a datacenter does. **LOCAL_PREF** is the strongest lever inside an AS (it is step 2 of the selection, before the path length). **AS_PATH prepending** makes a path look longer, so it loses step 4 when the other steps tie, and it is the lever you use on what you **announce**, since it changes what **others** choose.

`e3_bestpath.sh` runs three stages on the fabric and prints the best path and the kernel route each time: the baseline, then a route map on `l1` that sets local preference 200 on everything from `s1`, then (with that removed) a route map on `s2` that prepends 65000 once on what it sends to `l1`.

```bash
# e3: steering BGP best path: local preference and AS path prepend, and what each does to ECMP
. prog/fabric.sh
trap fab_down EXIT
fab_up; sleep 0.5
bgp_fabric "timers 1 3" 2>&1 | grep -v ZEBRA
sleep 8
show() { vt l1 -c 'show bgp ipv4 unicast 10.10.2.0/24' | grep -E '^  [0-9]|from|Local|best' | sed 's/^ *//' | grep -vE 'Last|^10[0-9.]+ 10[0-9.]+$'; nse l1 ip route show 10.10.2.0/24; }
echo "== 1. baseline: two equal paths"
show
echo "== 2. l1 sets local preference 200 on routes from s1"
vt l1 -c 'conf t' -c 'route-map FROM_S1 permit 10' -c 'set local-preference 200' -c 'exit' -c 'router bgp 65001' -c 'address-family ipv4 unicast' -c 'neighbor 10.1.1.0 route-map FROM_S1 in' > /dev/null
vt l1 -c 'clear bgp ipv4 unicast 10.1.1.0 soft in' > /dev/null; sleep 2
show
echo "== 3. remove it; s2 prepends its AS once towards l1"
vt l1 -c 'conf t' -c 'router bgp 65001' -c 'address-family ipv4 unicast' -c 'no neighbor 10.1.1.0 route-map FROM_S1 in' > /dev/null
vt s2 -c 'conf t' -c 'route-map TO_L1 permit 10' -c 'set as-path prepend 65000' -c 'exit' -c 'router bgp 65000' -c 'address-family ipv4 unicast' -c 'neighbor 10.2.1.1 route-map TO_L1 out' > /dev/null
vt l1 -c 'clear bgp ipv4 unicast * soft in' > /dev/null; vt s2 -c 'clear bgp ipv4 unicast 10.2.1.1 soft out' > /dev/null; sleep 3
show
```

```text
== 1. baseline: two equal paths
Paths: (2 available, best #2, table default)
65000 65002
10.2.1.0 from 10.2.1.0 (10.255.1.2)
65000 65002
10.1.1.0 from 10.1.1.0 (10.255.1.1)
Origin IGP, valid, external, multipath, best (Router ID)
10.10.2.0/24 nhid 12 proto bgp metric 20
        nexthop via 10.1.1.0 dev l1s1 weight 1
        nexthop via 10.2.1.0 dev l1s2 weight 1
== 2. l1 sets local preference 200 on routes from s1
Paths: (2 available, best #2, table default)
65000 65002
10.2.1.0 from 10.2.1.0 (10.255.1.2)
65000 65002
10.1.1.0 from 10.1.1.0 (10.255.1.1)
Origin IGP, localpref 200, valid, external, best (Local Pref)
10.10.2.0/24 nhid 13 via 10.1.1.0 dev l1s1 proto bgp metric 20
== 3. remove it; s2 prepends its AS once towards l1
Paths: (2 available, best #2, table default)
65000 65000 65002
10.2.1.0 from 10.2.1.0 (10.255.1.2)
65000 65002
10.1.1.0 from 10.1.1.0 (10.255.1.1)
Origin IGP, valid, external, best (AS Path)
10.10.2.0/24 nhid 13 via 10.1.1.0 dev l1s1 proto bgp metric 20
```

1. **Baseline.** Two equal paths, the tie broken by router ID, both used: the kernel has **two next hops**.
2. **Local preference 200 on routes from `s1`.** The path through `s1` is `best (Local Pref)`, step 2, decided before the AS_PATH is even compared (both are `65000 65002`). The other path is no longer eligible for multipath, since multipath needs a tie up to the IGP metric, and local preference is earlier. The kernel route fell to **one next hop**, through `s1`.
3. **`s2` prepends its AS once.** The path through `s2` is now `65000 65000 65002`, one AS longer, and the path through `s1` wins at step 4: `best (AS Path)`. Again **one next hop**.

Two lessons. **Steering and ECMP pull against each other**: any policy that makes paths unequal removes the load balancing, so a fabric that wants ECMP keeps its policy **uniform**. And **the sender can shape what others choose** (prepending) while the **receiver** has the stronger lever (local preference), because local preference is compared first. A route map can also match one prefix and leave the others alone, which is Problem B.

---

## 5. iBGP and route reflectors

Section 3.4 gave the iBGP rule: a route learned from an iBGP peer is not passed to another iBGP peer. In an AS with `n` routers that means `n(n-1)/2` iBGP sessions if every router must learn every route: **45** for 10 routers, **4,950** for 100 (arithmetic). A **route reflector** (RR, RFC 4456) breaks the rule on purpose for **its clients**: it takes a route from one client and **reflects** it to the others (and to non-clients), so clients peer only with the reflector, `n` sessions instead of `n(n-1)/2`.

Two attributes stop loops the full mesh did not have. The reflector adds `ORIGINATOR_ID` (the router ID of the router that first injected the route into the AS), and a router that sees **its own** ID as the originator discards the route. And it prepends its **cluster ID** to a `CLUSTER_LIST`; a reflector that sees its own cluster ID in the list discards the route.

`e9_rr.sh` builds three routers in one AS, `l1 - s1 - l2`, where `s1` peers with both and the two leaves do not peer with each other. First plain iBGP, then `s1` marks both as clients.

```bash
# e9: iBGP and route reflection: l1 - s1 - l2 in one AS, with and without s1 acting as route reflector
. prog/fabric.sh
trap fab_down EXIT
fab_up; sleep 0.5
nse l2 ip route add 10.1.1.0/31 via 10.1.2.0       # l2 needs a path to l1's link address, the next hop of l1's routes
nse l1 ip route add 10.1.2.0/31 via 10.1.1.0
cat > /tmp/frr/s1.conf << 'CONF'
router bgp 65100
 bgp router-id 10.255.1.1
 no bgp network import-check
 neighbor 10.1.1.1 remote-as 65100
 neighbor 10.1.1.1 timers 1 3
 neighbor 10.1.2.1 remote-as 65100
 neighbor 10.1.2.1 timers 1 3
CONF
cat > /tmp/frr/l1.conf << 'CONF'
router bgp 65100
 bgp router-id 10.255.0.1
 no bgp network import-check
 neighbor 10.1.1.0 remote-as 65100
 neighbor 10.1.1.0 timers 1 3
 address-family ipv4 unicast
  network 10.10.1.0/24
 exit-address-family
CONF
cat > /tmp/frr/l2.conf << 'CONF'
router bgp 65100
 bgp router-id 10.255.0.2
 no bgp network import-check
 neighbor 10.1.2.0 remote-as 65100
 neighbor 10.1.2.0 timers 1 3
 address-family ipv4 unicast
  network 10.10.2.0/24
 exit-address-family
CONF
for r in s1 l1 l2; do frr_start $r bgpd 2>&1 | grep -v ZEBRA; done
sleep 8
echo "== 1. plain iBGP: s1 learns both prefixes but does not pass one client's route to the other"
vt s1 -c 'show bgp ipv4 unicast' | grep -E '^\*|^ *Displayed'
echo "-- l2's table:"
vt l2 -c 'show bgp ipv4 unicast' | grep -E '^\*|^ *Displayed'
echo "== 2. s1 marks both neighbours as route reflector clients"
vt s1 -c 'conf t' -c 'router bgp 65100' -c 'address-family ipv4 unicast' -c 'neighbor 10.1.1.1 route-reflector-client' -c 'neighbor 10.1.2.1 route-reflector-client' > /dev/null
sleep 4
echo "-- l2's table:"
vt l2 -c 'show bgp ipv4 unicast' | grep -E '^\*|^ *Displayed'
echo "-- the reflected route on l2:"
vt l2 -c 'show bgp ipv4 unicast 10.10.1.0/24' | grep -E 'Originator|Cluster|from|Origin|Paths' | sed 's/^ *//'
echo "== 3. s1 forwards with its own routes: the reflector is a router too, but it does not rewrite the next hops"
nse s1 ip route | grep bgp
echo "== 4. l2's kernel route to l1's prefix"
nse l2 ip route show 10.10.1.0/24
```

```text
== 1. plain iBGP: s1 learns both prefixes but does not pass one client's route to the other
*>i10.10.1.0/24     10.1.1.1                 0    100      0 i
*>i10.10.2.0/24     10.1.2.1                 0    100      0 i
Displayed  2 routes and 2 total paths
-- l2's table:
*> 10.10.2.0/24     0.0.0.0                  0         32768 i
Displayed  1 routes and 1 total paths
== 2. s1 marks both neighbours as route reflector clients
-- l2's table:
*>i10.10.1.0/24     10.1.1.1                 0    100      0 i
*> 10.10.2.0/24     0.0.0.0                  0         32768 i
Displayed  2 routes and 2 total paths
-- the reflected route on l2:
Paths: (1 available, best #1, table default)
10.1.1.1 from 10.1.2.0 (10.255.0.1)
Origin IGP, metric 0, localpref 100, valid, internal, best (First path received)
Originator: 10.255.0.1, Cluster list: 10.255.1.1
== 3. s1 forwards with its own routes: the reflector is a router too, but it does not rewrite the next hops
10.10.1.0/24 nhid 12 via 10.1.1.1 dev s1l1 proto bgp metric 20
10.10.2.0/24 nhid 11 via 10.1.2.1 dev s1l2 proto bgp metric 20
== 4. l2's kernel route to l1's prefix
10.10.1.0/24 nhid 13 via 10.1.2.0 dev l2s1 proto bgp metric 20
```

1. **Plain iBGP.** `s1` learned both prefixes, one from each leaf (`i` marks an internal path). But `l2`'s table has **one** route, its own: `s1` did not pass `10.10.1.0/24` on, because that route came from an iBGP peer and `l2` is an iBGP peer.
2. **With route-reflector clients.** `l2` now has `10.10.1.0/24` as an internal route with local preference 100, the default. The detail shows what was added: `Originator: 10.255.0.1` (`l1`'s router ID) and `Cluster list: 10.255.1.1` (`s1`'s, which is its router ID when no cluster ID is set). The `from 10.1.2.0 (10.255.0.1)` shows the session the route arrived on and, in brackets, the router ID of the originator.
3. **The next hop is unchanged.** The route's next hop on `l2` is `10.1.1.1`, `l1`'s link address, which `l2` cannot reach through any connected link. The script gave both leaves a static route to the other's link subnet through `s1` (the two `ip route add` lines at the top). Without that line `l2` would hold the route as **inaccessible** and not install it (from memory; I did not run it without). In a real AS this reachability is the IGP's job, which is why iBGP is run **on top of** an IGP, and why a route reflector that is not in the forwarding path (a **dedicated** reflector) still needs routes to every next hop.

Reflectors have their own failure modes (from memory): a reflector holds **one** best path per prefix and sends only that, so clients lose the **path diversity** a full mesh gave; a poorly placed reflector gives clients a path that is best for the reflector, not for them (a **suboptimal path**); and a loop in the reflector hierarchy is cut only by the cluster list. Production designs use **two reflectors in one cluster**, and the **ADD-PATH** extension (RFC 7911) to send more than one path.

---


## 6. BGP as the datacenter routing protocol

### 6.1 The design (RFC 7938)

Large datacenters moved from OSPF and IS-IS to **eBGP in the fabric**, a design written up in RFC 7938 ("Use of BGP for Routing in Large-Scale Data Centers", from memory). The reasons, from the weaknesses in section 2.3:

- **Contained changes.** A link failure produces updates only along the paths that used it, not a database flood to every router.
- **Policy** per prefix and per neighbour, which is how a fabric is drained for maintenance (set a community, prepend, or lower preference on one device and traffic moves off it).
- **One protocol** and one implementation, tested at internet scale, instead of an IGP plus BGP at the border.
- **No next hop problem.** Every session is between **directly connected** routers on a point-to-point link, so the next hop of each route is the neighbour itself and no IGP is needed to resolve it.

The **AS number plan** in the fabric built here is the common one:

- every **leaf** has its **own AS number** (`65001`, `65002`, `65003`);
- all **spines** share **one** AS number (`65000`);
- each leaf peers with every spine, and the spines do not peer with each other.

`bgp_fabric` writes it. The leaf announces its server subnet (`network 10.10.i.0/24`) and its loopback, `maximum-paths 2` puts both spine paths in the FIB, and `no bgp ebgp-requires-policy` turns off a safety feature of FRR (without it eBGP learns nothing until a policy is attached, from memory).

```bash
bgp_fabric() { # starts zebra+bgpd on every router; $1 = extra neighbor options (for example "timers 1 3" or "bfd")
  local opt=$1 daemons="bgpd"; case "$opt" in *bfd*) daemons="bgpd bfdd";; esac
  for j in 1 2; do
    { echo "router bgp 65000"; echo " bgp router-id 10.255.1.$j"; echo " no bgp ebgp-requires-policy"
      echo " no bgp network import-check"
      for i in 1 2 3; do echo " neighbor 10.$j.$i.1 remote-as 650$(printf %02d $i)"; local o; IFS="|"; for o in $opt; do echo " neighbor 10.$j.$i.1 $o"; done; unset IFS; done
      echo " address-family ipv4 unicast"; echo "  maximum-paths 2"; echo " exit-address-family"; } > /tmp/frr/s$j.conf.tmp
  done
  for i in 1 2 3; do
    { echo "router bgp 650$(printf %02d $i)"; echo " bgp router-id 10.255.0.$i"; echo " no bgp ebgp-requires-policy"
      echo " no bgp network import-check"
      for j in 1 2; do echo " neighbor 10.$j.$i.0 remote-as 65000"; local o; IFS="|"; for o in $opt; do echo " neighbor 10.$j.$i.0 $o"; done; unset IFS; done
      echo " address-family ipv4 unicast"; echo "  network 10.10.$i.0/24"; echo "  network 10.255.0.$i/32"; echo "  maximum-paths 2"; echo " exit-address-family"; } > /tmp/frr/l$i.conf.tmp
  done
  for r in s1 s2 l1 l2 l3; do
    case "$opt" in *bfd*) printf "bfd\n profile fast\n  detect-multiplier 3\n  transmit-interval ${BFD_MS:-50}\n  receive-interval ${BFD_MS:-50}\n" >> /tmp/frr/$r.conf.tmp;; esac
    mv /tmp/frr/$r.conf.tmp /tmp/frr/$r.conf; frr_start $r $daemons
  done
}
```

### 6.2 Why the spines share an AS

The shared spine AS is what gives **valley-free routing**. A valley path goes up to a spine, down to a leaf, and up to another spine: it bounces off a leaf, which is a router that should only be an endpoint of paths. In this design a valley path always has the spine AS number in it twice, and BGP discards a route with its own AS in the path. Section 3.2 already showed it happening:

```text
== 7. what l1 sends to a spine (loop prevention)
*> 10.10.1.0/24     0.0.0.0                  0         32768 i
*> 10.10.2.0/24     0.0.0.0                                0 65000 65002 i
*> 10.10.3.0/24     0.0.0.0                                0 65000 65003 i
*> 10.255.0.1/32    0.0.0.0                  0         32768 i
*> 10.255.0.2/32    0.0.0.0                                0 65000 65002 i
*> 10.255.0.3/32    0.0.0.0                                0 65000 65003 i
Total number of prefixes 6
*> 10.10.1.0/24     10.1.1.1                 0             0 65001 i
*> 10.10.2.0/24     10.1.2.1                 0             0 65002 i
*> 10.10.3.0/24     10.1.3.1                 0             0 65003 i
*> 10.255.0.1/32    10.1.1.1                 0             0 65001 i
*> 10.255.0.2/32    10.1.2.1                 0             0 65002 i
*> 10.255.0.3/32    10.1.3.1                 0             0 65003 i
Displayed  6 routes and 6 total paths
```

`l1` **offered** every spine six prefixes, including `10.10.2.0/24` with `AS_PATH 65000 65002` (the path it had learned from a spine, to which it prepends 65001 on the way out), but the table of `s1` holds **six paths** for six prefixes, one from each leaf. `l1` was offering `s1` routes that `s1` already had, and `s1` threw away the ones carrying its own AS number: **it never believes it can reach a leaf through another leaf**. (I count the discards by their effect, six routes and six paths; FRR did not print a discard counter here.)

If the spines had their own AS numbers, a leaf would accept a path like `65001 65003 65000 65002` and could use it as a backup that crosses a leaf, a transit role the leaf's links were never sized for.

### 6.3 What the shared AS does after a failure: BGP against OSPF

The test is a failure: cut the link between `s1` and `l2` and see which paths each protocol still finds from `l1` to the servers behind `l2`. The OSPF run is Problem E (section 15), which also raises the cost of one uplink to make the choice sharper. The BGP run is `e11_valley.sh`:

```bash
# e11: the same spine-to-leaf failure as Problem E, with BGP instead of OSPF
. prog/fabric.sh
trap fab_down EXIT
fab_up; sleep 0.5
bgp_fabric "timers 1 3" 2>&1 | grep -vE 'ZEBRA|BFD:'
sleep 8
echo "before: l1 to 10.10.2.0/24"; nse l1 ip route show 10.10.2.0/24
nse s1 ip link set s1l2 down
sleep 4
echo "after the s1-l2 link fails: l1 to 10.10.2.0/24"; nse l1 ip route show 10.10.2.0/24
echo "what s1 knows about 10.10.2.0/24:"; vt s1 -c 'show bgp ipv4 unicast 10.10.2.0/24' | head -3
echo "the path packets take:"; nse l1 traceroute -n -w 1 -q 1 -s 10.10.1.1 10.10.2.1 2>&1 | tail -n +2
```

```text
before: l1 to 10.10.2.0/24
10.10.2.0/24 nhid 12 proto bgp metric 20
        nexthop via 10.1.1.0 dev l1s1 weight 1
        nexthop via 10.2.1.0 dev l1s2 weight 1
after the s1-l2 link fails: l1 to 10.10.2.0/24
10.10.2.0/24 nhid 14 via 10.2.1.0 dev l1s2 proto bgp metric 20
what s1 knows about 10.10.2.0/24:
% Network not in table
the path packets take:
 1  10.2.1.0  0.125 ms
 2  10.10.2.1  0.090 ms
```

Before: **two** next hops. After: `l1` has **one**, through `s2`, and `s1` has `% Network not in table`: it **does not know** `10.10.2.0/24` any more, because the only other way to it was through `l1` or `l3`, with 65000 in the path, which it rejects. The traceroute shows the **two hop path**: `s2`, then the server. Compare with OSPF in Problem E: after the same failure, traffic went over **four hops**, through `s1`, `l3` and `s2`. OSPF found a path that existed. BGP found only the paths the design allows.

### 6.4 Details that matter in production (from memory)

- **ECMP across different AS paths.** If the spines had **different** AS numbers, the paths through them have different `AS_PATH` content, and multipath needs `bestpath as-path multipath-relax` (a length-only comparison) to use both.
- **Private AS numbers.** The 16 bit private range (64512 to 65534) is only 1,023 numbers, so large fabrics use the 32 bit private range or reuse numbers per pod with `allowas-in`.
- **Unnumbered interfaces.** BGP can run on the IPv6 link-local address of each link (`neighbor swp1 interface remote-as external`), and learn IPv4 routes with an IPv6 next hop (RFC 5549), which removes the need for `/31` addresses on every link.
- **Fast timers and BFD** for failure detection (section 10), and **graceful restart** so that a routing daemon restart does not drain the router.
- **Route aggregation** at the spine or super spine, to keep the FIB small; at the cost of black holes if a more specific path fails and the aggregate still attracts traffic.

---


## 7. Leaf-spine and Clos

### 7.1 The topology

A **leaf-spine** fabric has two tiers. Servers connect to **leaf** switches (often one at the top of each rack, a **ToR**), and **every leaf connects to every spine**; leaves never connect to leaves, nor spines to spines. The consequences, all visible in the fabric built here (three leaves, two spines, six links):

- **Any two servers on different leaves are the same distance apart**: server, leaf, spine, leaf, server. The path length is the same for all pairs, which is what lets software treat the datacenter as one flat pool.
- **The number of equal-cost paths between two leaves equals the number of spines.** Here `l1` has two next hops to `10.10.2.0/24` and to `10.10.3.0/24` (sections 2.2 and 3.5).
- **Capacity is added by adding spines** (more paths) or **leaves** (more racks), without touching the rest.

It is the folded form of a **Clos network**, Charles Clos's 1953 design for telephone exchanges: three stages of small switches that together are **non-blocking**, with no big central switch. A larger datacenter adds a tier: leaves and a middle tier (**aggregation** or **fabric** switches) form a **pod**, and pods are joined by a **super-spine** (a 5-stage Clos, or a 3-tier fat tree). The maths of the k-ary fat tree, its path counts and incast belongs to [CO 29](../04_computer_organization/29_interconnection_networks_and_warehouse_scale_computing.md) and is not repeated here; what follows is the arithmetic you need to **size** a fabric.

### 7.2 Oversubscription and sizing

A leaf has `K` ports. If `d` face servers and `u = K - d` face spines, the **oversubscription ratio** is `d : u`, the downlink bandwidth over the uplink bandwidth with equal port speeds. At `1:1` a rack can send everything out of the rack at once; at `3:1` each server has a third of its port speed to share when **all** of them send outside the rack at once (which is rare, and the reason the ratio is usually above 1). Each leaf uses one port on **each** spine, so the number of **spines is `u`** and the number of **leaves is at most `K`**, the port count of a spine. `sizing.py` tabulates it for 64 port switches of 100 Gbit/s:

```python
# sizing.py: leaf-spine arithmetic for switches with K ports of 100 Gb/s
K = 64
print("two tiers, %d-port switches: spines = uplinks per leaf; leaves = ports per spine = %d" % (K, K))
print("  down  up  ratio  spines  leaves  servers  server Gb/s if every server sends outside its rack")
for down in (32, 40, 48, 56):
    up = K - down
    print("  %4d  %2d  %3.1f:1  %6d  %6d  %7d  %6.1f" % (down, up, down / up, up, K, K * down, 100 * up / down))
print()
print("three tiers (fat tree, k = %d): servers k^3/4, switches 5k^2/4, no oversubscription" % K)
print("  servers  %d" % (K ** 3 // 4))
print("  switches %d  (%d edge + %d aggregation + %d core)" % (5 * K * K // 4, K * K // 2, K * K // 2, K * K // 4))
print("  equal-cost paths between servers in different pods: %d" % (K * K // 4))
print()
print("failure of one spine in a 2-tier fabric with 4 spines: capacity left = %d%%" % (100 * 3 // 4))
print("failure of one spine in 16 spines: capacity left = %.1f%%" % (100 * 15 / 16))
```

```text
two tiers, 64-port switches: spines = uplinks per leaf; leaves = ports per spine = 64
  down  up  ratio  spines  leaves  servers  server Gb/s if every server sends outside its rack
    32  32  1.0:1      32      64     2048   100.0
    40  24  1.7:1      24      64     2560    60.0
    48  16  3.0:1      16      64     3072    33.3
    56   8  7.0:1       8      64     3584    14.3

three tiers (fat tree, k = 64): servers k^3/4, switches 5k^2/4, no oversubscription
  servers  65536
  switches 5120  (2048 edge + 2048 aggregation + 1024 core)
  equal-cost paths between servers in different pods: 1024

failure of one spine in a 2-tier fabric with 4 spines: capacity left = 75%
failure of one spine in 16 spines: capacity left = 93.8%
```

Read the table across. `48` down and `16` up is `3:1`; it needs **16 spines**, supports **64 leaves** and **3,072 servers** (`64 x 48`), and a server that sends out of its rack with all of its neighbours gets `100 x 16 / 48 = 33.3 Gbit/s`. At `1:1` the same switches give only `2,048` servers on **32** spines. **Oversubscription buys servers**, and it is the first knob a designer turns: storage and AI training fabrics are `1:1` (all-to-all traffic), a web tier may be `3:1` or worse.

Two further numbers. With **two tiers** the fabric tops out at `K` leaves, so for more servers you add a tier: 3 tiers at `K = 64` is `K^3 / 4 = 65,536` servers on `5K^2 / 4 = 5,120` switches with `K^2 / 4 = 1,024` equal-cost paths between servers in different pods. And the fabric degrades **gracefully**: with 4 spines, one failing leaves **75 percent** of the capacity between leaves; with 16 spines, **93.8 percent**. More, smaller spines make each failure cheaper, which is one reason fabrics are wide.

---

## 8. ECMP: the hash that spreads flows

### 8.1 Why a hash

With several equal-cost next hops, a router must choose one for each packet. Sending packets in turn (round robin) balances perfectly and **reorders** packets of one TCP connection, since the paths differ in delay and queueing, and TCP reads reordering as loss (NET 03). So routers choose **per flow**: they compute a **hash** of the packet's fields, and the hash picks the next hop. All packets of a flow share the fields, so they share a path. Which fields are hashed is the first decision, and the second is what the hash is mixed with.

### 8.2 Which fields

Linux offers a choice (`net.ipv4.fib_multipath_hash_policy`): policy `0` hashes the **source and destination addresses**, and policy `1` the **5-tuple** (addresses, ports, protocol). Switch ASICs offer the same kind of choice. `ecmp.sh` builds a path through three tiers with static equal-cost routes, `a`, then `b1` or `b2`, then `c1` or `c2`, then `d`, so that no routing protocol is in the way.

```bash
# ecmp.sh: a three-tier path a -> {b1,b2} -> {c1,c2} -> d with static ECMP routes at the first two tiers
. prog/h.sh
EN="a b1 b2 c1 c2 d"
rxpkts() { nse "$1" cat /sys/class/net/$2/statistics/rx_packets; }
ecmp_down() { for n in $EN; do ip netns del $n 2> /dev/null; done; }
lk() { # ns1 if1 addr1 ns2 if2 addr2
  ip link add $2 netns $1 type veth peer name $5 netns $4
  nse $1 ip addr add $3/31 dev $2; nse $1 ip link set $2 up
  nse $4 ip addr add $6/31 dev $5; nse $4 ip link set $5 up
}
ecmp_up() {
  ecmp_down; sleep 0.2
  for n in $EN; do ip netns add $n; nse $n ip link set lo up; nse $n sysctl -qw net.ipv4.ip_forward=1; done
  lk a ab1 10.0.1.0 b1 b1a 10.0.1.1;  lk a ab2 10.0.2.0 b2 b2a 10.0.2.1
  lk b1 b1c1 10.1.1.0 c1 c1b1 10.1.1.1; lk b1 b1c2 10.1.2.0 c2 c2b1 10.1.2.1
  lk b2 b2c1 10.2.1.0 c1 c1b2 10.2.1.1; lk b2 b2c2 10.2.2.0 c2 c2b2 10.2.2.1
  lk c1 c1d 10.3.1.0 d dc1 10.3.1.1;  lk c2 c2d 10.3.2.0 d dc2 10.3.2.1
  nse d ip addr add 10.9.9.2/32 dev lo; nse a ip addr add 10.9.9.1/32 dev lo
  nse a ip route add 10.9.9.2/32 nexthop via 10.0.1.1 nexthop via 10.0.2.1
  nse b1 ip route add 10.9.9.2/32 nexthop via 10.1.1.1 nexthop via 10.1.2.1
  nse b2 ip route add 10.9.9.2/32 nexthop via 10.2.1.1 nexthop via 10.2.2.1
  nse c1 ip route add 10.9.9.2/32 via 10.3.1.1
  nse c2 ip route add 10.9.9.2/32 via 10.3.2.1
}
policy() { for n in a b1 b2; do nse $n sysctl -qw net.ipv4.fib_multipath_hash_policy=$1; done; }
seed() { for n in a b1 b2; do nse $n sysctl -qw net.ipv4.fib_multipath_hash_seed=$1; done; }
snap() { for p in "b1 b1a" "b2 b2a" "c1 c1b1" "c2 c2b1" "c1 c1b2" "c2 c2b2"; do set -- $p; echo $(rxpkts $1 $2); done; }
sendflows() { # N flows, one UDP packet each, source ports 20000..20000+N-1
  nse a python3 prog/udpflows.py 10.9.9.1 10.9.9.2 9 $1
}
```

`e4_ecmp_hash.sh` sends 2,000 UDP packets from `a` to `d`, each from a different source port, and counts the packets that arrive at each link from the receive counters. The packets all use **one source address** (`10.9.9.1`, bound explicitly).

```bash
# e4: how the kernel spreads flows over equal-cost paths, and what happens when two tiers hash alike
. prog/ecmp.sh
trap ecmp_down EXIT
ecmp_up
N=2000
run() { # label
  local b=($(snap)); sendflows $N; local e=($(snap)) i out=""
  for i in 0 1 2 3 4 5; do out="$out $((e[i] - b[i]))"; done
  set -- $out
  echo "$LABEL"
  printf '  tier 1  a>b1 %5d   a>b2 %5d\n' $1 $2
  printf '  tier 2  b1>c1 %4d  b1>c2 %4d  b2>c1 %4d  b2>c2 %4d\n' $3 $4 $5 $6
}
LABEL="== 1. policy 0 (hash on addresses only): all $N flows share one address pair"
policy 0; run
LABEL="== 2. policy 1 (hash on the 5-tuple), seeds left at their defaults"
policy 1; run
LABEL="== 3. same, but one seed value set explicitly in all three routers (like identical switch hardware)"
seed 12345; run
LABEL="== 4. one seed in a and different seeds in b1 and b2"
nse a sysctl -qw net.ipv4.fib_multipath_hash_seed=12345
nse b1 sysctl -qw net.ipv4.fib_multipath_hash_seed=777
nse b2 sysctl -qw net.ipv4.fib_multipath_hash_seed=999; run
```

```text
== 1. policy 0 (hash on addresses only): all 2000 flows share one address pair
  tier 1  a>b1  2001   a>b2     0
  tier 2  b1>c1 2001  b1>c2    0  b2>c1    0  b2>c2    0
```

With **addresses only**, all **2,001** packets took **one** path at each tier: the 2,000 flows have the same address pair, so the same hash. (The extra packet is most likely the ARP request for the first hop, counted by the receiving interface; I did not capture it.) This is the elephant flow problem in its pure form, and also the reason for policy `1` in a datacenter: the traffic between two servers is a few addresses and many ports.

### 8.3 Hash polarisation

```text
== 2. policy 1 (hash on the 5-tuple), seeds left at their defaults
  tier 1  a>b1  1046   a>b2   955
  tier 2  b1>c1 1046  b1>c2    0  b2>c1    0  b2>c2  955
== 3. same, but one seed value set explicitly in all three routers (like identical switch hardware)
  tier 1  a>b1  1015   a>b2   985
  tier 2  b1>c1 1015  b1>c2    0  b2>c1    0  b2>c2  985
== 4. one seed in a and different seeds in b1 and b2
  tier 1  a>b1  1015   a>b2   985
  tier 2  b1>c1  512  b1>c2  504  b2>c1  487  b2>c2  499
```

With the 5-tuple, tier 1 split the flows **1,046 and 955** (the spread of 2,000 coin flips has a standard deviation of about 22, so this is two deviations from even, plausible). Now look at tier 2. `b1` received 1,046 flows and sent **all of them** to `c1`: `b1>c1 1,046, b1>c2 0`. And `b2` sent **all** 955 to `c2`. **Half of the second tier links carried nothing.** This is **hash polarisation**: `a`, `b1` and `b2` all compute the same function of the same 5-tuple, so a flow that `a` sent to `b1` (hash on one side of the split) is again on the same side of the split at `b1`. Every router makes the same choice for the same flow, and the choices are correlated down the path instead of independent.

Case 3 sets one seed value on all three routers, as identical switch hardware does with its default seed: still polarised (`1,015` and `985`, then everything on one link each). Case 4 gives `b1` and `b2` **different seeds** from `a` and each other, and the second tier splits evenly: **512, 504, 487 and 499** of 2,000 (ideal 500).

The fix in practice is a **different hash per tier** (a per-switch seed, a different subset of hash bits, or mixing the **ingress port** or a switch ID into the hash) so the choices are independent. Linux's `net.ipv4.fib_multipath_hash_seed` is that seed; the same experiment run only with default seeds polarised, which tells you that in this kernel **the default seeds are the same on every namespace** (the output is the evidence; I did not read the kernel source for why). Problem C shows that reseeding **one** of the two tier 2 routers fixes only that router.

### 8.4 Imbalance without a bug

Even with a good hash, a few flows balance badly. Two flows on two paths collide with probability `1/2`; two flows on four paths, `1/4`; and a data center flow is not a packet but a **transfer**, which may be a gigabyte. Hashing balances the **number of flows**, not the **bytes**, so a handful of large flows (storage replication, an AI training all-reduce) on a few paths load some links twice as much as others, and nothing in the router knows. The two answers are **more flows** (spread one transfer over many connections, which is what a multipath transport does) and **better path selection** (section 8.6).

### 8.5 Weighted and resilient groups

The next hop list is a **group** in the kernel (`ip nexthop`), and what happens when a member **leaves** matters, because a link failure removes one. With the simple method, the hash space is cut into equal ranges, one per member (**hash threshold**, from memory), and removing one member moves the **boundaries**, so flows that were on the healthy members move too. A **resilient** group fixes a table of **buckets**, the hash picks a bucket, each bucket points to a member, and removing a member reassigns **only its buckets**. `e10_nexthop.sh` removes member 4 of four from each kind of group and counts the flows (out of 400 different source ports) that were **not** on member 4 and still changed member.

```bash
# e10: next hop groups in the kernel FIB: what removing one member does to the flows on the others
. prog/h.sh
trap 'ip netns del x 2> /dev/null' EXIT
ip netns del x 2> /dev/null; ip netns add x; nse x ip link set lo up
nse x sysctl -qw net.ipv4.fib_multipath_hash_policy=1
for k in 1 2 3 4; do
  nse x ip link add x$k type veth peer name y$k; nse x ip link set y$k up
  nse x ip addr add 10.9.$k.1/24 dev x$k; nse x ip link set x$k up
  nse x ip nexthop add id $k via 10.9.$k.2 dev x$k
done
pick() { # print the member chosen for flows 1..N, one digit per flow
  for p in $(seq 30001 30400); do nse x ip route get 10.50.0.1 ipproto udp sport $p dport 9 | head -1 | grep -oE 'dev x[1-4]' | grep -oE '[1-4]$'; done | tr -d '\n'
}
count_moves() { # before after: flows not on member 4 before that changed member
  python3 - "$1" "$2" << 'PY'
import sys
b, a = sys.argv[1], sys.argv[2]
on4 = sum(1 for x in b if x == "4")
healthy = [(x, y) for x, y in zip(b, a) if x != "4"]
moved = sum(1 for x, y in healthy if x != y)
print("flows on member 4 before: %d of %d; flows on members 1 to 3 that changed member: %d of %d" % (on4, len(b), moved, len(healthy)))
PY
}
echo "== 1. ordinary group of four (hash threshold): remove member 4"
nse x ip nexthop add id 10 group 1/2/3/4; nse x ip route add 10.50.0.0/24 nhid 10
B=$(pick); nse x ip nexthop replace id 10 group 1/2/3; A=$(pick); count_moves "$B" "$A"
echo "== 2. resilient group of four (32 buckets): remove member 4"
nse x ip route del 10.50.0.0/24; nse x ip nexthop del id 10
nse x ip nexthop add id 20 group 1/2/3/4 type resilient buckets 32 idle_timer 0; nse x ip route add 10.50.0.0/24 nhid 20
nse x ip nexthop show id 20 | head -3
B=$(pick); nse x ip nexthop replace id 20 group 1/2/3 type resilient; A=$(pick); count_moves "$B" "$A"
echo "buckets after the change:"; nse x ip nexthop bucket show id 20 | awk '{print $NF}' | sort | uniq -c | head
```

```text
== 1. ordinary group of four (hash threshold): remove member 4
flows on member 4 before: 84 of 400; flows on members 1 to 3 that changed member: 110 of 316
== 2. resilient group of four (32 buckets): remove member 4
id 20 group 1/2/3/4 type resilient buckets 32 idle_timer 0 unbalanced_timer 0 unbalanced_time 0
flows on member 4 before: 97 of 400; flows on members 1 to 3 that changed member: 0 of 303
buckets after the change:
     11 1
     10 2
     11 3
```

With the ordinary group, **110 of 316** flows (35 percent) that did nothing wrong **changed path**. Each is a TCP flow that sees a burst of reordering, or a connection that a stateful device on the new path has never seen. With the resilient group (32 buckets), **0 of 303** moved; the 97 flows on the removed member were redistributed, and the final bucket count is **11, 10 and 11** across the three members left (32 buckets over 3 members, one short of even). The cost is that the buckets stay where they are when the member **returns**, so the group may stay unbalanced until it is rebalanced (the `idle_timer` setting makes it wait for idle buckets; from memory). Switch ASICs offer **resilient hashing** for the same reason (from memory).

### 8.6 Flowlets and adaptive routing (from memory)

A **flowlet** is a burst of packets of one flow, separated from the next burst by a gap longer than the difference in delay between the paths. A gap that long means the next burst can take a **different path** without arriving before the previous one: the flow is rebalanced, with no reordering. Some switches rehash on each flowlet (CONGA is the paper). **Adaptive routing** goes further and picks the least loaded path using queue depth, per packet or per flowlet, and relies on the **receiver** (a NIC) to put packets back in order; this is how fabrics for RDMA traffic, where a few large flows are the whole load, get better than hashing. None of it was run here: no queues, no congestion and no ASIC.

---


## 9. VXLAN and EVPN: an overlay on the fabric

### 9.1 Why an overlay

The fabric of section 6 is a **layer 3** network: every link a subnet, every leaf a router. Servers and virtual machines often want a **layer 2** network that follows them: the same subnet in two racks, a VM that keeps its IP address when it moves. An **overlay** gives that. Traffic between two hosts is wrapped in a packet that the fabric routes as ordinary IP between two **VTEPs** (VXLAN tunnel endpoints), usually the leaves, and unwrapped at the far end. The fabric sees only UDP between leaf loopbacks, so it stays simple and routed, and a **network identifier** in the wrapper keeps tenants apart.

**VXLAN** (RFC 7348) is the wrapper: the original **Ethernet frame** goes inside a **UDP datagram** to port **4789**, behind an 8 byte header that holds a **24 bit VNI** (VXLAN network identifier, so about 16 million segments against the 4,094 of a VLAN).

### 9.2 The wrapper and its cost

`e6_vxlan.sh` builds the overlay between leaves `l1` and `l2`. Each leaf has a bridge `br100` with a VXLAN device `vxlan100` (VNI 100, local address its loopback, UDP port 4789) and a host attached by a veth pair, and the loopbacks are reachable through the BGP fabric of section 6. The only overlay state at first is a **flood list**: a permanent forwarding entry for the all-zero MAC address that sends broadcast and unknown frames to the other VTEP.

```bash
# e6: a VXLAN overlay between leaves l1 and l2 over the BGP underlay: header cost, MAC learning, ECMP entropy
. prog/fabric.sh
trap 'fab_down; ip netns del h1 2> /dev/null; ip netns del h2 2> /dev/null' EXIT
fab_up; sleep 0.5
bgp_fabric "timers 1 3" 2>&1 | grep -vE 'ZEBRA|BFD:'
for h in 1 2; do ip netns add h$h; nse h$h ip link set lo up; done
for i in 1 2; do
  nse l$i ip link add vxlan100 type vxlan id 100 local 10.255.0.$i dstport 4789 nolearning 2> /dev/null || nse l$i ip link add vxlan100 type vxlan id 100 local 10.255.0.$i dstport 4789
  nse l$i ip link add br100 type bridge; nse l$i ip link set vxlan100 master br100
  ip link add h$i netns h$i type veth peer name hl$i netns l$i
  nse l$i ip link set hl$i master br100
  for d in vxlan100 br100 hl$i; do nse l$i ip link set $d up; done
  nse h$i ip addr add 192.168.100.$i/24 dev h$i; nse h$i ip link set h$i up
done
# learning on, flood list: unknown and broadcast frames are copied to the other VTEP
nse l1 ip link set vxlan100 type vxlan learning 2> /dev/null; nse l2 ip link set vxlan100 type vxlan learning 2> /dev/null
nse l1 bridge fdb append 00:00:00:00:00:00 dev vxlan100 dst 10.255.0.2
nse l2 bridge fdb append 00:00:00:00:00:00 dev vxlan100 dst 10.255.0.1
sleep 6
echo "== 1. MTU: the underlay is 1500, VXLAN adds 50 bytes"
echo "MTUs after the devices were joined to the bridge:"
for d in "h1 h1" "l1 hl1" "l1 vxlan100" "l1 br100"; do set -- $d; printf '  %-4s %-9s %s\n' $1 $2 "$(nse $1 cat /sys/class/net/$2/mtu)"; done
for sz in 1472 1422; do printf 'ping -M do -s %s (inner IP packet %s bytes): ' $sz $((sz + 28)); nse h1 ping -M do -s $sz -c 2 -i 0.2 -W 1 192.168.100.2 | grep -oE '[0-9]+% packet loss'; done
echo "== 2. h1 pings h2 across the fabric"
nse h1 ping -c 3 -i 0.2 -W 1 192.168.100.2 | tail -2
echo "== 3. what l1 learned"
nse l1 bridge fdb show dev vxlan100 | grep -v permanent
nse l1 bridge fdb show dev vxlan100 | grep 00:00:00:00:00:00
echo "== 4. outer IP length and UDP length of one frame carrying a 100 byte UDP payload (inner frame: 14 + 20 + 8 + 100 = 142 bytes)"
nse l1 tcpdump -i any -w /tmp/vx.pcap --immediate-mode -U 'udp port 4789' 2> /dev/null &
sleep 0.5
nse h1 python3 prog/udpflows.py 192.168.100.1 192.168.100.2 9 1 2> /dev/null
nse h1 python3 -c "
import socket; s=socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(('192.168.100.1', 30000)); s.sendto(b'y'*100, ('192.168.100.2', 9))"
sleep 0.5; pkill -x tcpdump; sleep 0.3
tshark -r /tmp/vx.pcap -Y 'udp.dstport==4789 && vxlan && udp.length>100 && ip.src==10.255.0.1' -T fields -e ip.len -e udp.length -e vxlan.vni 2> /dev/null | head -1

rx() { nse $1 cat /sys/class/net/$2/statistics/rx_packets; }
spread() { # label
  local a=$(rx s1 s1l1) b=$(rx s2 s2l1)
  nse l1 tcpdump -i any -w /tmp/vx2.pcap --immediate-mode -U 'udp port 4789 and src host 10.255.0.1' 2> /dev/null &
  sleep 0.5; nse h1 python3 prog/udpflows.py 192.168.100.1 192.168.100.2 9 400 2> /dev/null; sleep 0.5; pkill -x tcpdump; sleep 0.3
  printf '%-40s via s1: %4d   via s2: %4d   outer source ports: %s distinct in %s captured\n' "$1" $(( $(rx s1 s1l1) - a )) $(( $(rx s2 s2l1) - b )) "$(tshark -r /tmp/vx2.pcap -T fields -e udp.srcport -Y 'vxlan' 2> /dev/null | cut -d, -f1 | sort -u | wc -l)" "$(tshark -r /tmp/vx2.pcap -Y 'vxlan' 2> /dev/null | wc -l)"
}
echo "== 5. 400 inner UDP flows from h1 to h2, differing only in inner source port"
nse l1 sysctl -qw net.ipv4.fib_multipath_hash_policy=1; spread "underlay hash policy 1 (5-tuple)"
echo "== 6. outer UDP source ports chosen for five different inner flows (inner source ports 20000 to 20004)"
tshark -r /tmp/vx2.pcap -T fields -e udp.srcport -Y 'vxlan' 2> /dev/null | head -5 | tr '\n' ' '; echo
echo "== 7. if l1 hashed the outer header: ask the routing table about the first 200 captured outer source ports"
for p in $(tshark -r /tmp/vx2.pcap -T fields -e udp.srcport -Y 'vxlan' 2> /dev/null | cut -d, -f1 | head -200); do
  nse l1 ip route get 10.255.0.2 from 10.255.0.1 ipproto udp sport $p dport 4789 | head -1 | grep -oE 'dev l1s[12]'
done | sort | uniq -c
```

```text
== 1. MTU: the underlay is 1500, VXLAN adds 50 bytes
MTUs after the devices were joined to the bridge:
  h1   h1        1500
  l1   hl1       1500
  l1   vxlan100  1500
  l1   br100     1500
ping -M do -s 1472 (inner IP packet 1500 bytes): ping: local error: message too long, mtu=1450
100% packet loss
ping -M do -s 1422 (inner IP packet 1450 bytes): 0% packet loss
== 2. h1 pings h2 across the fabric
3 packets transmitted, 3 received, 0% packet loss, time 407ms
rtt min/avg/max/mdev = 0.047/0.078/0.100/0.022 ms
== 3. what l1 learned
e6:cd:d7:13:76:c1 master br100
e6:cd:d7:13:76:c1 dst 10.255.0.2 self
00:00:00:00:00:00 dst 10.255.0.2 self permanent
== 4. outer IP length and UDP length of one frame carrying a 100 byte UDP payload (inner frame: 14 + 20 + 8 + 100 = 142 bytes)
178,128 158,108 100
```

- **The overhead.** The inner frame is an Ethernet header (14), IP (20), UDP (8) and 100 bytes: **142 bytes**. `tshark` printed the outer and the inner value of each field: the outer packet has an IP length of **178** (the inner one is `20 + 8 + 100 = 128`), a UDP length of **158** (inner 108) and `vni 100`. So `178 - 142 = 36` bytes of IP packet added: outer IP **20**, outer UDP **8** and the VXLAN header **8**; with the **14 byte outer Ethernet header** on the wire, VXLAN costs **50 bytes** per packet, which is why a 1,500 byte link carries **1,450** bytes of inner IP packet.
- **The MTU.** The hosts have an MTU of 1,500 and so does the underlay; all four devices on `l1` report 1,500, since the VXLAN device did not lower its own MTU (it had no lower device to read it from), but the limit applied when it transmitted. A ping with a 1,472 byte payload (**1,500 byte** IP packet, don't fragment set) failed with **100 percent loss**, and the host was told `message too long, mtu=1450` (the leaf evidently sent back an ICMP "fragmentation needed" with the limit, which the host cached; I inferred that from the message and did not capture the ICMP); a payload of 1,422 (**1,450** byte packet) passed. The fix is a **jumbo frame underlay** (the fabric at 9,000 or more, the usual practice) so the overlay keeps its 1,500, or an inner MTU of 1,450.
- **Learning.** After the ping, `l1` knows the MAC address of the far host as `dst 10.255.0.2` on `vxlan100`, learned from traffic (**flood and learn**, as in an ordinary switch). The flood entry `00:00:00:00:00:00 dst 10.255.0.2 permanent` is a **head-end replication** list: the sending leaf copies each broadcast to every VTEP on the list, one unicast packet each. Real fabrics use that, or **multicast** in the underlay (from memory).

### 9.3 Entropy for the underlay's ECMP

The fabric hashes the **outer** headers (section 8), and for every VXLAN packet between two leaves these are the same two loopback addresses, the same protocol and the same destination port 4789. If the source port were fixed too, **all traffic between two leaves would take one path**. The VXLAN sender takes care of it: it sets the outer **source port from a hash of the inner packet's headers**, so one inner flow has one outer source port, and different flows have different ones.

```text
== 5. 400 inner UDP flows from h1 to h2, differing only in inner source port
underlay hash policy 1 (5-tuple)         via s1:  402   via s2:    4   outer source ports: 393 distinct in 397 captured
== 6. outer UDP source ports chosen for five different inner flows (inner source ports 20000 to 20004)
54158,20000 32996,20001 33638,20002 53366,20003 37092,20004
== 7. if l1 hashed the outer header: ask the routing table about the first 200 captured outer source ports
    108 dev l1s1
     92 dev l1s2
```

400 inner flows differing only in their source port got **393 distinct outer source ports** in 397 captured packets (the capture, on a VM with two vCPUs, missed three of the 400). Inner source ports `20000` to `20004` became `54158`, `32996`, `33638`, `53366`, `37092`: the outer port is a hash, in the ephemeral range. **That is the entropy a switch hashes.** The last section of the output asks the routing table of `l1` where each of 200 captured outer ports would go: **108 through `l1s1` and 92 through `l1s2`**, so a router that hashes on the outer 5-tuple spreads these flows.

But look at section 5 of the output: all **402** of the real packets left through `s1` and **4** through `s2` (the 4 are most likely BGP keepalives). **The Linux software VXLAN device did not spread them**, although the outer ports differed and the route lookup would have. I did not find out why in this run. The likely reason is that the driver caches the route to each remote VTEP and reuses it, so the first lookup fixes the path for every flow (**from memory, not verified here**). It is a property of this software VTEP and not of the design: a hardware VTEP computes the path per packet from the outer hash. It is worth knowing as a trap whenever you test fabric balance with a Linux VTEP at the edge.

### 9.4 EVPN: BGP as the control plane

Flood and learn has the costs of a classic switch: broadcast flooding, MAC tables learned from traffic, and loops if links are misconfigured. **EVPN** (RFC 7432, with VXLAN in RFC 8365) replaces it with **BGP**: the leaves tell each other which MAC addresses are behind them, in BGP routes, so unknown unicast does not need flooding and a host that moves is a route change. It uses the **multiprotocol capability** seen in the OPEN message in section 3.2 and runs in the same sessions as the underlay.

Two route types are enough to start:

- **Type 3, inclusive multicast**: "I am a VTEP for VNI 100; send me your broadcast, unknown and multicast traffic." It builds the **flood list** automatically.
- **Type 2, MAC and IP advertisement**: "this MAC address (and optionally this IP) is behind me, in VNI 100." It replaces learning from the wire.

(Type 1, Ethernet segment, and type 4 serve **multihoming**, a host on two leaves, and type 5 carries **IP prefixes** for routing between VNIs; all from memory.)

`e7_evpn.sh` builds the same overlay, but this time with the VXLAN device in `nolearning` mode, activates the `l2vpn evpn` address family on every BGP session (the spines too, so that they pass the routes on) and `advertise-all-vni` on the leaves, and shows the routes and the forwarding table before and after one ping.

```bash
# e7: the same overlay with EVPN: BGP carries the MAC addresses and the flood list, nothing is learned from traffic
. prog/fabric.sh
trap 'fab_down; ip netns del h1 2> /dev/null; ip netns del h2 2> /dev/null' EXIT
fab_up; sleep 0.5
for h in 1 2; do ip netns add h$h; nse h$h ip link set lo up; done
for i in 1 2; do
  nse l$i ip link add br100 type bridge
  nse l$i ip link add vxlan100 type vxlan id 100 local 10.255.0.$i dstport 4789 nolearning
  nse l$i ip link set vxlan100 master br100
  ip link add h$i netns h$i type veth peer name hl$i netns l$i
  nse l$i ip link set hl$i master br100
  for d in vxlan100 br100 hl$i; do nse l$i ip link set $d up; done
  nse h$i ip addr add 192.168.100.$i/24 dev h$i; nse h$i ip link set h$i up
done
bgp_fabric "timers 1 3" 2>&1 | grep -vE 'ZEBRA|BFD:'
sleep 6
for r in s1 s2 l1 l2 l3; do
  cmds=(-c 'conf t' -c "router bgp $(grep -m1 'router bgp' /tmp/frr/$r.conf | awk '{print $3}')" -c 'address-family l2vpn evpn')
  for n in $(grep -oE 'neighbor [0-9.]+ remote-as' /tmp/frr/$r.conf | awk '{print $2}'); do cmds+=(-c "neighbor $n activate"); done
  case $r in l1|l2) cmds+=(-c 'advertise-all-vni');; esac
  vt $r "${cmds[@]}" > /dev/null
done
sleep 5
show_evpn() {
  echo "forwarding table of the overlay on l1:"; nse l1 bridge fdb show dev vxlan100 | sed 's/^/  /'
  echo "EVPN routes on l1 (type 2 = MAC, type 3 = flood list entry):"
  vt l1 -c 'show bgp l2vpn evpn route' | grep -E '^\*>|^Route Dist|^Displayed' | sed 's/^\*> */  /'
}
echo "== 1. before any host traffic"
show_evpn
echo "== 2. h1 pings h2 once, then the same view"
nse h1 ping -c 1 -W 1 192.168.100.2 | grep -E 'packet loss'
sleep 3
show_evpn
echo "== 3. the MAC addresses of the two hosts"
for h in 1 2; do printf 'h%s %s\n' $h "$(nse h$h cat /sys/class/net/h$h/address)"; done
echo "== 4. the extended communities on l2's MAC route, as l1 sees them"
vt l1 -c 'show bgp l2vpn evpn route type macip' | grep -B1 -A2 'RT:65002' | grep -E 'ET:|RT:' | head -2
```

```text
== 1. before any host traffic
forwarding table of the overlay on l1:
  e2:dc:10:12:03:f8 master br100 permanent
  00:00:00:00:00:00 dst 10.255.0.2 self permanent
EVPN routes on l1 (type 2 = MAC, type 3 = flood list entry):
Route Distinguisher: 10.255.0.1:2
  [3]:[0]:[32]:[10.255.0.1]
Route Distinguisher: 10.255.0.2:2
  [3]:[0]:[32]:[10.255.0.2]
Displayed 2 prefixes (3 paths)
== 2. h1 pings h2 once, then the same view
1 packets transmitted, 1 received, 0% packet loss, time 0ms
forwarding table of the overlay on l1:
  82:9b:ed:aa:0c:e3 extern_learn master br100
  e2:dc:10:12:03:f8 master br100 permanent
  82:9b:ed:aa:0c:e3 dst 10.255.0.2 self extern_learn
  00:00:00:00:00:00 dst 10.255.0.2 self permanent
EVPN routes on l1 (type 2 = MAC, type 3 = flood list entry):
Route Distinguisher: 10.255.0.1:2
  [2]:[0]:[48]:[6a:94:f6:71:0b:c6]
  [3]:[0]:[32]:[10.255.0.1]
Route Distinguisher: 10.255.0.2:2
  [2]:[0]:[48]:[82:9b:ed:aa:0c:e3]
  [3]:[0]:[32]:[10.255.0.2]
Displayed 4 prefixes (6 paths)
== 3. the MAC addresses of the two hosts
h1 6a:94:f6:71:0b:c6
h2 82:9b:ed:aa:0c:e3
== 4. the extended communities on l2's MAC route, as l1 sees them
                    RT:65002:100 ET:8
                    RT:65002:100 ET:8
```

1. **Before any host traffic**, the forwarding table already has the flood entry `00:00:00:00:00:00 dst 10.255.0.2`, and the BGP table has **two type 3 routes**, `[3]:[0]:[32]:[10.255.0.1]` and `[3]:[0]:[32]:[10.255.0.2]`: each leaf announced itself as a VTEP for VNI 100, and the other installed the flood entry. Nobody configured the remote VTEP address on the leaf.
2. **After one ping**, each leaf has announced the MAC address of its host in a **type 2 route** (`[2]:[0]:[48]:[<MAC>]`), and `l1`'s table now holds the far host's MAC as `dst 10.255.0.2 self extern_learn`: `extern_learn` means that the entry was **installed by a routing daemon**, not learned from a frame. The extended community on the route (`RT:65002:100`, `ET:8`) carries the **route target** (which VNI's table this belongs in) and the encapsulation type (8 is VXLAN, from memory).

The result is that the overlay's MAC table moves from the data plane to the **control plane**, which is how a big overlay is run, with all the tools of BGP (policy, communities, route reflectors) available. The cost is that a **bug** in it is now a routing problem, and that a table of millions of MAC routes is BGP state to be held and converged.

---


## 10. Failure detection and convergence

### 10.1 What the time is made of

When a link fails, the **convergence time** is the time until every router forwards around it. It is the sum of four parts:

1. **Detection**: how long until a router knows the link or its neighbour is gone.
2. **Propagation**: the time for the news to reach the routers that must change: an LSA flooded (OSPF), a withdrawal passed from one BGP router to the next.
3. **Computation**: SPF in OSPF, best path selection in BGP, and the throttle timers that sit in front of them.
4. **Installation**: handing the new route to the kernel (or the ASIC) and having it take effect.

For a fabric the first part is usually the largest, and it is the one the operator sets. The packets sent into the failed link during that time are lost, and an **ECMP** fabric helps more than most: the packets of the flows on the surviving paths are not affected, and the routers that sit next to the failure can remove the dead next hop from the group at once (section 11).

### 10.2 Three ways to detect

- **Carrier loss.** The interface goes down and the operating system tells the routing daemon. This is the fastest and needs no protocol, and it exists only if both ends see the link state.
- **Protocol timers.** OSPF declares a neighbour dead after the dead interval without a Hello. BGP closes the session when the hold timer runs out without a message. Both are **tens of seconds to minutes** at defaults on many platforms (from memory) and, set to **seconds**, they add CPU load and risk false alarms when a router is busy.
- **BFD** (Bidirectional Forwarding Detection, RFC 5880): two routers exchange **tiny UDP packets** (port 3784 for single hop) at a fixed interval, **independent of any routing protocol**, and declare the path down after `detect-multiplier` packets missing. The routing protocols **register** with it, and when BFD says down they close the session at once. The detection time is `interval x multiplier`, which can be tens of milliseconds, and with BFD **in hardware** (the switch's data plane or a dedicated NPU) down to a few milliseconds (from memory).

A **silent failure** is a path that stops forwarding while the link stays up: a line card or ASIC fault, a unidirectional fibre break (one direction is dark, the other still carries light), a firewall rule or a hung control plane. Carrier does not see it, and only a **probe along the same path** does. `e5_converge.sh` makes both kinds. The carrier failure is `ip link set s1l2 down`. The silent failure is an `nft` rule on `s1` that **drops all packets in and out** of the `s1` to `l2` interface while the link stays up. The time measured is from the fault until `l1` has only **one** next hop for `10.10.2.0/24`, which is `l1` seeing that the path through `s1` is gone.

```bash
# e5: time for l1 to drop the path through s1 after the s1-l2 link fails, by failure type and protocol
. prog/fabric.sh
trap fab_down EXIT
silent() { # break s1l2 without changing link state: drop everything in both directions
  nse s1 nft -f - << 'RULES'
table inet cut {
  chain i { type filter hook input priority -10; iifname "s1l2" drop; }
  chain o { type filter hook output priority -10; oifname "s1l2" drop; }
}
RULES
}
trial() { # label, bring-up function, fault function
  fab_up; sleep 0.5; $2 2>&1 | grep -vE 'ZEBRA|BFD:'; sleep 25
  printf '%-52s' "$1"
  t0=$(date +%s.%N); $3; python3 prog/waitroute.py l1 10.10.2.0/24 1 $t0
  fab_down
}
carrier() { nse s1 ip link set s1l2 down; }
bgp_fast() { bgp_fabric "timers 1 3"; }

bgp_bfd() { bgp_fabric "timers 1 3|bfd"; }
bgp_bfd_p() { bgp_fabric "timers 1 3|bfd profile fast"; }
ospf_fast() { ospf_fabric fast; }
trial "BGP hold 3 s, link goes down (carrier lost)" bgp_fast carrier
trial "BGP hold 3 s, silent failure (packets dropped)" bgp_fast silent
trial "BGP hold 3 s + BFD 300 ms x 3 (default), silent" bgp_bfd silent
trial "BGP hold 3 s + BFD 50 ms x 3, silent failure" bgp_bfd_p silent
trial "OSPF dead 3 s, link goes down (carrier lost)" ospf_fast carrier
trial "OSPF dead 3 s, silent failure (packets dropped)" ospf_fast silent
```

```text
BGP hold 3 s, link goes down (carrier lost)         0.13 s
BGP hold 3 s, silent failure (packets dropped)      2.84 s
BGP hold 3 s + BFD 300 ms x 3 (default), silent     0.94 s
BGP hold 3 s + BFD 50 ms x 3, silent failure        0.24 s
OSPF dead 3 s, link goes down (carrier lost)        0.03 s
OSPF dead 3 s, silent failure (packets dropped)     2.78 s
```

| Failure and protocol | Detection (computed) | Measured | Remainder |
|---|---|---|---|
| BGP, link down | carrier, 0 | **0.13 s** | 0.13 s |
| BGP, silent, hold timer 3 s | 3 s (hold) | **2.84 s** | -0.16 s |
| BGP, silent, BFD 300 ms x 3 | 0.9 s | **0.94 s** | 0.04 s |
| BGP, silent, BFD 50 ms x 3 | 0.15 s | **0.24 s** | 0.09 s |
| OSPF, link down | carrier, 0 | **0.03 s** | 0.03 s |
| OSPF, silent, dead 3 s | 3 s (dead) | **2.78 s** | -0.22 s |

Reading it:

- **A silent failure costs the full timer.** Without BFD it took **2.84 s** for BGP and **2.78 s** for OSPF, and the timer was 3 s. The measure is a little under 3 s because the timer counts from the last message **received**, which was before the fault: the hold timer was already partly run down. With the usual **180 s** BGP hold timer (from memory) the same failure would blackhole traffic into that link for up to three minutes, and that is the case BFD exists for.
- **BFD set the time.** At 300 ms times 3 the detection is 0.9 s and the measure 0.94 s; at 50 ms times 3 it is 0.15 s and the measure 0.24 s. The remainder, **40 to 90 ms**, is the time for `s1` to close the session, withdraw the route, tell `l1`, and for `l1` to update the kernel, plus up to 5 ms of polling. So the **detection interval is the knob**, and the rest is a small fixed cost on this fabric.
- **Carrier loss is already fast.** **0.13 s** for BGP and **0.03 s** for OSPF. OSPF is faster here because its SPF throttle starts with a delay of 0 (section 2.2), while BGP sends an UPDATE and `l1` processes it; I did not separate the parts. These two numbers are why a failure that the operating system sees is not the one to design for.
- **The price of fast timers.** Packets at 50 ms intervals from every router to every neighbour are `1 / 0.05 = 20` packets a second per session each way, which is nothing for one session and real CPU for thousands; and a router busy enough to miss **three** packets in a row, 150 ms, declares a healthy neighbour dead, tears the session down and causes the outage it was meant to prevent. Production BFD is therefore either in **hardware** or set to hundreds of milliseconds, and routers use **graceful restart** to avoid drain on a daemon restart (from memory).

### 10.3 What the number does not include

The **computation and installation** in a fabric of 5 routers are tens of milliseconds. They grow with the table: a router that must withdraw, recompute and **reprogram 100,000 routes** into an ASIC (ASIC programming is hundreds to thousands of routes per second per core in software and quicker in hardware, from memory) can take seconds, and the **order** in which it reprograms them matters. A BGP network that has a long chain of routers also suffers **path exploration**: after a withdrawal, each router tries each alternative path it still holds, one after another, before it gives up, so a prefix may change many times before it settles. The leaf-spine design with a shared spine AS (section 6.2) has very few alternatives, which keeps exploration short, so it is a **convergence** reason for the design as well as a policy reason. None of this was measured here.

Finally, **TCP** decides what an application sees: a flow that loses packets in a detection window recovers when its retransmission timer fires (a **minimum of 200 ms** in Linux, from memory), so detecting in 50 ms rather than 150 ms does not always shorten the pause the application sees by the same amount, and detecting in 3 s means several timeouts with backoff. The honest summary for an interview: **detect quickly (BFD), propagate only what changed (BGP), reroute locally first (ECMP in hardware), and measure the loss, not the protocol time**.

---


## 11. From a route to the forwarding hardware

### 11.1 RIB and FIB

A router keeps two tables, and the split is the key to how all of this works.

- The **RIB** (routing information base) holds **every route from every source**: connected networks, static routes, and what OSPF and BGP computed. When several sources know the same prefix, the **administrative distance** picks one (OSPF's is **110**, as in `[110/30]` in the listing in section 2.2, and the second number is the metric; the others, such as eBGP at 20, are from memory).
- The **FIB** (forwarding information base) holds only the **result**: for each prefix, the next hop group, and for each next hop the outgoing interface and the neighbour's link layer address. The data plane reads only this table, with a longest prefix match, for every packet.

In FRR the roles are split between processes. `ospfd` and `bgpd` compute routes and hand them to **`zebra`**, the routing table manager, which holds the RIB, picks the winner for each prefix, and installs it in the kernel over **netlink**. The listing in section 2.2 shows both sides of that handoff: `zebra`'s view marks `O>*` (`>` selected, `*` installed in the FIB), and the kernel's table shows the same route with `proto ospf`. The kernel's own FIB is a trie keyed by prefix (a longest prefix match structure, from memory).

### 11.2 Next hop groups

The kernel output in section 2.2 held a detail worth a second look: `10.10.2.0/24`, `10.10.3.0/24`, `10.255.0.2` and `10.255.0.3` all show `nhid 25`, one **next hop object** with the two members `s1` and `s2`, **shared** by four routes. Routes point at the group and the group points at the next hops. The benefit is at failure time: when the link to `s1` fails, **one group** is updated and every route that uses it is fixed at once, in time that does not depend on the number of routes (**prefix independent convergence**, from memory). A router that keeps a next hop list inside each route has to rewrite every one of them, so 100,000 routes cost 100,000 writes. Section 8.5 used the same objects (`ip nexthop`) for the resilient group.

### 11.3 In an ASIC (from memory)

A switch chip holds the FIB in on-chip memory in one of two forms. A **TCAM** (ternary content addressable memory) compares the address with every stored prefix in parallel in a single cycle, and so gives the longest match by ordering the entries by prefix length; it is fast, and costs power and area, so the number of entries is limited. **Algorithmic** schemes keep prefixes in hash tables and tries in ordinary SRAM, with tens of thousands of routes to a million and more, depending on the chip. The next hop group sits in a separate table, which gives the sharing of section 11.2 in hardware, and ECMP is done there: the chip hashes the packet fields (section 8) and uses the result to index the group. This is why a **FIB size** is a hard limit of a switch, why a fabric aggregates routes at the edge, and why the **number of ECMP groups and members** is a limit as well.

### 11.4 SONiC at a glance (from memory)

**SONiC** (Software for Open Networking in the Cloud) is an open source network operating system, started at Microsoft, that runs on switches with many ASICs, including Nvidia Spectrum. Its routing stack is the FRR used in this tutorial. What makes it different is that it is a set of **containers around a Redis database**, and a route passes through it in steps:

1. **`bgpd`** receives the UPDATE and decides the best path; **`zebra`** puts it in the RIB and sends it to the kernel over netlink and to **`fpmsyncd`** over the Forwarding Plane Manager (FPM) interface.
2. **`fpmsyncd`** writes the route into the **APPL_DB** (application database in Redis).
3. **`orchagent`** (in the `swss` container) subscribes to APPL_DB. Its route, neighbour and next hop group orchestration modules turn the entry into the objects of the **SAI** (Switch Abstraction Interface, a vendor neutral C API) and write them to **ASIC_DB**.
4. **`syncd`** reads ASIC_DB and calls the SAI library of the chip vendor, which programs the ASIC through its SDK.

A link failure takes a shorter path: the port state change goes to the database, and `orchagent` removes the member from the next hop groups. The design has two consequences that interviewers ask about. A route has to cross **several processes and a database** before it reaches the chip, and each hop can be a place where it queues, so the end-to-end **route programming time** at scale is something SONiC builds measure and tune. And the **database is the interface**, so tools and tests can read or write the state at each stage (`redis-cli` against `APPL_DB`, `ASIC_DB`), which makes a problem findable: where did the route stop?

---


## 12. Common mistakes

1. **Believing a hash that splits flows evenly at one tier does so at the next.** With the same hash function and seed at every tier, `b1` sent **all 1,046** flows to one next hop and `b2` all 955 to the other, so **half of the second tier links carried nothing** (section 8.3). Use a different seed, hash bits or ingress mixing per tier, and test with the **real topology**, not one router.
2. **Testing polarisation with a sender whose source address changes.** My first `e4` run showed **no** polarisation, because the sender chose its source address **after** the route (10.0.1.0 or 10.0.2.0, one per path) and the changed address changed the hash at the next tier. Binding the sender to one fixed address (`10.9.9.1`) showed it. A real server has one address, so the test must too.
3. **Hashing on addresses only in a datacenter.** With policy 0, **2,001 of 2,001** packets took one path (section 8.2). Hash the 5-tuple, and for tunnels make sure the outer source port carries the inner flow's entropy (section 9.3).
4. **Using the default hold timer as the failure detector.** A silent failure took the full timer: **2.84 s** with a 3 s hold and, at 180 s (from memory), up to three minutes of blackhole. Use BFD (**0.24 s** at 50 ms x 3) and treat carrier loss as the case that already works (**0.13 s**).
5. **Setting BFD timers faster than the router can keep.** Three missed packets at 50 ms is 150 ms; a busy control plane declares a healthy neighbour dead and causes the outage. Run BFD in hardware, or set hundreds of milliseconds (section 10.2).
6. **Putting a cost change on a link and expecting only the path you wanted.** With the cost of one uplink raised to 100, a failure sent OSPF traffic over a **four hop** path through a third leaf at cost **50** (Problem E). A metric describes preference, not a path you intended; a policy that says "never through a leaf" is in BGP's AS path rule (section 6.2).
7. **Giving each spine its own AS number without thinking about valley paths.** A shared spine AS makes a path that crosses a leaf carry the spine AS twice, and BGP drops it (`s1` kept **6 paths for 6 prefixes**, section 6.2). With distinct spine ASNs the leaf can become a transit.
8. **Expecting iBGP to propagate.** A route learned from one iBGP peer is **not** passed to another: `l2` had **1 route of 2** until `s1` became a reflector (section 5). Also, iBGP leaves the next hop unchanged, so it has to be reachable (the static routes in `e9_rr.sh`).
9. **Mixing policy and ECMP.** Local preference 200 on one spine, or one prepend, cut the kernel route from **2 next hops to 1** (section 4). A fabric that wants ECMP keeps policy uniform between equal paths.
10. **Forgetting what an overlay costs.** VXLAN adds **50 bytes**; a **1,500 byte** inner packet **failed** (100 percent loss) and **1,450** passed (section 9.2). Make the underlay MTU 9,000 or clamp the inner one.
11. **Reading the next hop count as load balance.** Two next hops balance the **number of flows**, not bytes: two elephant flows collide with probability **1/2** on two paths (section 8.4), and the Linux VXLAN device in this run sent **402 of the 406** counted packets through one spine in spite of 393 different outer ports (section 9.3).
12. **Losing healthy flows when a member leaves a group.** The ordinary group moved **110 of 316** flows that were not on the failed next hop; the resilient group moved **0 of 303** (section 8.5).
13. **Taking OSPF's quick convergence at face value.** The adjacencies were up in about 2 s, but the **last route** appeared about **15 s** after start, behind the LSA and SPF throttles (section 2.2). The timer that matters is not always the hello.
14. **Forgetting to enable the address family.** EVPN routes flow only if the `l2vpn evpn` family is **activated** on every session, spines included; the spines have no VNI but must pass the routes on (section 9.4).

---

## 13. Twelve interview questions

1. **What is the difference between link state and distance vector?** Link state routers flood their **own links**, every router holds the **same database** and runs Dijkstra; after a failure one LSA is flooded and everyone recomputes (OSPF, IS-IS). Distance vector routers send **distances** and take the neighbour's word (RIP; BGP is a **path vector** with the AS path attached). Measured: two distance vector routers needed **15 exchanges** to count to infinity after a cut, and split horizon stopped it in **2** (and only for a loop of two).
2. **Walk through an OSPF adjacency and what is in the database.** Hellos find neighbours, the pair goes through Down, Init, 2-Way, ExStart, Exchange, Loading and **Full**, swapping LSA headers and then the LSAs they lack. Measured: **5 router LSAs for 5 routers**, no network LSAs on point-to-point links, a link count of 6 for a leaf and 7 for a spine (two entries per point-to-point link, plus stubs), and a full SPF in **87 microseconds**.
3. **What are OSPF areas for?** To bound **flooding and SPF**: the routers in an area share one database, areas attach to area 0 through ABRs that pass **summaries**, and the inside of an area is hidden from the rest. A small fabric runs one area; a large OSPF network uses areas to keep a change local.
4. **How does BGP choose the best path?** In order, stopping at the first difference: weight, **local preference**, locally originated, **shortest AS path**, origin, MED, eBGP over iBGP, IGP metric to the next hop, oldest path, **router ID**. Measured: equal paths were decided by `Router ID`, local preference 200 by `Local Pref`, one prepend by `AS Path`. Only paths that tie to the IGP metric are used together (**multipath**), so policy and ECMP pull against each other.
5. **Why can't an iBGP speaker pass on what it learned from an iBGP peer, and what do you do about it?** There is no AS path change inside an AS to catch a loop, so the rule prevents one. The cost is a **full mesh** (`n(n-1)/2`: 45 sessions for 10 routers, 4,950 for 100). A **route reflector** breaks the rule for its clients and uses `ORIGINATOR_ID` and `CLUSTER_LIST` against loops. Measured: `l2` learned **1 route of 2** without it and both with it, carrying `Originator: 10.255.0.1` and `Cluster list: 10.255.1.1`.
6. **Why do large datacenters use eBGP and what is the AS plan?** Contained changes (no fabric-wide flood), per-prefix policy for drains, one protocol, and no IGP needed to resolve next hops since every session is on a point-to-point link. Each leaf has its own AS and all spines share one; a leaf-to-leaf-to-spine valley path then carries the spine AS twice and is **rejected**. Measured after a failure: OSPF used a **four hop** valley path at cost 50, BGP the **two hop** path through the other spine.
7. **Size a leaf-spine fabric for 3,000 servers on 64 port switches at 3:1.** `d / u <= 3`, so `d = 48, u = 16`; leaves `ceil(3000 / 48) = 63`; spines = uplinks per leaf = **16**; spine ports used 63 of 64; cables `63 x 16 = 1,008`; servers supported **3,024**. A server sending out of its rack with its neighbours gets `100 x 16 / 48 = 33.3 Gbit/s`. More servers than `K x d` needs a third tier.
8. **What is ECMP hash polarisation and how do you avoid it?** Every tier computes the same hash of the same fields, so a flow lands on the same side of the split at each tier and some links of the lower tier carry **nothing** (measured: **1,046 to c1 and 0 to c2**). Avoid it with a **different seed** (or hash bits, or mixing in the ingress port) at each tier: measured **512, 504, 487 and 499** of 2,000.
9. **What happens to flows when a link in an ECMP group fails?** In an ordinary group the hash space is re-cut, so flows on **healthy** links move too (measured **110 of 316**), reordering TCP and breaking stateful paths. **Resilient hashing** reassigns only the failed member's buckets (**0 of 303** moved). Say that the group is a shared object, so the update is one write for every route that uses it.
10. **Explain VXLAN and EVPN.** VXLAN wraps an Ethernet frame in UDP (port 4789) with a 24 bit VNI between two VTEPs: **50 bytes** of overhead (14 + 20 + 8 + 8), measured an outer IP length of **178** for a 142 byte frame, and a **1,500 byte** inner packet failed on a 1,500 byte underlay. The outer UDP source port is a hash of the inner headers so the fabric's ECMP can spread flows (**393 distinct ports in 397** packets). EVPN replaces flood and learn with **BGP**: type 3 builds the flood list (present before any traffic), type 2 advertises MAC addresses (present after one ping, with `extern_learn` in the bridge).
11. **How fast does a fabric converge after a failure and what sets it?** Detection plus propagation plus computation plus installation, and detection dominates. Measured: carrier loss **0.13 s** (BGP) and **0.03 s** (OSPF), a silent failure **2.84 s** with a 3 s hold timer, **0.94 s** with BFD at 300 ms x 3, **0.24 s** at 50 ms x 3. Detection is `interval x multiplier` and the rest was 40 to 90 ms here. Say that production values depend on whether BFD is in hardware.
12. **Walk a BGP route from the wire to the ASIC.** `bgpd` takes the UPDATE through Adj-RIB-In, inbound policy and best path selection, hands the route to `zebra`, which holds the RIB, picks by administrative distance and installs in the kernel over netlink (measured: `O>*` and the kernel's `proto ospf`, with a shared `nhid`). In SONiC, `zebra` also sends it over FPM to `fpmsyncd`, which writes APPL_DB; `orchagent` converts it to SAI objects in ASIC_DB; `syncd` programs the chip through the vendor SDK. In the chip, a longest prefix match (TCAM or algorithmic) yields a **next hop group** that the ECMP hash indexes (from memory beyond the kernel). A link failure updates the **group**, not the routes.

---


## 14. Problems

Each problem says where to solve it and how to approach it. Problem A needs only Python. Problems B, D and E need the fabric: Linux, root, FRR (`apt install frr`), and the helpers `prog/frr.sh` and `prog/fabric.sh` (a VM or a container with `NET_ADMIN` works; the daemons run in network namespaces). Problem C needs only the namespace helper `prog/ecmp.sh`. Predict before you run.

**Problem A. Size a fabric.** A cluster needs **3,000 servers**. The switches have **64 ports**, and the oversubscription at a leaf must be **at most 3:1**. Compute the downlinks and uplinks per leaf, the number of leaves, the number of spines, the spine ports used, the number of servers supported and the number of cables between leaves and spines. **Where:** paper, then Python. **Approach:** the ratio is `d / u` with `d + u = 64`, so take the largest `d` that keeps it at 3 or below; the leaves are the servers divided by `d`, rounded **up**; the spines equal the uplinks per leaf (each leaf has one link to each spine); a spine uses one port per leaf.

**Problem B. Steer one prefix.** In the BGP fabric, make leaf `l1` reach `10.10.3.0/24` **only through `s2`**, while `10.10.2.0/24` keeps **both** paths. Show the kernel routes before and after. **Where:** the fabric, as root (`. prog/fabric.sh; fab_up; bgp_fabric "timers 1 3"`, then `vt l1 -c ...`). **Approach:** the attribute is local preference, which is decided before the path length; a route map must match **only that prefix** (a prefix list) and then end with an empty `permit` clause that lets everything else through, since a route map ends with an implicit deny; apply it **inbound** on the neighbour you want preferred, then do a soft inbound reset so that the existing routes are evaluated again.

**Problem C. Reseed one router.** In the three tier path (`ecmp.sh`) send 2,000 flows with the 5-tuple hash. First with the default seeds, then reseed **only `b1`**, then reseed `b2` too with a different value. **Predict** the four second tier counts in each case before you run. **Where:** the namespace helper, as root. **Approach:** each router that has the same seed as the one before it makes the same choice for the same flow; a router with a **new** seed is independent of the others; so decide for each of `b1` and `b2` whether it shares its hash with `a`.

**Problem D. Choose BFD timers.** A silent failure must be detected, and the route withdrawn, within **500 ms**. The part of the time that is not detection was **40 to 90 ms** in `e5`. Choose a BFD interval with a multiplier of 3, state the predicted time, and **measure** it three times. **Where:** the fabric, as root; the interval is set through `BFD_MS`. **Approach:** `detection = interval x multiplier`; leave about 100 ms for withdrawal, propagation and installation; solve `3 x interval + 0.1 <= 0.5` and round down to a value the daemon accepts; cut the link silently with an `nft` rule in both directions, not with `ip link set down`.

**Problem E. The valley path.** In the OSPF fabric raise the cost of `l1`'s uplink to `s2` (`l1s2`) to **100**. Predict `l1`'s route to `10.10.2.0/24` and its metric. Then **fail the link between `s1` and `l2`** and predict the new path and its metric before you run, and compare with what BGP did in `e11_valley.sh`. **Where:** the fabric, as root (`ospf_fabric fast`; allow 30 s for OSPF to settle and about 12 s after the failure). **Approach:** add up link costs along each candidate path (every link is 10, and the stub network at the end adds 10); list **all** paths from `l1` to `l2`, including the ones through a third leaf; OSPF picks the cheapest, whatever its shape.

---

## 15. Answers

### Problem A

```python
# ans_a.py: 3000 servers, at most 3:1 oversubscription, 64-port switches, two tiers
import math
K, SERVERS, MAX_RATIO = 64, 3000, 3.0
for down in range(K // 2, K):
    up = K - down
    if down / up <= MAX_RATIO and down * K >= SERVERS:
        pass
best = max(d for d in range(1, K) if d / (K - d) <= MAX_RATIO)
up = K - best
leaves = math.ceil(SERVERS / best)
print("largest allowed downlink count: %d down, %d up (ratio %.2f:1)" % (best, up, best / up))
print("leaves needed: ceil(%d / %d) = %d" % (SERVERS, best, leaves))
print("spines needed: one per uplink = %d" % up)
print("spine ports used: %d leaves of %d ports available on each spine" % (leaves, K))
print("servers supported: %d, spare server ports: %d" % (leaves * best, leaves * best - SERVERS))
print("cables between leaves and spines: %d" % (leaves * up))
```

```text
largest allowed downlink count: 48 down, 16 up (ratio 3.00:1)
leaves needed: ceil(3000 / 48) = 63
spines needed: one per uplink = 16
spine ports used: 63 leaves of 64 ports available on each spine
servers supported: 3024, spare server ports: 24
cables between leaves and spines: 1008
```

The largest `d` with `d / (64 - d) <= 3` is **48** (`48 / 16 = 3.00`; at 49 it would be 3.27). So each leaf has **16 uplinks**, which means **16 spines**. The servers divided by the downlinks, `3,000 / 48 = 62.5`, rounds up to **63 leaves**, and each spine uses **63 of its 64 ports**. The fabric supports `63 x 48 = 3,024` servers (24 spare ports) with `63 x 16 = 1,008` cables. State what a Staff answer adds: growth is a **64th leaf** and then a third tier; one spine failing costs `1/16` of the capacity; and this is a `3:1` design, so say what traffic it assumes.

### Problem B

```bash
# Problem B: l1 must reach 10.10.3.0/24 only through s2, while 10.10.2.0/24 keeps both paths
. prog/fabric.sh
trap fab_down EXIT
fab_up; sleep 0.5
bgp_fabric "timers 1 3" 2>&1 | grep -vE 'ZEBRA|BFD:'
sleep 8
echo "before:"; for p in 10.10.2.0/24 10.10.3.0/24; do nse l1 ip route show $p; done
vt l1 -c 'conf t' -c 'ip prefix-list L3 seq 5 permit 10.10.3.0/24' \
  -c 'route-map FROM_S2 permit 10' -c 'match ip address prefix-list L3' -c 'set local-preference 200' -c 'exit' \
  -c 'route-map FROM_S2 permit 20' -c 'exit' \
  -c 'router bgp 65001' -c 'address-family ipv4 unicast' -c 'neighbor 10.2.1.0 route-map FROM_S2 in' > /dev/null
vt l1 -c 'clear bgp ipv4 unicast 10.2.1.0 soft in' > /dev/null; sleep 2
echo "after:"; for p in 10.10.2.0/24 10.10.3.0/24; do nse l1 ip route show $p; done
```

```text
before:
10.10.2.0/24 nhid 12 proto bgp metric 20
        nexthop via 10.1.1.0 dev l1s1 weight 1
        nexthop via 10.2.1.0 dev l1s2 weight 1
10.10.3.0/24 nhid 12 proto bgp metric 20
        nexthop via 10.1.1.0 dev l1s1 weight 1
        nexthop via 10.2.1.0 dev l1s2 weight 1
after:
10.10.2.0/24 nhid 12 proto bgp metric 20
        nexthop via 10.1.1.0 dev l1s1 weight 1
        nexthop via 10.2.1.0 dev l1s2 weight 1
10.10.3.0/24 nhid 14 via 10.2.1.0 dev l1s2 proto bgp metric 20
```

Before, both prefixes had two next hops. After, `10.10.3.0/24` has **one**, through `s2` (`10.2.1.0`, `l1s2`), and `10.10.2.0/24` still has **two**. The route map `FROM_S2` has two clauses: sequence 10 matches the prefix list `L3` (only `10.10.3.0/24`) and sets local preference 200, and sequence 20 is an **empty permit**, so every other route passes unchanged. Without clause 20, the implicit deny at the end would have dropped every other route from `s2`, including `10.10.2.0/24`: the second path would vanish and you would have lost ECMP for **all** prefixes (from memory; I did not run it without clause 20). The `clear bgp ... soft in` makes the router re-evaluate routes it already has with the new policy; without it nothing changes until the next UPDATE.

### Problem C

```bash
# Problem C: reseed only some of the routers and see which tier stays polarised
. prog/ecmp.sh
trap ecmp_down EXIT
ecmp_up; policy 1; N=2000
run() {
  local b=($(snap)); sendflows $N; local e=($(snap)) i d=()
  for i in 0 1 2 3 4 5; do d+=($((e[i] - b[i]))); done
  echo "$1"; printf '  tier 1  a>b1 %d  a>b2 %d\n  tier 2  b1>c1 %d  b1>c2 %d  b2>c1 %d  b2>c2 %d\n' ${d[@]}
}
run "default seeds on a, b1, b2"
nse b1 sysctl -qw net.ipv4.fib_multipath_hash_seed=777
run "b1 reseeded, a and b2 unchanged"
nse b2 sysctl -qw net.ipv4.fib_multipath_hash_seed=999
run "b1 and b2 reseeded with different values"
```

```text
default seeds on a, b1, b2
  tier 1  a>b1 1047  a>b2 955
  tier 2  b1>c1 1047  b1>c2 0  b2>c1 0  b2>c2 955
b1 reseeded, a and b2 unchanged
  tier 1  a>b1 1046  a>b2 954
  tier 2  b1>c1 525  b1>c2 522  b2>c1 0  b2>c2 954
b1 and b2 reseeded with different values
  tier 1  a>b1 1046  a>b2 954
  tier 2  b1>c1 525  b1>c2 521  b2>c1 460  b2>c2 495
```

**Default seeds:** `a`, `b1` and `b2` share a hash, so the second tier is polarised (`b1>c1 1,047, b1>c2 0, b2>c1 0, b2>c2 955`). **`b1` reseeded:** `b1` is now independent of `a`, so it splits its roughly **1,046** flows `525` and `522`; but `b2` still shares `a`'s hash, and sends **all 954** to `c2` (`b2>c1 0`). **Both reseeded** with different values: `525, 521, 460, 495`, all four links in use. The rule is that a router must differ from the one **above it**; the 954 flows reaching `b2` are all on the same side of `a`'s split, and only a different hash at `b2` can divide them.

### Problem D

```bash
# Problem D: BGP with BFD must withdraw a silently failed path within 500 ms; try 100 ms x 3
. prog/fabric.sh
trap fab_down EXIT
export BFD_MS=100
cut() { nse s1 nft -f - << 'RULES'
table inet cut {
  chain i { type filter hook input priority -10; iifname "s1l2" drop; }
  chain o { type filter hook output priority -10; oifname "s1l2" drop; }
}
RULES
}
fab_up; sleep 0.5
bgp_fabric "timers 1 3|bfd profile fast" 2>&1 | grep -vE 'ZEBRA|BFD:'
sleep 25
vt l1 -c 'show bfd peers' | grep -E 'Detect-mult|Transmit|Receive' | sed 's/^ *//' | head -4
for k in 1 2 3; do
  t0=$(date +%s.%N); cut; printf 'trial %d: ' $k; python3 prog/waitroute.py l1 10.10.2.0/24 1 $t0
  nse s1 nft delete table inet cut
  python3 prog/waitroute.py l1 10.10.2.0/24 2 $(date +%s.%N) > /dev/null
done
```

```text
                        Detect-multiplier: 3
                        Receive interval: 100ms
                        Detect-multiplier: 3
                        Receive interval: 100ms
trial 1: 0.39 s
trial 2: 0.40 s
trial 3: 0.39 s
```

`3 x interval + 0.1 <= 0.5` gives `interval <= 133 ms`, so choose **100 ms** (a round value, and the detection is `3 x 100 = 300 ms`). The prediction is `0.3 + (0.04 to 0.09) = 0.34 to 0.39 s`, and the three trials gave **0.39, 0.40 and 0.39 s**, at the top of that range (the polling resolution is about 5 ms). The `BFD_MS` variable sets `transmit-interval` and `receive-interval` in the BFD profile `bgp_fabric` writes (`fabric.sh`, line 42), and the multiplier stays 3. A cost to state: at 100 ms every session sends 10 packets a second each way, and a control plane that is stalled for 300 ms takes the session down, so on a busy router this is a hardware BFD setting.

### Problem E

```bash
# Problem E: steer OSPF with link cost, then watch the fallback when the preferred path fails
. prog/fabric.sh
trap fab_down EXIT
fab_up; sleep 0.5
ospf_fabric fast 2>&1 | grep -v ZEBRA
sleep 30
echo "before:"; nse l1 ip route show 10.10.2.0/24
vt l1 -c 'conf t' -c 'interface l1s2' -c 'ip ospf cost 100' > /dev/null
sleep 8
echo "after raising the cost of l1s2 to 100:"; nse l1 ip route show 10.10.2.0/24; vt l1 -c 'show ip route 10.10.2.0/24' | grep -E 'Routing entry|via|metric'
nse s1 ip link set s1l2 down
sleep 12
echo "after the s1-l2 link fails:"; nse l1 ip route show 10.10.2.0/24; vt l1 -c 'show ip ospf route' | grep -E '10.10.2.0/24'
echo "the path packets take:"; nse l1 traceroute -n -w 1 -q 1 -s 10.10.1.1 10.10.2.1 2>&1 | tail -n +2
```

```text
before:
10.10.2.0/24 nhid 25 proto ospf metric 20
        nexthop via 10.1.1.0 dev l1s1 weight 1
        nexthop via 10.2.1.0 dev l1s2 weight 1
after raising the cost of l1s2 to 100:
10.10.2.0/24 nhid 18 via 10.1.1.0 dev l1s1 proto ospf metric 20
Routing entry for 10.10.2.0/24
  Known via "ospf", distance 110, metric 30, best
  * 10.1.1.0, via l1s1, weight 1
after the s1-l2 link fails:
10.10.2.0/24 nhid 18 via 10.1.1.0 dev l1s1 proto ospf metric 20
N    10.10.2.0/24          [50] area: 0.0.0.0
the path packets take:
 1  10.1.1.0  0.163 ms
 2  10.1.3.1  0.126 ms
 3  10.2.1.0  0.112 ms
 4  10.10.2.1  0.097 ms
```

Before the cost change, `10.10.2.0/24` has **two** next hops at metric 30. With `l1s2` at 100, the path through `s2` costs `100 + 10 + 10 = 120`, the path through `s1` costs `10 + 10 + 10 = 30`: **one** next hop, through `s1`, as printed. After the `s1` to `l2` link fails, `l1`'s paths to `l2` are: through `s2` directly at `100 + 10 + 10 = 120`, and **through `s1` then `l3` then `s2` then `l2`** at `10 + 10 + 10 + 10 + 10 = 50` (four links and the stub). OSPF takes the cheaper, and the traceroute confirms **four hops** (`10.1.1.0` is `s1`, `10.1.3.1` is `l3`, `10.2.1.0` is `s2`, and then the server). That path **bounces off leaf `l3`**: it goes up to a spine, down into a leaf and up to a spine again, loading links and a leaf that were not meant to carry transit traffic. Compare section 6.3: with BGP, the same failure (without the cost change) left `l1` with **one two-hop path through `s2`**, and `s1` had **no route** to `10.10.2.0/24`, because the only other way carried AS 65000 twice. The lesson is not "OSPF is wrong": the cost change made the detour cheaper than the direct path, and OSPF has no way to say "not through a leaf".

---


## 16. Cheat sheet

| Topic | Remember |
|---|---|
| Link state / distance vector | LS floods **own links**, everyone runs Dijkstra; DV sends **distances**; BGP is **path vector**; DV count to infinity **15 steps**, split horizon **2** |
| OSPF | IP protocol 89; Hello and dead interval; states Down, Init, 2-Way, ExStart, Exchange, Loading, **Full**; LSA types 1 router, 2 network, 3 summary, 5 external; areas bound flooding; **5 router LSAs** for 5 routers; SPF **87 microseconds** |
| OSPF timers | hello/dead, **LSA minimum interval 5 s**, SPF delay 0 and hold 50 ms to 5 s; last route **about 15 s** after start |
| BGP | TCP **179**; OPEN, UPDATE, KEEPALIVE, NOTIFICATION; capabilities in OPEN (multiprotocol, route refresh); UPDATE = attributes + prefixes; **2 OPEN, 8 UPDATE, 20 KEEPALIVE** on one link |
| Attributes | ORIGIN, AS_PATH, NEXT_HOP (mandatory); LOCAL_PREF; MED; COMMUNITY; ORIGINATOR_ID, CLUSTER_LIST |
| Best path | weight, **local pref**, originated, **AS path length**, origin, MED, eBGP over iBGP, IGP metric, oldest, **router ID**; multipath only if tied to the IGP metric |
| eBGP / iBGP | eBGP prepends AS and sets next hop to self; iBGP keeps both, carries LOCAL_PREF, and **does not pass iBGP routes to iBGP peers** |
| Route reflector | clients peer with it; adds `ORIGINATOR_ID` and `CLUSTER_LIST`; `n(n-1)/2` sessions become `n`; **45** for 10 routers, **4,950** for 100 |
| Datacenter BGP | eBGP on every link, **leaf own AS, spines one shared AS**, `maximum-paths`, no IGP; shared spine AS makes valley paths loop and **rejected** |
| Valley after a failure | OSPF **four hops at cost 50** through a third leaf; BGP **two hops** and `% Network not in table` on the spine |
| Sizing | `d + u = K`; spines = `u`; leaves <= `K`; **3,000 servers, K 64, 3:1: 63 leaves, 16 spines, 1,008 cables, 3,024 servers**; 3 tiers: `K^3 / 4` = **65,536** servers |
| ECMP hash | per flow, not per packet; addresses only: **2,001 of 2,001** on one path; 5-tuple: **1,046 and 955** |
| Polarisation | same hash at two tiers: **b1>c1 1,046, b1>c2 0**; fix: per tier seed: **512, 504, 487, 499** |
| Groups | ordinary group: **110 of 316** healthy flows moved on a member loss; resilient: **0 of 303**; one shared group object (`nhid`) for many routes |
| VXLAN | UDP **4789**, 24 bit VNI; **50 bytes** (14 + 20 + 8 + 8); 142 byte frame to **178** byte IP packet; 1,500 inner **failed**, 1,450 passed; outer source port = hash of inner headers |
| EVPN | BGP address family `l2vpn evpn`; type **3** = flood list (before traffic), type **2** = MAC (after the first frames), `extern_learn`; RT `65002:100` |
| Detection | carrier **0.13 s** (BGP) **0.03 s** (OSPF); silent, hold 3 s: **2.84 s**; BFD 300 ms x 3: **0.94 s**; 100 ms x 3: **0.39 s**; 50 ms x 3: **0.24 s** |
| BFD | UDP 3784 single hop; `interval x multiplier`; routing protocols register; hardware BFD for ms (from memory) |
| RIB / FIB | RIB all routes, administrative distance (OSPF **110**); FIB prefix to **next hop group**; zebra to kernel over netlink |
| SONiC (from memory) | bgpd, zebra, **fpmsyncd**, APPL_DB, **orchagent**, ASIC_DB, **syncd**, SAI, ASIC |

---

## 17. Sources

- RFC 2328, OSPF Version 2: https://www.rfc-editor.org/rfc/rfc2328 (neighbour states, LSA types, areas, SPF). RFC 5340 for OSPFv3.
- RFC 4271, A Border Gateway Protocol 4: https://www.rfc-editor.org/rfc/rfc4271 (messages, attributes, the decision process). RFC 4456, BGP route reflection: https://www.rfc-editor.org/rfc/rfc4456. RFC 7911, ADD-PATH: https://www.rfc-editor.org/rfc/rfc7911. RFC 5549, IPv4 routes with an IPv6 next hop: https://www.rfc-editor.org/rfc/rfc5549.
- RFC 7938, Use of BGP for Routing in Large-Scale Data Centers: https://www.rfc-editor.org/rfc/rfc7938 (the AS plan of section 6, from memory beyond what was measured).
- RFC 5880, Bidirectional Forwarding Detection: https://www.rfc-editor.org/rfc/rfc5880, and RFC 5881 for single hop BFD over IP.
- RFC 7348, VXLAN: https://www.rfc-editor.org/rfc/rfc7348. RFC 7432, BGP MPLS-based Ethernet VPN: https://www.rfc-editor.org/rfc/rfc7432. RFC 8365, A network virtualization overlay solution using EVPN: https://www.rfc-editor.org/rfc/rfc8365.
- RFC 2991 and RFC 2992, multipath issues and the hash-threshold algorithm: https://www.rfc-editor.org/rfc/rfc2992 (section 8). The kernel's `fib_multipath_hash_policy`, `fib_multipath_hash_seed` and resilient next hop groups: https://docs.kernel.org/networking/ip-sysctl.html, and `ip-nexthop(8)`.
- FRRouting documentation: https://docs.frrouting.org/ (zebra, ospfd, bgpd, bfdd, EVPN).
- SONiC architecture: https://github.com/sonic-net/SONiC/wiki/Architecture. SAI: https://github.com/opencomputeproject/SAI.
- C. Clos, "A study of non-blocking switching networks", Bell System Technical Journal, 1953. Alizadeh et al., "CONGA: Distributed congestion-aware load balancing for datacenters", SIGCOMM 2014 (flowlets, section 8.6).
- Measured on this VM: `v/*.out` for the experiments `e1` to `e7`, `e9` to `e11`, `dv` and `sizing`, and the answers `ans_a` to `ans_e`. Everything marked "from memory" is **not** measured here: verify it against the RFCs and the documentation above before relying on it.
