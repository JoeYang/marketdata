#include "order_book.h"

#include <algorithm>
#include <cstring>

namespace feedhandler {

// ============================================================================
// OrderBook
// ============================================================================

OrderBook::OrderBook(const std::string& symbol, size_t depth)
    : symbol_(symbol), depth_(depth) {
}

void OrderBook::addOrder(uint64_t order_ref, itch::Side side, uint32_t price, uint32_t qty) {
    Order order{order_ref, price, qty, side};
    orders_[order_ref] = order;
    
    if (side == itch::Side::Buy) {
        addToLevel(bids_, price, qty);
    } else {
        addToLevel(asks_, price, qty);
    }
    
    dirty_ = true;
}

void OrderBook::deleteOrder(uint64_t order_ref) {
    auto it = orders_.find(order_ref);
    if (it == orders_.end()) return;
    
    const Order& order = it->second;
    
    if (order.side == itch::Side::Buy) {
        removeFromLevel(bids_, order.price, order.remaining_qty);
    } else {
        removeFromLevel(asks_, order.price, order.remaining_qty);
    }
    
    orders_.erase(it);
    dirty_ = true;
}

void OrderBook::cancelOrder(uint64_t order_ref, uint32_t cancel_qty) {
    auto it = orders_.find(order_ref);
    if (it == orders_.end()) return;
    
    Order& order = it->second;
    uint32_t actual_cancel = std::min(cancel_qty, order.remaining_qty);
    
    if (order.side == itch::Side::Buy) {
        removeFromLevel(bids_, order.price, actual_cancel);
    } else {
        removeFromLevel(asks_, order.price, actual_cancel);
    }
    
    order.remaining_qty -= actual_cancel;
    if (order.remaining_qty == 0) {
        orders_.erase(it);
    }
    
    dirty_ = true;
}

void OrderBook::executeOrder(uint64_t order_ref, uint32_t exec_qty) {
    auto it = orders_.find(order_ref);
    if (it == orders_.end()) return;
    
    Order& order = it->second;
    uint32_t actual_exec = std::min(exec_qty, order.remaining_qty);
    
    if (order.side == itch::Side::Buy) {
        removeFromLevel(bids_, order.price, actual_exec);
    } else {
        removeFromLevel(asks_, order.price, actual_exec);
    }
    
    // Record as trade
    recordTrade(order.price, actual_exec, 
                order.side == itch::Side::Buy ? itch::Side::Sell : itch::Side::Buy);
    
    order.remaining_qty -= actual_exec;
    if (order.remaining_qty == 0) {
        orders_.erase(it);
    }
    
    dirty_ = true;
}

void OrderBook::replaceOrder(uint64_t old_ref, uint64_t new_ref, uint32_t price, uint32_t qty) {
    auto it = orders_.find(old_ref);
    if (it == orders_.end()) return;
    
    Order old_order = it->second;
    
    // Remove old order
    if (old_order.side == itch::Side::Buy) {
        removeFromLevel(bids_, old_order.price, old_order.remaining_qty);
    } else {
        removeFromLevel(asks_, old_order.price, old_order.remaining_qty);
    }
    orders_.erase(it);
    
    // Add new order
    addOrder(new_ref, old_order.side, price, qty);
}

void OrderBook::recordTrade(uint32_t price, uint32_t qty, itch::Side aggressor_side) {
    (void)aggressor_side; // May use later for trade direction
    last_price_ = price;
    last_qty_ = qty;
    total_volume_ += qty;
    dirty_ = true;
}

OrderBookSnapshot OrderBook::getSnapshot(uint64_t timestamp, uint64_t sequence) const {
    OrderBookSnapshot snap{};
    
    std::strncpy(snap.symbol, symbol_.c_str(), 8);
    snap.timestamp = timestamp;
    snap.sequence = sequence;
    
    // Fill bids
    snap.bids.count = 0;
    for (const auto& [price, level] : bids_) {
        if (snap.bids.count >= depth_) break;
        snap.bids.levels[snap.bids.count].price = price;
        snap.bids.levels[snap.bids.count].quantity = level.first;
        snap.bids.levels[snap.bids.count].order_count = level.second;
        snap.bids.count++;
    }
    
    // Fill asks
    snap.asks.count = 0;
    for (const auto& [price, level] : asks_) {
        if (snap.asks.count >= depth_) break;
        snap.asks.levels[snap.asks.count].price = price;
        snap.asks.levels[snap.asks.count].quantity = level.first;
        snap.asks.levels[snap.asks.count].order_count = level.second;
        snap.asks.count++;
    }
    
    snap.last_price = last_price_;
    snap.last_quantity = last_qty_;
    snap.total_volume = total_volume_;
    
    return snap;
}

QuoteUpdate OrderBook::getBBO(uint64_t timestamp, uint64_t sequence) const {
    QuoteUpdate quote{};
    
    std::strncpy(quote.symbol, symbol_.c_str(), 8);
    quote.timestamp = timestamp;
    quote.sequence = sequence;
    
    if (!bids_.empty()) {
        const auto& best_bid = *bids_.begin();
        quote.bid_price = best_bid.first;
        quote.bid_quantity = best_bid.second.first;
    }
    
    if (!asks_.empty()) {
        const auto& best_ask = *asks_.begin();
        quote.ask_price = best_ask.first;
        quote.ask_quantity = best_ask.second.first;
    }
    
    return quote;
}

void OrderBook::addToLevel(std::map<uint32_t, std::pair<uint32_t, uint32_t>, std::greater<uint32_t>>& levels,
                           uint32_t price, uint32_t qty) {
    auto& level = levels[price];
    level.first += qty;
    level.second++;
}

void OrderBook::addToLevel(std::map<uint32_t, std::pair<uint32_t, uint32_t>>& levels,
                           uint32_t price, uint32_t qty) {
    auto& level = levels[price];
    level.first += qty;
    level.second++;
}

void OrderBook::removeFromLevel(std::map<uint32_t, std::pair<uint32_t, uint32_t>, std::greater<uint32_t>>& levels,
                                uint32_t price, uint32_t qty) {
    auto it = levels.find(price);
    if (it == levels.end()) return;
    
    auto& level = it->second;
    level.first = (level.first > qty) ? level.first - qty : 0;
    level.second = (level.second > 0) ? level.second - 1 : 0;
    
    if (level.first == 0) {
        levels.erase(it);
    }
}

void OrderBook::removeFromLevel(std::map<uint32_t, std::pair<uint32_t, uint32_t>>& levels,
                                uint32_t price, uint32_t qty) {
    auto it = levels.find(price);
    if (it == levels.end()) return;
    
    auto& level = it->second;
    level.first = (level.first > qty) ? level.first - qty : 0;
    level.second = (level.second > 0) ? level.second - 1 : 0;
    
    if (level.first == 0) {
        levels.erase(it);
    }
}

// ============================================================================
// OrderBookManager
// ============================================================================

OrderBookManager::OrderBookManager(size_t depth) : depth_(depth) {}

OrderBook& OrderBookManager::getBook(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = books_.find(symbol);
    if (it == books_.end()) {
        auto [inserted, _] = books_.emplace(symbol, OrderBook(symbol, depth_));
        return inserted->second;
    }
    return it->second;
}

bool OrderBookManager::hasBook(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return books_.find(symbol) != books_.end();
}

std::vector<std::string> OrderBookManager::getDirtySymbols() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> dirty;
    
    for (const auto& [symbol, book] : books_) {
        if (book.isDirty()) {
            dirty.push_back(symbol);
        }
    }
    
    return dirty;
}

void OrderBookManager::clearAllDirty() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [symbol, book] : books_) {
        book.clearDirty();
    }
}

OrderBookSnapshot OrderBookManager::getSnapshot(const std::string& symbol, 
                                                 uint64_t timestamp, uint64_t sequence) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = books_.find(symbol);
    if (it == books_.end()) {
        return OrderBookSnapshot{};
    }
    
    return it->second.getSnapshot(timestamp, sequence);
}

} // namespace feedhandler
