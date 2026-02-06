#pragma once

#include "cme_protocol.h"
#include "src/feedhandler/market_data.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cme {

// Price level in CME order book
struct CmePriceLevel {
    int64_t price;          // CME price format (mantissa, -7 exponent)
    int32_t quantity;
    uint8_t order_count;

    CmePriceLevel() : price(0), quantity(0), order_count(0) {}
    CmePriceLevel(int64_t p, int32_t q, uint8_t oc)
        : price(p), quantity(q), order_count(oc) {}
};

constexpr size_t CME_MAX_DEPTH = 10;

// CME L2 Order Book for a single security
class CmeOrderBook {
public:
    explicit CmeOrderBook(uint32_t security_id);

    // Apply an incremental update entry
    void applyUpdate(const MDIncrementalRefreshEntry& entry);

    // Apply a full snapshot (replaces entire book)
    void applySnapshot(const MDSnapshotEntry* entries, uint8_t count);

    // Clear the book
    void clear();

    // Get snapshot in common output format
    feedhandler::OrderBookSnapshot getSnapshot() const;

    // Get last applied rpt_seq
    uint32_t getLastRptSeq() const { return last_rpt_seq_; }
    void setLastRptSeq(uint32_t seq) { last_rpt_seq_ = seq; }

    uint32_t getSecurityId() const { return security_id_; }
    const char* getSymbol() const { return getSymbolName(security_id_); }

    // Trade tracking
    void recordTrade(int64_t price, int32_t quantity);
    uint64_t getTotalVolume() const { return total_volume_; }

private:
    void applyBid(uint8_t level, MDUpdateAction action, int64_t price, int32_t qty, uint8_t orders);
    void applyAsk(uint8_t level, MDUpdateAction action, int64_t price, int32_t qty, uint8_t orders);

    uint32_t security_id_;
    uint32_t last_rpt_seq_ = 0;

    std::array<CmePriceLevel, CME_MAX_DEPTH> bids_;
    std::array<CmePriceLevel, CME_MAX_DEPTH> asks_;
    uint8_t bid_count_ = 0;
    uint8_t ask_count_ = 0;

    // Trade info
    int64_t last_trade_price_ = 0;
    int32_t last_trade_qty_ = 0;
    uint64_t total_volume_ = 0;
};

// Manages order books for multiple securities
class CmeOrderBookManager {
public:
    CmeOrderBookManager() = default;

    // Get or create book for security
    CmeOrderBook& getBook(uint32_t security_id);

    // Check if book exists
    bool hasBook(uint32_t security_id) const;

    // Apply incremental update, returns affected security_id
    uint32_t applyIncremental(const MDIncrementalRefreshEntry& entry);

    // Apply snapshot to specific security
    void applySnapshot(uint32_t security_id, const MDSnapshotEntry* entries, uint8_t count, uint32_t rpt_seq);

    // Mark a security as dirty (needs conflation)
    void markDirty(uint32_t security_id);

    // Get and clear dirty securities
    std::vector<uint32_t> getDirtySecurities();

    // Clear all books
    void clear();

    // Get all security IDs
    std::vector<uint32_t> getAllSecurityIds() const;

private:
    std::unordered_map<uint32_t, CmeOrderBook> books_;
    std::unordered_set<uint32_t> dirty_securities_;
};

} // namespace cme
