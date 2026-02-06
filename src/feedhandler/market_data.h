#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace feedhandler {

// Price level in order book
struct PriceLevel {
    uint32_t price;         // Fixed-point (4 decimals)
    uint32_t quantity;
    uint32_t order_count;
    
    double getPriceAsDouble() const {
        return price / 10000.0;
    }
};

// Order book snapshot for one side
constexpr size_t MAX_DEPTH = 10;

struct BookSide {
    std::array<PriceLevel, MAX_DEPTH> levels;
    uint8_t count;          // Number of valid levels
};

// Full order book snapshot
struct OrderBookSnapshot {
    char symbol[8];
    uint64_t timestamp;     // Nanoseconds since midnight
    uint64_t sequence;      // Sequence number
    BookSide bids;
    BookSide asks;
    
    // Last trade info
    uint32_t last_price;
    uint32_t last_quantity;
    uint64_t total_volume;
    
    std::string getSymbol() const {
        return std::string(symbol, 8);
    }
};

// Trade tick
struct TradeTick {
    char symbol[8];
    uint64_t timestamp;
    uint64_t sequence;
    uint32_t price;
    uint32_t quantity;
    char side;              // 'B' or 'S'
    uint64_t match_number;
};

// Quote update (BBO)
struct QuoteUpdate {
    char symbol[8];
    uint64_t timestamp;
    uint64_t sequence;
    uint32_t bid_price;
    uint32_t bid_quantity;
    uint32_t ask_price;
    uint32_t ask_quantity;
};

#pragma pack(push, 1)

// Output message types
enum class OutputMessageType : uint8_t {
    Heartbeat = 0,
    OrderBookSnapshot = 1,
    TradeTick = 2,
    QuoteUpdate = 3,
};

// Output message header
struct OutputHeader {
    uint16_t length;
    OutputMessageType type;
    uint8_t flags;
    uint64_t timestamp;
};

#pragma pack(pop)

// Statistics
struct FeedStats {
    uint64_t messages_received = 0;
    uint64_t messages_sent = 0;
    uint64_t bytes_received = 0;
    uint64_t bytes_sent = 0;
    uint64_t add_orders = 0;
    uint64_t delete_orders = 0;
    uint64_t executions = 0;
    uint64_t trades = 0;
    uint64_t errors = 0;
};

} // namespace feedhandler
