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
