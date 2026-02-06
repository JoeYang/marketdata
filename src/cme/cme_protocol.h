#pragma once

#include <cstdint>
#include <cstring>

namespace cme {

// CME MDP 3.0 Constants
constexpr uint16_t CME_INCREMENTAL_PORT = 40001;
constexpr uint16_t CME_SNAPSHOT_PORT = 40002;
constexpr uint16_t CME_OUTPUT_PORT = 40003;

constexpr const char* CME_INCREMENTAL_GROUP = "239.2.1.1";
constexpr const char* CME_SNAPSHOT_GROUP = "239.2.1.2";
constexpr const char* CME_OUTPUT_GROUP = "239.2.1.3";

// SBE Template IDs
constexpr uint16_t TEMPLATE_CHANNEL_RESET = 4;
constexpr uint16_t TEMPLATE_HEARTBEAT = 12;
constexpr uint16_t TEMPLATE_SECURITY_DEFINITION = 27;
constexpr uint16_t TEMPLATE_MD_INCREMENTAL_REFRESH = 32;
constexpr uint16_t TEMPLATE_MD_SNAPSHOT_FULL_REFRESH = 38;

// Entry types
enum class MDEntryType : uint8_t {
    Bid = 0,
    Offer = 1,
    Trade = 2,
    ImpliedBid = 'E',
    ImpliedOffer = 'F',
};

// Update actions
enum class MDUpdateAction : uint8_t {
    New = 0,
    Change = 1,
    Delete = 2,
    DeleteThru = 3,
    DeleteFrom = 4,
    Overlay = 5,
};

// Predefined security IDs for CME futures
constexpr uint32_t SECURITY_ID_ESH26 = 1001;  // E-mini S&P 500 Mar 2026
constexpr uint32_t SECURITY_ID_NQM26 = 1002;  // E-mini NASDAQ Jun 2026
constexpr uint32_t SECURITY_ID_CLK26 = 1003;  // Crude Oil May 2026
constexpr uint32_t SECURITY_ID_GCZ26 = 1004;  // Gold Dec 2026

inline const char* getSymbolName(uint32_t security_id) {
    switch (security_id) {
        case SECURITY_ID_ESH26: return "ESH26";
        case SECURITY_ID_NQM26: return "NQM26";
        case SECURITY_ID_CLK26: return "CLK26";
        case SECURITY_ID_GCZ26: return "GCZ26";
        default: return "UNKNOWN";
    }
}

inline uint32_t getSecurityIdFromSymbol(const char* symbol) {
    if (std::strncmp(symbol, "ESH26", 5) == 0) return SECURITY_ID_ESH26;
    if (std::strncmp(symbol, "NQM26", 5) == 0) return SECURITY_ID_NQM26;
    if (std::strncmp(symbol, "CLK26", 5) == 0) return SECURITY_ID_CLK26;
    if (std::strncmp(symbol, "GCZ26", 5) == 0) return SECURITY_ID_GCZ26;
    return 0;
}

#pragma pack(push, 1)

// Packet header (appears once per UDP packet)
struct PacketHeader {
    uint32_t msg_seq_num;       // Packet sequence number
    uint64_t sending_time;      // Nanoseconds since epoch
};

// SBE Message header (appears before each message in packet)
struct SBEMessageHeader {
    uint16_t block_length;      // Root block length
    uint16_t template_id;       // Message template ID
    uint16_t schema_id;         // Schema ID
    uint16_t version;           // Schema version
};

// Repeating group header
struct GroupHeader {
    uint16_t block_length;      // Entry block length
    uint8_t num_in_group;       // Number of entries
};

// Security Definition (template 27)
struct SecurityDefinition {
    SBEMessageHeader header;
    uint32_t security_id;
    char symbol[20];
    int64_t min_price_increment;    // Price tick (mantissa, exponent -7)
    uint32_t display_factor;
    uint8_t security_trading_status;

    void init() {
        header.block_length = sizeof(SecurityDefinition) - sizeof(SBEMessageHeader);
        header.template_id = TEMPLATE_SECURITY_DEFINITION;
        header.schema_id = 1;
        header.version = 9;
    }
};

// MD Incremental Refresh Entry
struct MDIncrementalRefreshEntry {
    int64_t md_entry_px;        // Price (mantissa, exponent -7)
    int32_t md_entry_size;      // Quantity
    uint32_t security_id;
    uint32_t rpt_seq;           // Per-symbol sequence number
    uint8_t md_entry_type;      // MDEntryType
    uint8_t md_update_action;   // MDUpdateAction
    uint8_t md_price_level;     // Price level (1-based)
    uint8_t number_of_orders;   // Number of orders at level
};

// MD Incremental Refresh Book (template 32)
struct MDIncrementalRefreshBook {
    SBEMessageHeader header;
    uint64_t transact_time;     // Transaction time
    GroupHeader entries_header;
    // Followed by MDIncrementalRefreshEntry[num_in_group]

    void init(uint8_t num_entries) {
        header.block_length = sizeof(uint64_t);  // Only transact_time in root
        header.template_id = TEMPLATE_MD_INCREMENTAL_REFRESH;
        header.schema_id = 1;
        header.version = 9;
        entries_header.block_length = sizeof(MDIncrementalRefreshEntry);
        entries_header.num_in_group = num_entries;
    }

    MDIncrementalRefreshEntry* getEntries() {
        return reinterpret_cast<MDIncrementalRefreshEntry*>(
            reinterpret_cast<uint8_t*>(this) + sizeof(MDIncrementalRefreshBook));
    }

    const MDIncrementalRefreshEntry* getEntries() const {
        return reinterpret_cast<const MDIncrementalRefreshEntry*>(
            reinterpret_cast<const uint8_t*>(this) + sizeof(MDIncrementalRefreshBook));
    }
};

// MD Snapshot Full Refresh Entry
struct MDSnapshotEntry {
    int64_t md_entry_px;        // Price
    int32_t md_entry_size;      // Quantity
    uint8_t md_entry_type;      // MDEntryType (Bid or Offer)
    uint8_t md_price_level;     // Level (1-based)
    uint8_t number_of_orders;
    uint8_t padding;
};

// MD Snapshot Full Refresh (template 38)
struct MDSnapshotFullRefresh {
    SBEMessageHeader header;
    uint32_t last_msg_seq_num_processed;  // Last incremental seq processed
    uint32_t security_id;
    uint32_t rpt_seq;           // Per-symbol sequence to sync to
    uint64_t transact_time;
    GroupHeader entries_header;
    // Followed by MDSnapshotEntry[num_in_group]

    void init(uint8_t num_entries) {
        header.block_length = sizeof(MDSnapshotFullRefresh) - sizeof(SBEMessageHeader) - sizeof(GroupHeader);
        header.template_id = TEMPLATE_MD_SNAPSHOT_FULL_REFRESH;
        header.schema_id = 1;
        header.version = 9;
        entries_header.block_length = sizeof(MDSnapshotEntry);
        entries_header.num_in_group = num_entries;
    }

    MDSnapshotEntry* getEntries() {
        return reinterpret_cast<MDSnapshotEntry*>(
            reinterpret_cast<uint8_t*>(this) + sizeof(MDSnapshotFullRefresh));
    }

    const MDSnapshotEntry* getEntries() const {
        return reinterpret_cast<const MDSnapshotEntry*>(
            reinterpret_cast<const uint8_t*>(this) + sizeof(MDSnapshotFullRefresh));
    }
};

// Channel Reset (template 4)
struct ChannelReset {
    SBEMessageHeader header;
    uint64_t transact_time;

    void init() {
        header.block_length = sizeof(uint64_t);
        header.template_id = TEMPLATE_CHANNEL_RESET;
        header.schema_id = 1;
        header.version = 9;
    }
};

// Heartbeat (template 12)
struct Heartbeat {
    SBEMessageHeader header;
    uint64_t last_msg_seq_num;

    void init() {
        header.block_length = sizeof(uint64_t);
        header.template_id = TEMPLATE_HEARTBEAT;
        header.schema_id = 1;
        header.version = 9;
    }
};

#pragma pack(pop)

// Helper to convert CME price (mantissa with -7 exponent) to fixed-point (4 decimals)
inline uint32_t cmeToFixedPrice(int64_t cme_price) {
    // CME uses exponent -7 (1e-7), we use 4 decimals (1e-4)
    // So divide by 1000
    return static_cast<uint32_t>(cme_price / 1000);
}

// Helper to convert fixed-point (4 decimals) to CME price
inline int64_t fixedToCmePrice(uint32_t fixed_price) {
    return static_cast<int64_t>(fixed_price) * 1000;
}

// Calculate message size for incremental refresh
inline size_t calcIncrementalSize(uint8_t num_entries) {
    return sizeof(MDIncrementalRefreshBook) + num_entries * sizeof(MDIncrementalRefreshEntry);
}

// Calculate message size for snapshot
inline size_t calcSnapshotSize(uint8_t num_entries) {
    return sizeof(MDSnapshotFullRefresh) + num_entries * sizeof(MDSnapshotEntry);
}

} // namespace cme
