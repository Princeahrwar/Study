# HLD 09 — Storage engines

> Prerequisite: `02_estimation_and_budgets.md`.
> You do not need to build a database. You need to argue about one credibly.

## The fundamental trade

Every storage engine trades among three amplifications. You cannot minimise all
three; picking which to sacrifice *is* the design.

```
  Write amplification   bytes written to disk / bytes written by the user
  Read amplification    disk reads per logical read
  Space amplification   bytes on disk / bytes of live data
```

| Engine | Write amp | Read amp | Space amp | Good at |
| --- | --- | --- | --- | --- |
| B-tree | high (page per update) | low (~tree height) | low-medium | Reads, range scans, updates in place |
| LSM tree | low (sequential batches) | high (many levels) | medium-high | Write-heavy ingest |
| Columnar | low | very low *for few columns* | very low (compresses well) | Analytics, scans |
| Hash index | low | 1 | medium | Point lookups only, no ranges |

## B-trees

Pages of sorted keys, updated in place. What PostgreSQL, MySQL/InnoDB and most
filesystems use.

- Reads are `O(log_B n)` with a very large branching factor, so 3-4 levels covers
  billions of keys — and the upper levels stay cached.
- Range scans are excellent because keys are physically ordered.
- Writes are expensive: modifying 8 bytes writes a whole page, plus a
  write-ahead-log entry. Random writes are random I/O.

The WAL is the durability mechanism worth being able to explain: append the
change sequentially, fsync, *then* modify pages lazily. Sequential writes are
cheap; the WAL turns random durability into sequential durability. Recovery
replays from the last checkpoint.

## LSM trees

Buffer writes in memory, flush sorted runs to disk, merge in the background.
RocksDB, LevelDB, Cassandra, ScyllaDB.

```
  write -> WAL (durability) + memtable (sorted, in memory)
  memtable full -> flush as an immutable SSTable at L0
  background compaction merges SSTables into larger, higher levels

  read -> memtable -> L0 -> L1 -> ... (stop at the first hit)
          each level checked via bloom filter + sparse index
```

Why writes are fast: every disk write is **sequential and batched**. Why reads
are slower: a key may live at any level, so a miss may touch all of them. Bloom
filters make the common case cheap — one per SSTable, so a level that does not
contain the key is usually skipped without any I/O.

**Compaction strategy is the real tuning knob**, and it is where the follow-ups
go:

- **Leveled**: each level is ~10x the previous, non-overlapping within a level.
  Low read and space amp, **high write amp** (a key is rewritten once per level).
- **Tiered / size-tiered**: merge same-sized runs. Low write amp, higher read and
  space amp. Cassandra's default.
- **Hybrid**: tiered at L0, leveled below. What RocksDB does in practice.

Being able to say "leveled trades write amplification for read amplification, and
which one you want depends on whether you are ingest-bound or query-bound" is a
complete answer.

Compaction is also the operational hazard: it competes for I/O and CPU with the
foreground workload, so **p99 latency spikes during compaction** are a classic
production problem. Rate-limiting compaction is a real knob.

## Time-series specifics

This is your domain, so know it properly. Time-series data has properties that
let you beat general-purpose engines by an order of magnitude:

1. **Writes are append-only and nearly time-ordered.** No random updates.
2. **Values change slowly.** Consecutive samples are highly correlated.
3. **Timestamps are regular.** 10 s apart, over and over.
4. **Queries are range scans over one series, or aggregations across many.**

The encodings that exploit this, from the Gorilla paper and used by Prometheus,
InfluxDB and friends:

```
  Timestamps: delta-of-delta
     10:00:00, 10:00:10, 10:00:20, 10:00:30
     deltas:        10,        10,        10
     delta-of-delta: 0,         0             -> a single bit each

  Values: XOR with the previous value
     Floats that change slightly share most of their bits.
     XOR leaves mostly zeros; encode leading/trailing zero counts.
     Typical result: ~1.3 bytes per sample, down from 16.
```

That compression ratio is why the Project 05 (`telemetrix`) arithmetic goes from 1.4 TB/day raw
to ~150 GB/day stored. Quote the mechanism, not just the ratio.

**Downsampling tiers** are the other half:

```
  10 s resolution    7 days      raw
   1 m resolution    90 days     min/max/avg/count per minute
   1 h resolution    2 years     same aggregates per hour
```

Storage falls by ~6x per tier while keeping the queries people actually run.
Note that you must store min/max/count, not just the average — averaging averages
is wrong when the underlying counts differ, and saying that shows care.

## Columnar

Store by column instead of by row. Parquet, ClickHouse, most analytics engines.

- A query touching 3 of 50 columns reads 3/50 of the data.
- Compression is far better because a column is homogeneous — run-length,
  dictionary and delta encodings all work well on one type.
- Terrible for single-row reads and updates; this is not an OLTP store.

The complement is **vectorised execution**: process a batch of column values at a
time, which is exactly the SIMD-friendly layout from `04_computer_organization/21_simd_and_vector_processing`. The
connection between columnar storage and SIMD is a nice cross-layer observation.

## Durability

```
  write() returns        data is in the page cache. A process crash is survivable,
                         a power loss is NOT.
  fsync() returns        data is on stable storage (if the device is honest).
  O_DIRECT               bypass the page cache; you manage caching yourself.
  fdatasync()            like fsync but skips metadata not needed for reading back.
```

Two things worth knowing precisely:

- **Group commit.** fsync is expensive (~100 us-1 ms). Batch many transactions
  into one fsync and the per-transaction cost collapses. Every serious database
  does this, and it is a great example of amortisation.
- **fsync error handling is genuinely broken-ish.** On Linux, a failed fsync may
  clear the error and a subsequent fsync can return success while the data is
  lost. This caused real data-loss bugs in PostgreSQL. Mentioning "fsyncgate" is
  a strong signal of depth.

## Choosing, out loud

```
  Write-heavy, key-value, ranges needed       -> LSM (RocksDB)
  Read-heavy, transactional, ad-hoc queries   -> B-tree (Postgres)
  Time-series, high ingest, range queries     -> purpose-built TSDB or LSM + delta encoding
  Analytics over few columns of many rows     -> columnar (Parquet, ClickHouse)
  Point lookups only, fits in memory          -> hash index, or just a hash map
  Immutable blobs                             -> object storage; do not build this
```

The last line matters: a surprising number of "design a storage system" answers
should end with "put the bytes in S3 and keep an index elsewhere."

## Interview angles

**"LSM vs B-tree?"**
The amplification table, then compaction strategies, then which one your workload
is bound by.

**"Design a time-series database."**
Delta-of-delta and XOR encoding with the byte figures, downsampling tiers with
min/max/count, and shard by series id plus time window.

**"How does a database guarantee durability?"**
WAL, fsync, group commit. Mention fsyncgate if you want to show depth.

**"Why is p99 spiking every few minutes?"**
Compaction, checkpoint flush, or GC. All three are background work competing with
the foreground — name the class, then the specific.

**"What compresses a float time series to 1.3 bytes?"**
XOR with the previous value plus leading/trailing zero encoding.

## Exercises

1. Implement delta-of-delta timestamp encoding and XOR float encoding. Run them
   over a real metric series (grab one from your own fleet if you can) and report
   the actual bytes per sample.
2. Build a toy LSM: memtable, SSTable flush, and leveled compaction. Measure
   write and read amplification directly by counting bytes.
3. Add bloom filters to it and measure the reduction in disk reads for missing
   keys. Sweep the bits-per-key parameter and plot false-positive rate against
   memory.
4. Benchmark `fsync` on your machine: per-write, and with group commit at batch
   sizes 1, 10, 100. Plot transactions per second.
5. Write the storage section of Project 05 (`telemetrix`): engine choice, encoding, tiering,
   retention, and the resulting cost per day. Defend each choice in one sentence.
