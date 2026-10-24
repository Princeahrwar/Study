# High-Level Design (System Design): Track Plan

> **Track code:** HLD · **Weeks:** 11–23 · **Budget:** ~80 hours · **Prerequisites:** LLD foundations (weeks 1–9) for the vocabulary; OS and networking basics help from week 12

System design often decides the level, and it is required for every Senior and Staff loop. You are starting from zero, so this track starts from zero: first a repeatable framework for the interview, then the building blocks one at a time (load balancers, caches, databases, replication, partitioning, consensus, queues), then nineteen full design problems worked the way an interviewer expects. It ends with the domain designs where your fleet and data-plane experience makes you stronger than most candidates.

---

## 1. Why this track matters

System design rounds appear in nearly every Senior-and-above loop, and weak design is a common reason for being levelled down. They are graded on scoping, data modelling and trade-offs before boxes and arrows. The problems vary by role: product systems (news feed, messaging), infrastructure (storage, queues), scale-out compute (inference serving, job schedulers, telemetry), security systems (decryption pipelines, distributed logging, policy distribution) and device fleets (configuration, data plane versus control plane at scale).
---

## 2. What you will be able to do after this track

1. Run a 45–60 minute design interview from requirements to deep dives, managing time and steering towards your strengths.
2. Estimate traffic, storage and bandwidth in two minutes, out loud, and use the numbers to drive decisions.
3. Explain each building block (load balancing, caching, SQL and NoSQL, indexes, replication, sharding, consensus, messaging, rate limiting) with its trade-offs and failure modes.
4. Design the nineteen classic and company-specific systems in this track, and handle the follow-ups on scale, failure and cost.
5. Present a domain design (data plane vs control plane, telemetry, fleet management) at Staff depth, using your own experience as evidence.

---

## 3. How the track is organised

| Part | Folder | What it is |
|---|---|---|
| Fundamentals | `01_fundamentals/` | 17 building-block tutorials, in study order |
| Design problems | `02_design_problems/` | 19 full designs, each in the same template |
| Systems domain | `03_systems_domain/` | 3 domain designs from your own field |
| Diagrams | [`diagrams/`](diagrams/telemetrix_hld.mmd) | Standalone Mermaid sources used by the domain notes |

---

## 4. Fundamentals (weeks 11–17)

Topics 02, 06, 09 and 15 are older, Staff-level notes; each is rewritten in the same detailed style as the new tutorials before you study it.

| # | File | Topic | Key subtopics | Status | Week |
|---|---|---|---|---|---|
| 01 | `01_design_interview_framework.md` | The design interview framework | The six phases (requirements, estimation, API, data model, high-level design, deep dives), timing, how levels are judged, driving the conversation, common failure modes | To write | 11 |
| 02 | [`02_estimation_and_budgets.md`](01_fundamentals/02_estimation_and_budgets.md) | Estimation and latency budgets | Latency numbers, QPS, storage and bandwidth arithmetic, latency budgets across tiers | Exists, rewrite due | 11 |
| 03 | `03_networking_for_system_design.md` | Networking for system design | DNS, CDNs, HTTP and keep-alive, REST vs gRPC vs GraphQL, WebSockets, long polling and server-sent events, API gateways | To write | 12 |
| 04 | `04_load_balancing_and_proxies.md` | Load balancing and proxies | L4 vs L7, algorithms (round robin, least connections, power of two choices), health checks, sticky sessions, consistent hashing, forward and reverse proxies, service mesh | To write | 12 |
| 05 | `05_caching_fundamentals.md` | Caching fundamentals | Where caches live, cache-aside, read-through, write-through, write-back, eviction (LRU, LFU, TTL), invalidation, thundering herds | To write | 13 |
| 06 | [`06_distributed_caching.md`](01_fundamentals/06_distributed_caching.md) | Distributed caching | Consistent hashing, invalidation at scale, stampede control, hot keys, Redis and Memcached in production | Exists, rewrite due | 13 |
| 07 | `07_databases_and_transactions.md` | Databases and transactions | Relational vs key-value, document, wide-column and graph stores, ACID, isolation levels and their anomalies, MVCC, choosing a database | To write | 13 |
| 08 | `08_indexing_and_storage_engine_basics.md` | Indexing and storage-engine basics | B-tree and LSM-tree indexes, secondary indexes, covering and composite indexes, write and read amplification | To write | 14 |
| 09 | [`09_storage_engines.md`](01_fundamentals/09_storage_engines.md) | Storage engines in depth | LSM vs B-tree vs columnar, compaction, time-series specifics | Exists, rewrite due | 14 |
| 10 | `10_replication.md` | Replication | Leader–follower, multi-leader, leaderless, synchronous vs asynchronous, replication lag and read-your-writes, quorums, failover | To write | 14 |
| 11 | `11_partitioning_and_sharding.md` | Partitioning and sharding | Range vs hash partitioning, rebalancing, hot keys and celebrity problems, secondary indexes across shards, request routing | To write | 15 |
| 12 | `12_consistency_and_consensus.md` | Consistency and consensus | CAP and PACELC, consistency models, Raft (leader election, log replication), distributed locks and fencing tokens, idempotency, exactly-once claims | To write | 15 |
| 13 | `13_messaging_and_streaming.md` | Messaging and streaming | Queues vs logs, Kafka's design, delivery semantics, ordering, consumer groups, backpressure, the outbox pattern, stream processing | To write | 16 |
| 14 | `14_reliability_and_microservices.md` | Reliability and microservices | Service discovery, timeouts, retries with backoff and jitter, circuit breakers, bulkheads, graceful degradation, observability (metrics, logs, traces), SLOs | To write | 16 |
| 15 | [`15_rate_limiters.md`](01_fundamentals/15_rate_limiters.md) | Rate limiting and admission control | Token bucket to distributed quota, admission control, load shedding | Exists, rewrite due | 16 |
| 16 | `16_storage_at_scale.md` | Storage at scale | Object storage and erasure coding, search and inverted indexes, time-series databases, data lakes and batch processing | To write | 17 |
| 17 | `17_security_and_multitenancy.md` | Security and multi-tenancy | Authentication and authorisation, OAuth 2.0 and OIDC, JWT, mTLS, encryption at rest and in transit, secrets, tenant isolation, noisy neighbours | To write | 17 |

---

## 5. Design problems (weeks 15–23)

Every problem uses the same template: requirements (functional and non-functional), estimation, API, data model, high-level architecture with a diagram, deep dives, bottlenecks and failure modes, what interviewers probe at Senior vs Staff, and variations.

| # | File | Problem | Teaches | Status | Week |
|---|---|---|---|---|---|
| 01 | `01_url_shortener.md` | URL shortener | ID generation, caching, read-heavy scaling | To write | 15 |
| 02 | `02_distributed_rate_limiter.md` | Distributed rate limiter | Counters at scale, Redis, accuracy vs cost | To write | 16 |
| 03 | `03_distributed_key_value_store.md` | Distributed key-value store | Partitioning, replication, quorums, conflict resolution | To write | 17 |
| 04 | `04_distributed_cache.md` | Distributed cache | Consistent hashing, eviction, hot keys | To write | 17 |
| 05 | `05_notification_system.md` | Notification system | Fan-out, queues, retries, user preferences | To write | 18 |
| 06 | `06_news_feed.md` | News feed | Fan-out on write vs read, ranking, celebrities | To write | 18 |
| 07 | `07_chat_messaging.md` | Chat and messaging | WebSockets, presence, ordering, delivery receipts | To write | 18 |
| 08 | `08_web_crawler.md` | Web crawler | Politeness, deduplication, URL frontier | To write | 19 |
| 09 | `09_search_autocomplete.md` | Search autocomplete | Tries at scale, top-K, update pipelines | To write | 19 |
| 10 | `10_video_streaming.md` | Video streaming | Transcoding, CDNs, adaptive bitrate | To write | 19 |
| 11 | `11_file_sync_dropbox.md` | File sync (Dropbox) | Chunking, deduplication, sync conflicts, metadata | To write | 20 |
| 12 | `12_ride_sharing.md` | Ride sharing | Geospatial indexes, matching, location updates | To write | 20 |
| 13 | `13_distributed_job_scheduler.md` | Distributed job scheduler | Leases, exactly-once execution claims, priorities | To write | 21 |
| 14 | `14_payment_system.md` | Payment system | Idempotency, ledgers, reconciliation, consistency | To write | 21 |
| 15 | `15_metrics_and_logging_platform.md` | Metrics and logging platform | High-volume ingest, time-series storage, querying | To write | 22 |
| 16 | `16_collaborative_editor.md` | Collaborative editor | OT vs CRDTs, presence, offline edits | To write | 22 |
| 17 | `17_gpu_inference_serving.md` | GPU inference-serving cluster | Batching, GPU memory, KV cache, autoscaling, PCIe and network bandwidth | To write | 23 |
| 18 | `18_ssl_decryption_and_nat_service.md` | SSL decryption pipeline and NAT service | Session tables, key handling, throughput per core, failure isolation | To write | 23 |
| 19 | `19_config_push_to_device_fleet.md` | Configuration push to a device fleet (your domain) | Versioned config, canaries, rollback, partial connectivity | To write | 23 |

---

## 6. Systems domain (weeks 22–23)

These are your strongest stories. Each is an older, Staff-level note, rewritten in the same detailed style as the new tutorials before you study it.

| # | File | Topic | Status | Week |
|---|---|---|---|---|
| 01 | [`01_data_plane_vs_control_plane.md`](03_systems_domain/01_data_plane_vs_control_plane.md) | Data plane vs control plane | Exists, rewrite due | 22 |
| 02 | [`02_telemetry_pipelines.md`](03_systems_domain/02_telemetry_pipelines.md) | Telemetry pipelines | Exists, rewrite due | 23 |
| 03 | [`03_edge_to_cloud_fleet_management.md`](03_systems_domain/03_edge_to_cloud_fleet_management.md) | Edge-to-cloud fleet management | Exists, rewrite due | 23 |

Diagrams: [`diagrams/telemetrix_hld.mmd`](diagrams/telemetrix_hld.mmd), [`diagrams/rollout_state_machine.mmd`](diagrams/rollout_state_machine.mmd), [`diagrams/lsm_write_read_path.mmd`](diagrams/lsm_write_read_path.mmd). Domain note 02 feeds Project 05, [`telemetrix`](../14_projects/05_telemetrix_fleet_telemetry/SPEC.md).

---

## 7. Practice

1. **Whiteboard every problem before reading the solution.** Set a 45-minute timer, write the requirements, estimate, draw, and record yourself. Then read the worked design and list what you missed.
2. **Mock with a person** from week 14: one design mock every two weeks, weekly from week 24 (see [`../15_interviews/03_mocks/00_mock_bank.md`](../15_interviews/03_mocks/00_mock_bank.md)).
3. **Keep an estimation sheet**: the numbers you reach for every time (latency table, bytes per record, QPS per server) are in [`../15_interviews/04_flashcards/00_core_numbers.md`](../15_interviews/04_flashcards/00_core_numbers.md).
4. **Draw with a tool you will use in the interview** (Excalidraw or the company's whiteboard) so that drawing is not slow on the day.

---

## 8. Representative interview questions

1. Design a URL shortener for 100 million new URLs a month. How do you generate IDs without collisions across servers?
2. Design a news feed. Fan-out on write or on read? What happens when a celebrity with 50 million followers posts?
3. Design a distributed key-value store. What happens during a network partition? How do you resolve conflicting writes?
4. Design a chat system with delivery and read receipts for 500 million users.
5. Design a rate limiter shared by 200 API servers. How accurate does it need to be, and what does accuracy cost?
6. Design a payment system that never double-charges. What does idempotency mean end to end?
7. Design an inference-serving platform for large language models across 1,000 GPUs.
8. Design the SSL-decryption pipeline in a next-generation firewall.
9. Design a system that pushes a configuration change to 500,000 devices safely.
10. Your service's p99 latency doubled overnight. How do you find out why?

---

## 9. How to study this track

1. Fundamentals: the five-step method in [`../README.md`](../README.md). The "practice" step is answering the tutorial's design questions on a whiteboard.
2. Design problems: cold attempt first (step 1 above), then the worked design, then a summary from memory of the three decisions that mattered most.
3. Connect every building block to systems you know: a firewall session table is a partitioned in-memory store; a Wi-Fi controller pushing config to APs is a control plane.

---

## 10. Resources

| Resource | Use it for |
|---|---|
| Hello Interview "Learn System Design" (core material free) | Building blocks and worked problems, in interview format |
| *System Design Interview*, Vol. 1 and 2, Alex Xu | Worked designs for most problems in this track |
| *Designing Data-Intensive Applications*, Martin Kleppmann (2nd ed. with Chris Riccomini, O'Reilly, 2026) | The depth behind fundamentals 07–13 |
| Google SRE book (free) | Reliability, SLOs, load shedding |
| Engineering blogs: Meta, Google, Netflix, Uber, Cloudflare, Discord | How real systems were built and what broke |

---

## 11. Progress

| Part | Done | Next |
|---|---|---|
| Fundamentals 01–17 | — | 01 framework, 02 estimation (week 11) |
| Design problems 01–19 | — | 01 URL shortener (week 15) |
| Systems domain 01–03 | — | Rewrite before week 22 |

Cross-track view: [`../00_MASTER_PLAN.md`](../00_MASTER_PLAN.md).
