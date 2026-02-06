#include "cme_order_book.h"

#include <algorithm>

namespace cme {

CmeOrderBook::CmeOrderBook(uint32_t security_id)
    : security_id_(security_id) {
    clear();
}

void CmeOrderBook::clear() {
    for (auto& level : bids_) {
        level = CmePriceLevel();
    }
    for (auto& level : asks_) {
        level = CmePriceLevel();
    }
    bid_count_ = 0;
    ask_count_ = 0;
}

void CmeOrderBook::applyUpdate(const MDIncrementalRefreshEntry& entry) {
    auto action = static_cast<MDUpdateAction>(entry.md_update_action);
    auto type = static_cast<MDEntryType>(entry.md_entry_type);

    if (type == MDEntryType::Bid || type == MDEntryType::ImpliedBid) {
        applyBid(entry.md_price_level, action, entry.md_entry_px,
                 entry.md_entry_size, entry.number_of_orders);
    } else if (type == MDEntryType::Offer || type == MDEntryType::ImpliedOffer) {
        applyAsk(entry.md_price_level, action, entry.md_entry_px,
                 entry.md_entry_size, entry.number_of_orders);
    } else if (type == MDEntryType::Trade) {
        recordTrade(entry.md_entry_px, entry.md_entry_size);
    }

    if (entry.rpt_seq > last_rpt_seq_) {
        last_rpt_seq_ = entry.rpt_seq;
    }
}

void CmeOrderBook::applyBid(uint8_t level, MDUpdateAction action,
                            int64_t price, int32_t qty, uint8_t orders) {
    // CME levels are 1-based, convert to 0-based
    if (level == 0 || level > CME_MAX_DEPTH) return;
    size_t idx = level - 1;

    switch (action) {
        case MDUpdateAction::New:
            // Shift levels down to make room
            for (size_t i = CME_MAX_DEPTH - 1; i > idx; --i) {
                bids_[i] = bids_[i - 1];
            }
            bids_[idx] = CmePriceLevel(price, qty, orders);
            if (bid_count_ < CME_MAX_DEPTH) bid_count_++;
            break;

        case MDUpdateAction::Change:
            bids_[idx] = CmePriceLevel(price, qty, orders);
            break;

        case MDUpdateAction::Delete:
            // Shift levels up
            for (size_t i = idx; i < CME_MAX_DEPTH - 1; ++i) {
                bids_[i] = bids_[i + 1];
            }
            bids_[CME_MAX_DEPTH - 1] = CmePriceLevel();
            if (bid_count_ > 0) bid_count_--;
            break;

        case MDUpdateAction::DeleteThru:
            // Delete from top through this level
            for (size_t i = 0; i <= idx && i < CME_MAX_DEPTH; ++i) {
                bids_[i] = CmePriceLevel();
            }
            bid_count_ = 0;
            break;

        case MDUpdateAction::DeleteFrom:
            // Delete from this level to bottom
            for (size_t i = idx; i < CME_MAX_DEPTH; ++i) {
                bids_[i] = CmePriceLevel();
            }
            bid_count_ = static_cast<uint8_t>(idx);
            break;

        case MDUpdateAction::Overlay:
            bids_[idx] = CmePriceLevel(price, qty, orders);
            // Ensure count includes this level
            if (idx + 1 > bid_count_) {
                bid_count_ = static_cast<uint8_t>(idx + 1);
            }
            break;
    }
}

void CmeOrderBook::applyAsk(uint8_t level, MDUpdateAction action,
                            int64_t price, int32_t qty, uint8_t orders) {
    if (level == 0 || level > CME_MAX_DEPTH) return;
    size_t idx = level - 1;

    switch (action) {
        case MDUpdateAction::New:
            for (size_t i = CME_MAX_DEPTH - 1; i > idx; --i) {
                asks_[i] = asks_[i - 1];
            }
            asks_[idx] = CmePriceLevel(price, qty, orders);
            if (ask_count_ < CME_MAX_DEPTH) ask_count_++;
            break;

        case MDUpdateAction::Change:
            asks_[idx] = CmePriceLevel(price, qty, orders);
            break;

        case MDUpdateAction::Delete:
            for (size_t i = idx; i < CME_MAX_DEPTH - 1; ++i) {
                asks_[i] = asks_[i + 1];
            }
            asks_[CME_MAX_DEPTH - 1] = CmePriceLevel();
            if (ask_count_ > 0) ask_count_--;
            break;

        case MDUpdateAction::DeleteThru:
            for (size_t i = 0; i <= idx && i < CME_MAX_DEPTH; ++i) {
                asks_[i] = CmePriceLevel();
            }
            ask_count_ = 0;
            break;

        case MDUpdateAction::DeleteFrom:
            for (size_t i = idx; i < CME_MAX_DEPTH; ++i) {
                asks_[i] = CmePriceLevel();
            }
            ask_count_ = static_cast<uint8_t>(idx);
            break;

        case MDUpdateAction::Overlay:
            asks_[idx] = CmePriceLevel(price, qty, orders);
            // Ensure count includes this level
            if (idx + 1 > ask_count_) {
                ask_count_ = static_cast<uint8_t>(idx + 1);
            }
            break;
    }
}

void CmeOrderBook::recordTrade(int64_t price, int32_t quantity) {
    last_trade_price_ = price;
    last_trade_qty_ = quantity;
    total_volume_ += static_cast<uint64_t>(quantity);
}

void CmeOrderBook::applySnapshot(const MDSnapshotEntry* entries, uint8_t count) {
    clear();

    for (uint8_t i = 0; i < count; ++i) {
        const auto& entry = entries[i];
        auto type = static_cast<MDEntryType>(entry.md_entry_type);

        if (entry.md_price_level == 0 || entry.md_price_level > CME_MAX_DEPTH) {
            continue;
        }

        size_t idx = entry.md_price_level - 1;

        if (type == MDEntryType::Bid) {
            bids_[idx] = CmePriceLevel(entry.md_entry_px, entry.md_entry_size, entry.number_of_orders);
            if (idx + 1 > bid_count_) {
                bid_count_ = static_cast<uint8_t>(idx + 1);
            }
        } else if (type == MDEntryType::Offer) {
            asks_[idx] = CmePriceLevel(entry.md_entry_px, entry.md_entry_size, entry.number_of_orders);
            if (idx + 1 > ask_count_) {
                ask_count_ = static_cast<uint8_t>(idx + 1);
            }
        }
    }
}

feedhandler::OrderBookSnapshot CmeOrderBook::getSnapshot() const {
    feedhandler::OrderBookSnapshot snap = {};

    const char* sym = getSymbol();
    std::strncpy(snap.symbol, sym, sizeof(snap.symbol) - 1);
    snap.symbol[sizeof(snap.symbol) - 1] = '\0';

    snap.sequence = last_rpt_seq_;

    // Convert bids
    snap.bids.count = std::min(static_cast<size_t>(bid_count_), feedhandler::MAX_DEPTH);
    for (size_t i = 0; i < snap.bids.count; ++i) {
        snap.bids.levels[i].price = cmeToFixedPrice(bids_[i].price);
        snap.bids.levels[i].quantity = static_cast<uint32_t>(bids_[i].quantity);
        snap.bids.levels[i].order_count = bids_[i].order_count;
    }

    // Convert asks
    snap.asks.count = std::min(static_cast<size_t>(ask_count_), feedhandler::MAX_DEPTH);
    for (size_t i = 0; i < snap.asks.count; ++i) {
        snap.asks.levels[i].price = cmeToFixedPrice(asks_[i].price);
        snap.asks.levels[i].quantity = static_cast<uint32_t>(asks_[i].quantity);
        snap.asks.levels[i].order_count = asks_[i].order_count;
    }

    snap.last_price = cmeToFixedPrice(last_trade_price_);
    snap.last_quantity = static_cast<uint32_t>(last_trade_qty_);
    snap.total_volume = total_volume_;

    return snap;
}

// CmeOrderBookManager implementation

CmeOrderBook& CmeOrderBookManager::getBook(uint32_t security_id) {
    auto it = books_.find(security_id);
    if (it == books_.end()) {
        auto result = books_.emplace(security_id, CmeOrderBook(security_id));
        return result.first->second;
    }
    return it->second;
}

bool CmeOrderBookManager::hasBook(uint32_t security_id) const {
    return books_.find(security_id) != books_.end();
}

uint32_t CmeOrderBookManager::applyIncremental(const MDIncrementalRefreshEntry& entry) {
    auto& book = getBook(entry.security_id);
    book.applyUpdate(entry);
    markDirty(entry.security_id);
    return entry.security_id;
}

void CmeOrderBookManager::applySnapshot(uint32_t security_id,
                                        const MDSnapshotEntry* entries,
                                        uint8_t count,
                                        uint32_t rpt_seq) {
    auto& book = getBook(security_id);
    book.applySnapshot(entries, count);
    book.setLastRptSeq(rpt_seq);
    markDirty(security_id);
}

void CmeOrderBookManager::markDirty(uint32_t security_id) {
    dirty_securities_.insert(security_id);
}

std::vector<uint32_t> CmeOrderBookManager::getDirtySecurities() {
    std::vector<uint32_t> result(dirty_securities_.begin(), dirty_securities_.end());
    dirty_securities_.clear();
    return result;
}

void CmeOrderBookManager::clear() {
    books_.clear();
    dirty_securities_.clear();
}

std::vector<uint32_t> CmeOrderBookManager::getAllSecurityIds() const {
    std::vector<uint32_t> ids;
    ids.reserve(books_.size());
    for (const auto& pair : books_) {
        ids.push_back(pair.first);
    }
    return ids;
}

} // namespace cme
