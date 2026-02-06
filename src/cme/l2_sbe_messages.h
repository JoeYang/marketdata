#pragma once

// L2 Market Data SBE Messages
// Generated from sbe_schema.xml - Schema ID: 1, Version: 1
//
// Wire format (little-endian):
//
// ┌─────────────────────────────────────────────────────────┐
// │                  Message Header (8 bytes)               │
// ├─────────────────────────────────────────────────────────┤
// │  blockLength  (uint16)  - Root block size               │
// │  templateId   (uint16)  - Message type (1=HB, 2=L2)     │
// │  schemaId     (uint16)  - Schema ID (1)                 │
// │  version      (uint16)  - Schema version (1)            │
// ├─────────────────────────────────────────────────────────┤
// │              L2Snapshot Root Block (46 bytes)           │
// ├─────────────────────────────────────────────────────────┤
// │  symbol[8]         (char[8])                            │
// │  timestamp         (uint64)  - ns since epoch           │
// │  sequenceNumber    (uint64)                             │
// │  lastTradePrice    (int64)   - 7 decimal places         │
// │  lastTradeQty      (uint32)                             │
// │  totalVolume       (uint64)                             │
// │  bidCount          (uint8)   - valid bid levels         │
// │  askCount          (uint8)   - valid ask levels         │
// ├─────────────────────────────────────────────────────────┤
// │              Bids Group Header (3 bytes)                │
// ├─────────────────────────────────────────────────────────┤
// │  blockLength  (uint16)  - Entry size (15 bytes)         │
// │  numInGroup   (uint8)   - Number of bid entries         │
// ├─────────────────────────────────────────────────────────┤
// │              Bid Entries (15 bytes each)                │
// ├─────────────────────────────────────────────────────────┤
// │  level        (uint8)                                   │
// │  price        (int64)   - 7 decimal places              │
// │  quantity     (uint32)                                  │
// │  numOrders    (uint16)                                  │
// ├─────────────────────────────────────────────────────────┤
// │              Asks Group Header (3 bytes)                │
// ├─────────────────────────────────────────────────────────┤
// │  blockLength  (uint16)  - Entry size (15 bytes)         │
// │  numInGroup   (uint8)   - Number of ask entries         │
// ├─────────────────────────────────────────────────────────┤
// │              Ask Entries (15 bytes each)                │
// └─────────────────────────────────────────────────────────┘

#include <cstdint>
#include <cstring>
#include <string>

namespace l2md {

// Schema constants
constexpr uint16_t SCHEMA_ID = 1;
constexpr uint16_t SCHEMA_VERSION = 1;

// Template IDs
constexpr uint16_t TEMPLATE_HEARTBEAT = 1;
constexpr uint16_t TEMPLATE_L2_SNAPSHOT = 2;

// Max levels
constexpr uint8_t MAX_LEVELS = 10;

#pragma pack(push, 1)

// SBE Message Header (8 bytes)
struct MessageHeader {
    uint16_t blockLength;
    uint16_t templateId;
    uint16_t schemaId;
    uint16_t version;
};

// Group Size Encoding (3 bytes)
struct GroupHeader {
    uint16_t blockLength;
    uint8_t numInGroup;
};

// Price Level Entry (15 bytes)
struct PriceLevelEntry {
    uint8_t level;          // 1-based level
    int64_t price;          // Price with 7 implied decimals
    uint32_t quantity;
    uint16_t numOrders;
};

// L2 Snapshot Root Block (58 bytes)
struct L2SnapshotRoot {
    char symbol[8];
    uint64_t timestamp;
    uint64_t sequenceNumber;
    int64_t lastTradePrice;
    uint32_t lastTradeQty;
    uint64_t totalVolume;
    uint8_t bidCount;       // Number of valid bid levels (for display)
    uint8_t askCount;       // Number of valid ask levels (for display)
};

// Heartbeat Root Block (16 bytes)
struct HeartbeatRoot {
    uint64_t timestamp;
    uint64_t sequenceNumber;
};

#pragma pack(pop)

// Compile-time size verification
static_assert(sizeof(MessageHeader) == 8, "MessageHeader must be 8 bytes");
static_assert(sizeof(GroupHeader) == 3, "GroupHeader must be 3 bytes");
static_assert(sizeof(PriceLevelEntry) == 15, "PriceLevelEntry must be 15 bytes");
static_assert(sizeof(L2SnapshotRoot) == 46, "L2SnapshotRoot must be 46 bytes");
static_assert(sizeof(HeartbeatRoot) == 16, "HeartbeatRoot must be 16 bytes");

// Calculate message sizes
constexpr size_t HEARTBEAT_SIZE = sizeof(MessageHeader) + sizeof(HeartbeatRoot);

inline size_t calcL2SnapshotSize(uint8_t numBids, uint8_t numAsks) {
    return sizeof(MessageHeader) +
           sizeof(L2SnapshotRoot) +
           sizeof(GroupHeader) + (numBids * sizeof(PriceLevelEntry)) +
           sizeof(GroupHeader) + (numAsks * sizeof(PriceLevelEntry));
}

constexpr size_t MAX_L2_SNAPSHOT_SIZE = sizeof(MessageHeader) +
                                        sizeof(L2SnapshotRoot) +
                                        sizeof(GroupHeader) + (MAX_LEVELS * sizeof(PriceLevelEntry)) +
                                        sizeof(GroupHeader) + (MAX_LEVELS * sizeof(PriceLevelEntry));

// ============================================================================
// Encoder Classes
// ============================================================================

class L2SnapshotEncoder {
public:
    L2SnapshotEncoder(uint8_t* buffer, size_t bufferSize)
        : buffer_(buffer), bufferSize_(bufferSize), offset_(0) {}

    bool encode(const char* symbol,
                uint64_t timestamp,
                uint64_t sequenceNumber,
                int64_t lastTradePrice,
                uint32_t lastTradeQty,
                uint64_t totalVolume,
                uint8_t bidCount,
                uint8_t askCount,
                const PriceLevelEntry* bids,
                uint8_t numBids,
                const PriceLevelEntry* asks,
                uint8_t numAsks) {

        size_t requiredSize = calcL2SnapshotSize(numBids, numAsks);
        if (bufferSize_ < requiredSize) {
            return false;
        }

        // Message Header
        auto* header = reinterpret_cast<MessageHeader*>(buffer_);
        header->blockLength = sizeof(L2SnapshotRoot);
        header->templateId = TEMPLATE_L2_SNAPSHOT;
        header->schemaId = SCHEMA_ID;
        header->version = SCHEMA_VERSION;
        offset_ = sizeof(MessageHeader);

        // Root Block
        auto* root = reinterpret_cast<L2SnapshotRoot*>(buffer_ + offset_);
        std::memset(root->symbol, 0, sizeof(root->symbol));
        std::strncpy(root->symbol, symbol, sizeof(root->symbol));
        root->timestamp = timestamp;
        root->sequenceNumber = sequenceNumber;
        root->lastTradePrice = lastTradePrice;
        root->lastTradeQty = lastTradeQty;
        root->totalVolume = totalVolume;
        root->bidCount = bidCount;
        root->askCount = askCount;
        offset_ += sizeof(L2SnapshotRoot);

        // Bids Group
        auto* bidsHeader = reinterpret_cast<GroupHeader*>(buffer_ + offset_);
        bidsHeader->blockLength = sizeof(PriceLevelEntry);
        bidsHeader->numInGroup = numBids;
        offset_ += sizeof(GroupHeader);

        for (uint8_t i = 0; i < numBids; ++i) {
            auto* entry = reinterpret_cast<PriceLevelEntry*>(buffer_ + offset_);
            *entry = bids[i];
            offset_ += sizeof(PriceLevelEntry);
        }

        // Asks Group
        auto* asksHeader = reinterpret_cast<GroupHeader*>(buffer_ + offset_);
        asksHeader->blockLength = sizeof(PriceLevelEntry);
        asksHeader->numInGroup = numAsks;
        offset_ += sizeof(GroupHeader);

        for (uint8_t i = 0; i < numAsks; ++i) {
            auto* entry = reinterpret_cast<PriceLevelEntry*>(buffer_ + offset_);
            *entry = asks[i];
            offset_ += sizeof(PriceLevelEntry);
        }

        return true;
    }

    size_t encodedLength() const { return offset_; }

private:
    uint8_t* buffer_;
    size_t bufferSize_;
    size_t offset_;
};

class HeartbeatEncoder {
public:
    HeartbeatEncoder(uint8_t* buffer, size_t bufferSize)
        : buffer_(buffer), bufferSize_(bufferSize) {}

    bool encode(uint64_t timestamp, uint64_t sequenceNumber) {
        if (bufferSize_ < HEARTBEAT_SIZE) {
            return false;
        }

        auto* header = reinterpret_cast<MessageHeader*>(buffer_);
        header->blockLength = sizeof(HeartbeatRoot);
        header->templateId = TEMPLATE_HEARTBEAT;
        header->schemaId = SCHEMA_ID;
        header->version = SCHEMA_VERSION;

        auto* root = reinterpret_cast<HeartbeatRoot*>(buffer_ + sizeof(MessageHeader));
        root->timestamp = timestamp;
        root->sequenceNumber = sequenceNumber;

        return true;
    }

    size_t encodedLength() const { return HEARTBEAT_SIZE; }

private:
    uint8_t* buffer_;
    size_t bufferSize_;
};

// ============================================================================
// Decoder Classes
// ============================================================================

class MessageDecoder {
public:
    MessageDecoder(const uint8_t* buffer, size_t length)
        : buffer_(buffer), length_(length) {}

    bool isValid() const {
        if (length_ < sizeof(MessageHeader)) return false;
        const auto* header = getHeader();
        return header->schemaId == SCHEMA_ID;
    }

    const MessageHeader* getHeader() const {
        return reinterpret_cast<const MessageHeader*>(buffer_);
    }

    uint16_t templateId() const {
        return getHeader()->templateId;
    }

    bool isHeartbeat() const {
        return templateId() == TEMPLATE_HEARTBEAT;
    }

    bool isL2Snapshot() const {
        return templateId() == TEMPLATE_L2_SNAPSHOT;
    }

protected:
    const uint8_t* buffer_;
    size_t length_;
};

class L2SnapshotDecoder : public MessageDecoder {
public:
    L2SnapshotDecoder(const uint8_t* buffer, size_t length)
        : MessageDecoder(buffer, length) {
        parse();
    }

    bool isValid() const {
        return MessageDecoder::isValid() &&
               isL2Snapshot() &&
               root_ != nullptr;
    }

    // Root fields
    std::string symbol() const {
        return std::string(root_->symbol, 8);
    }

    const char* symbolRaw() const { return root_->symbol; }
    uint64_t timestamp() const { return root_->timestamp; }
    uint64_t sequenceNumber() const { return root_->sequenceNumber; }
    int64_t lastTradePrice() const { return root_->lastTradePrice; }
    uint32_t lastTradeQty() const { return root_->lastTradeQty; }
    uint64_t totalVolume() const { return root_->totalVolume; }
    uint8_t bidCount() const { return root_->bidCount; }
    uint8_t askCount() const { return root_->askCount; }

    // Bid levels
    uint8_t numBids() const { return bidsHeader_ ? bidsHeader_->numInGroup : 0; }

    const PriceLevelEntry* getBid(uint8_t index) const {
        if (index >= numBids()) return nullptr;
        return &bids_[index];
    }

    // Ask levels
    uint8_t numAsks() const { return asksHeader_ ? asksHeader_->numInGroup : 0; }

    const PriceLevelEntry* getAsk(uint8_t index) const {
        if (index >= numAsks()) return nullptr;
        return &asks_[index];
    }

private:
    void parse() {
        if (length_ < sizeof(MessageHeader) + sizeof(L2SnapshotRoot)) {
            return;
        }

        size_t offset = sizeof(MessageHeader);
        root_ = reinterpret_cast<const L2SnapshotRoot*>(buffer_ + offset);
        offset += sizeof(L2SnapshotRoot);

        // Parse bids group
        if (offset + sizeof(GroupHeader) > length_) return;
        bidsHeader_ = reinterpret_cast<const GroupHeader*>(buffer_ + offset);
        offset += sizeof(GroupHeader);

        if (offset + bidsHeader_->numInGroup * sizeof(PriceLevelEntry) > length_) return;
        bids_ = reinterpret_cast<const PriceLevelEntry*>(buffer_ + offset);
        offset += bidsHeader_->numInGroup * sizeof(PriceLevelEntry);

        // Parse asks group
        if (offset + sizeof(GroupHeader) > length_) return;
        asksHeader_ = reinterpret_cast<const GroupHeader*>(buffer_ + offset);
        offset += sizeof(GroupHeader);

        if (offset + asksHeader_->numInGroup * sizeof(PriceLevelEntry) > length_) return;
        asks_ = reinterpret_cast<const PriceLevelEntry*>(buffer_ + offset);
    }

    const L2SnapshotRoot* root_ = nullptr;
    const GroupHeader* bidsHeader_ = nullptr;
    const PriceLevelEntry* bids_ = nullptr;
    const GroupHeader* asksHeader_ = nullptr;
    const PriceLevelEntry* asks_ = nullptr;
};

class HeartbeatDecoder : public MessageDecoder {
public:
    HeartbeatDecoder(const uint8_t* buffer, size_t length)
        : MessageDecoder(buffer, length) {
        if (length >= HEARTBEAT_SIZE) {
            root_ = reinterpret_cast<const HeartbeatRoot*>(buffer + sizeof(MessageHeader));
        }
    }

    bool isValid() const {
        return MessageDecoder::isValid() &&
               isHeartbeat() &&
               root_ != nullptr;
    }

    uint64_t timestamp() const { return root_->timestamp; }
    uint64_t sequenceNumber() const { return root_->sequenceNumber; }

private:
    const HeartbeatRoot* root_ = nullptr;
};

// ============================================================================
// Price Conversion Utilities
// ============================================================================

// Convert from 4-decimal fixed point to SBE 7-decimal format
inline int64_t priceToSbe(uint32_t fixedPrice4) {
    return static_cast<int64_t>(fixedPrice4) * 1000;  // 4 decimals -> 7 decimals
}

// Convert from SBE 7-decimal format to 4-decimal fixed point
inline uint32_t priceFromSbe(int64_t sbePrice) {
    return static_cast<uint32_t>(sbePrice / 1000);  // 7 decimals -> 4 decimals
}

// Convert SBE price to double
inline double priceToDouble(int64_t sbePrice) {
    return static_cast<double>(sbePrice) / 10000000.0;  // 7 decimal places
}

} // namespace l2md
