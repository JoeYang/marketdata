# CME MDP 3.0 Market Data System

A simulated CME MDP 3.0 market data system with feed handler, gap recovery, and SBE-encoded output.

## Architecture

```
┌─────────────────┐
│  CME SIMULATOR  │
│                 │
│  Incremental ───┼──► 239.2.1.1:40001  ──┐
│  Snapshot ──────┼──► 239.2.1.2:40002  ──┼──┐
└─────────────────┘                       │  │
                                          ▼  ▼
                              ┌───────────────────┐
                              │ CME FEED HANDLER  │
                              │                   │
                              │ • Gap Detection   │
                              │ • Recovery        │
                              │ • L2 Book Builder │
                              │ • Conflation      │
                              │                   │
                              │ Output ───────────┼──► 239.2.1.3:40003 (SBE)
                              └───────────────────┘          │
                                                             ▼
                                                  ┌─────────────────┐
                                                  │  CME RECEIVER   │
                                                  │  Prints L2 Book │
                                                  └─────────────────┘
```

## Project Structure

```
src/
├── cme/                          # Feed Handler
│   ├── BUILD.bazel
│   ├── cme_protocol.h            # MDP 3.0 message definitions
│   ├── cme_order_book.h/cpp      # Price-level L2 order book
│   ├── recovery_state.h/cpp      # Gap detection state machine
│   ├── cme_feedhandler.h/cpp     # Main feed handler logic
│   ├── main.cpp                  # Feed handler entry point
│   ├── l2_sbe_messages.h         # SBE encoder/decoder
│   └── sbe_schema.xml            # FIX SBE schema definition
│
├── cme_simulator/                # Market Data Simulator
│   ├── BUILD.bazel
│   ├── cme_simulator.h/cpp       # Simulator logic
│   └── main.cpp                  # Simulator entry point
│
└── cme_receiver/                 # Consumer
    ├── BUILD.bazel
    └── cme_receiver.cpp          # Prints L2 snapshots
```

## Building

```bash
# Build all components
bazel build //src/cme_simulator:cme_simulator \
            //src/cme:cme_feedhandler \
            //src/cme_receiver:cme_receiver

# Build individual components
bazel build //src/cme_simulator:cme_simulator
bazel build //src/cme:cme_feedhandler
bazel build //src/cme_receiver:cme_receiver
```

## Running

Start each component in a separate terminal:

```bash
# Terminal 1: Start the simulator
./bazel-bin/src/cme_simulator/cme_simulator

# Terminal 2: Start the feed handler
./bazel-bin/src/cme/cme_feedhandler

# Terminal 3: Start the receiver
./bazel-bin/src/cme_receiver/cme_receiver
```

## Command Line Options

### Simulator
```
./bazel-bin/src/cme_simulator/cme_simulator [options]

Options:
  --interface <ip>          Network interface (default: 0.0.0.0)
  --rate <n>                Updates per second (default: 100)
  --snapshot-interval <ms>  Snapshot interval in ms (default: 1000)
  --simulate-gaps           Simulate packet gaps for testing recovery
  --gap-frequency <n>       Gap every N packets (default: 100)
  -h, --help                Show help
```

### Feed Handler
```
./bazel-bin/src/cme/cme_feedhandler [options]

Options:
  --interface <ip>           Network interface (default: 0.0.0.0)
  --conflation-interval <ms> Conflation interval in ms (default: 100)
  --recovery-timeout <ms>    Recovery timeout in ms (default: 5000)
  -h, --help                 Show help
```

### Receiver
```
./bazel-bin/src/cme_receiver/cme_receiver [options]

Options:
  --group <ip>        Multicast group (default: 239.2.1.3)
  --port <port>       Port (default: 40003)
  --interface <ip>    Network interface (default: 0.0.0.0)
  --filter <symbol>   Only show this symbol (e.g., ESH26)
  --raw               Show raw SBE message details
  -h, --help          Show help
```

## Symbols

The simulator generates data for 4 CME futures contracts:

| Symbol | Security ID | Description |
|--------|-------------|-------------|
| ESH26  | 1001        | E-mini S&P 500 Mar 2026 |
| NQM26  | 1002        | E-mini NASDAQ Jun 2026 |
| CLK26  | 1003        | Crude Oil May 2026 |
| GCZ26  | 1004        | Gold Dec 2026 |

## Multicast Channels

| Channel | Address | Port | Description |
|---------|---------|------|-------------|
| Incremental | 239.2.1.1 | 40001 | Real-time updates |
| Snapshot | 239.2.1.2 | 40002 | Periodic full snapshots |
| Output | 239.2.1.3 | 40003 | Feed handler output (SBE) |

## SBE Wire Format

The feed handler publishes L2 snapshots using standard SBE encoding (Schema ID: 1, Version: 1).

### Message Structure

```
┌─────────────────────────────────────────────────────────┐
│              Message Header (8 bytes)                   │
│  blockLength=46, templateId=2, schemaId=1, version=1    │
├─────────────────────────────────────────────────────────┤
│              L2Snapshot Root Block (46 bytes)           │
│  symbol[8], timestamp, sequenceNumber, lastTradePrice,  │
│  lastTradeQty, totalVolume, bidCount, askCount          │
├─────────────────────────────────────────────────────────┤
│              Bids Group Header (3 bytes)                │
│  blockLength=15, numInGroup=N                           │
├─────────────────────────────────────────────────────────┤
│              Bid Entries (15 bytes each × N)            │
│  level(1), price(8), quantity(4), numOrders(2)          │
├─────────────────────────────────────────────────────────┤
│              Asks Group Header (3 bytes)                │
│  blockLength=15, numInGroup=M                           │
├─────────────────────────────────────────────────────────┤
│              Ask Entries (15 bytes each × M)            │
└─────────────────────────────────────────────────────────┘
```

### Price Encoding

Prices are encoded as `int64` with 7 implied decimal places:
- Wire value `45002500000` = $4500.25
- Wire value `20001000000` = $2000.10

### External Integration

External software can subscribe to `239.2.1.3:40003` and decode messages using:
- Schema: `src/cme/sbe_schema.xml`
- C++ Header: `src/cme/l2_sbe_messages.h`

## Testing Gap Recovery

```bash
# Start simulator with gap simulation
./bazel-bin/src/cme_simulator/cme_simulator --simulate-gaps --gap-frequency 50

# Watch feed handler recover from gaps
./bazel-bin/src/cme/cme_feedhandler
```

The feed handler will detect gaps and recover using the snapshot channel.

## Example Output

```
ESH26 @ 2026-02-05 22:27:50.216911 (seq=34)
  BID                    ASK
  ---                    ---
    209 @    4499.25      219 @    4499.75
    121 @    4499.00      236 @    4500.00
    136 @    4498.75      175 @    4500.25
```
