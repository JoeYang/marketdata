# Market Data Feed Handler

High-performance C++ market data feed handler that receives ITCH protocol data via multicast and rebroadcasts processed data.

## Features

- **ITCH 5.0 Protocol** parsing
- **Multicast receive/send** with configurable addresses
- **Two output modes:**
  - Tick-by-tick (raw forwarding with processing)
  - Conflated (throttled snapshots at configurable intervals)
- **ITCH Simulator** for local testing

## Build

```bash
bazel build //...
```

## Run Feed Handler

```bash
# Tick-by-tick mode
bazel run //src:feed_handler -- --mode=tick

# Conflated mode (100ms intervals)
bazel run //src:feed_handler -- --mode=conflated --interval-ms=100
```

## Run Simulator

```bash
# Start ITCH simulator (sends test data)
bazel run //src/simulator:itch_simulator -- --multicast-group=239.1.1.1 --port=30001 --rate=1000
```

## Architecture

```
┌─────────────────┐     Multicast      ┌──────────────────┐     Multicast     ┌─────────────┐
│  ITCH Simulator │ ──────────────────▶│   Feed Handler   │ ─────────────────▶│  Consumers  │
│  (or Exchange)  │   239.1.1.1:30001  │                  │  239.1.1.2:30002  │             │
└─────────────────┘                    │  • Parse ITCH    │                   └─────────────┘
                                       │  • Build Book    │
                                       │  • Conflate      │
                                       └──────────────────┘
```

## ITCH 5.0 Feed Handler

The ITCH feed handler (`src/feedhandler/`) receives NASDAQ ITCH 5.0 messages over multicast, maintains per-symbol order books, and rebroadcasts processed market data to downstream consumers.

### Processing Modes

- **Tick-by-tick** (`--mode=tick`) -- Every order book update triggers an immediate BBO quote or trade tick on the output feed. Lowest latency, highest message rate.
- **Conflated** (`--mode=conflated --interval-ms=<ms>`) -- Order book updates are batched internally. At each conflation interval, only symbols with dirty books are published as full depth snapshots. Reduces output bandwidth at the cost of update latency.

### Order Book

Each symbol maintains an independent `OrderBook` backed by:
- `std::unordered_map<uint64_t, Order>` for O(1) order lookup by reference
- `std::map` (sorted) for bid levels (descending) and ask levels (ascending)
- Configurable depth (default 10 levels, via `--depth`)

Supported order operations: Add, Delete, Cancel, Execute, Replace.

### Output Messages

The handler emits three output message types over multicast, each prefixed with an `OutputHeader` (length, type, flags, timestamp):

| Type | Description |
|------|-------------|
| `OrderBookSnapshot` | Full depth snapshot (symbol, bid/ask levels, last trade, total volume) |
| `QuoteUpdate` | BBO update (best bid/ask price and quantity) |
| `TradeTick` | Trade event (symbol, price, quantity, side, match number) |

### CLI Options

```
--mode <tick|conflated>     Processing mode (default: tick)
--interval-ms <ms>          Conflation interval in ms (default: 100)
--input-group <ip>          Input multicast group (default: 239.1.1.1)
--input-port <port>         Input port (default: 30001)
--output-group <ip>         Output multicast group (default: 239.1.1.2)
--output-port <port>        Output port (default: 30002)
--interface <ip>            Network interface (default: 0.0.0.0)
--depth <n>                 Order book depth (default: 10)
--stats-interval <sec>      Stats print interval (default: 10)
```

## CME MDP 3.0 Recovery Logic

The CME feed handler implements per-security sequence-based recovery using the `RecoveryManager`. Each security tracks its own `rpt_seq` independently of the packet-level sequence numbers.

### State Machine

Each security transitions through three states:

```
                   gap in rpt_seq             snapshot received
  ┌────────┐    ─────────────────▶   ┌──────────────┐   ──────────────▶   ┌────────────┐
  │ Normal │                         │ GapDetected  │                     │ Recovering │
  └────────┘    ◀─────────────────   └──────────────┘                     └────────────┘
       ▲             timeout &                                                  │
       │             retry                                                      │
       └────────────────────────────────────────────────────────────────────────┘
                              snapshot applied → completeRecovery()
```

- **Normal** -- Incremental messages are validated against `expected_rpt_seq` and applied to the order book. Duplicate/old messages are discarded.
- **GapDetected** -- A sequence gap was detected (`rpt_seq > expected`). Incremental messages are dropped while the handler waits for a snapshot on the snapshot feed.
- **Recovering** -- A snapshot has been received. The snapshot is applied to the order book and `completeRecovery()` transitions the security back to Normal, resuming incremental processing from the snapshot's `rpt_seq`.

### Key Behaviors

- **Snapshot feed is only read when recovery is needed** (`recovery_manager_.needsRecovery()`), avoiding unnecessary processing during steady state.
- **Recovery timeout** (default 5s, configurable via `recovery_timeout_ms`) -- if no valid snapshot arrives within the timeout, the attempt counter increments and the timer resets, waiting for the next snapshot cycle.
- **Channel reset** clears all order books and resets all securities' expected sequences back to 1.
- **Dirty-book publishing** -- only securities in `Normal` state are published during conflation; recovering securities are suppressed until recovery completes.

### Recovery Stats

The `RecoveryManager` tracks:
- `gaps_detected` -- total gap events across all securities
- `recoveries_completed` -- successful snapshot recoveries
- `messages_dropped` -- incrementals discarded during recovery
- `messages_buffered` -- incrementals buffered during recovery (reserved for future use)

## Configuration

See `config/feedhandler.yaml` for all options.

## ITCH Message Types Supported

- `S` - System Event
- `R` - Stock Directory  
- `A` - Add Order
- `F` - Add Order (MPID)
- `E` - Order Executed
- `C` - Order Executed with Price
- `X` - Order Cancel
- `D` - Order Delete
- `U` - Order Replace
- `P` - Trade (non-cross)
- `Q` - Cross Trade
