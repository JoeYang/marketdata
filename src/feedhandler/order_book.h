#pragma once

#include "market_data.h"
#include "itch_protocol.h"

#include <map>
#include <unordered_map>
#include <string>
#include <mutex>

namespace feedhandler {

// Single order in the book
struct Order {
    uint64_t order_ref;
    uint32_t price;
    uint32_t remaining_qty;
    itch::Side side;
};

// Order book for a single symbol
class OrderBook {
public:
    explicit OrderBook(const std::string& symbol, size_t depth = MAX_DEPTH);
    
    // Order operations
    void addOrder(uint64_t order_ref, itch::Side side, uint32_t price, uint32_t qty);
    void deleteOrder(uint64_t order_ref);
    void cancelOrder(uint64_t order_ref, uint32_t cancel_qty);
    void executeOrder(uint64_t order_ref, uint32_t exec_qty);
    void replaceOrder(uint64_t old_ref, uint64_t new_ref, uint32_t price, uint32_t qty);
    
    // Trade handling
    void recordTrade(uint32_t price, uint32_t qty, itch::Side aggressor_side);
    
    // Snapshot
    OrderBookSnapshot getSnapshot(uint64_t timestamp, uint64_t sequence) const;
    
    // BBO
    QuoteUpdate getBBO(uint64_t timestamp, uint64_t sequence) const;
    
    // Stats
    const std::string& getSymbol() const { return symbol_; }
    bool isDirty() const { return dirty_; }
    void clearDirty() { dirty_ = false; }
    
private:
    std::string symbol_;
    size_t depth_;
    bool dirty_ = false;
    
    // Orders by reference
    std::unordered_map<uint64_t, Order> orders_;
    
    // Price levels: price -> {total_qty, order_count}
    // For bids: use reverse order (descending)
    std::map<uint32_t, std::pair<uint32_t, uint32_t>, std::greater<uint32_t>> bids_;
    std::map<uint32_t, std::pair<uint32_t, uint32_t>> asks_;
    
    // Last trade
    uint32_t last_price_ = 0;
    uint32_t last_qty_ = 0;
    uint64_t total_volume_ = 0;
    
    void addToLevel(std::map<uint32_t, std::pair<uint32_t, uint32_t>, std::greater<uint32_t>>& levels,
                    uint32_t price, uint32_t qty);
    void addToLevel(std::map<uint32_t, std::pair<uint32_t, uint32_t>>& levels,
                    uint32_t price, uint32_t qty);
    void removeFromLevel(std::map<uint32_t, std::pair<uint32_t, uint32_t>, std::greater<uint32_t>>& levels,
                         uint32_t price, uint32_t qty);
    void removeFromLevel(std::map<uint32_t, std::pair<uint32_t, uint32_t>>& levels,
                         uint32_t price, uint32_t qty);
};

// Order book manager for all symbols
class OrderBookManager {
public:
    explicit OrderBookManager(size_t depth = MAX_DEPTH);
    
    // Get or create book for symbol
    OrderBook& getBook(const std::string& symbol);
    
    // Check if symbol exists
    bool hasBook(const std::string& symbol) const;
    
    // Get all dirty books
    std::vector<std::string> getDirtySymbols() const;
    
    // Clear all dirty flags
    void clearAllDirty();
    
    // Get snapshot for symbol
    OrderBookSnapshot getSnapshot(const std::string& symbol, uint64_t timestamp, uint64_t sequence);
    
private:
    size_t depth_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, OrderBook> books_;
};

} // namespace feedhandler
