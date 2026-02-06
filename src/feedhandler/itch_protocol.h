#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace feedhandler {
namespace itch {

// ITCH 5.0 Message Types
enum class MessageType : char {
    SystemEvent = 'S',
    StockDirectory = 'R',
    StockTradingAction = 'H',
    RegShoRestriction = 'Y',
    MarketParticipantPosition = 'L',
    MwcbDeclineLevel = 'V',
    MwcbStatus = 'W',
    IpoQuotingPeriod = 'K',
    LuldAuctionCollar = 'J',
    OperationalHalt = 'h',
    AddOrder = 'A',
    AddOrderMpid = 'F',
    OrderExecuted = 'E',
    OrderExecutedWithPrice = 'C',
    OrderCancel = 'X',
    OrderDelete = 'D',
    OrderReplace = 'U',
    Trade = 'P',
    CrossTrade = 'Q',
    BrokenTrade = 'B',
    Noii = 'I',
    RpiiMessage = 'N',
};

// Side indicator
enum class Side : char {
    Buy = 'B',
    Sell = 'S',
};

#pragma pack(push, 1)

// Base message header
struct MessageHeader {
    uint16_t length;        // Big-endian
    MessageType type;
    
    uint16_t getLength() const {
        return __builtin_bswap16(length);
    }
};

// System Event Message (S)
struct SystemEventMessage {
    MessageType type;       // 'S'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint64_t timestamp;     // Nanoseconds since midnight
    char event_code;        // 'O'=Start, 'S'=Start, 'Q'=Market Open, etc.
    
    uint64_t getTimestamp() const {
        return __builtin_bswap64(timestamp) >> 16; // 6-byte timestamp
    }
};

// Stock Directory Message (R)
struct StockDirectoryMessage {
    MessageType type;       // 'R'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint64_t timestamp;
    char stock[8];          // Stock symbol (space-padded)
    char market_category;
    char financial_status;
    uint32_t lot_size;
    char round_lots_only;
    char issue_classification;
    char issue_subtype[2];
    char authenticity;
    char short_sale_threshold;
    char ipo_flag;
    char luld_reference_price_tier;
    char etp_flag;
    uint32_t etp_leverage_factor;
    char inverse_indicator;
    
    std::string getStock() const {
        return std::string(stock, 8);
    }
};

// Add Order Message (A)
struct AddOrderMessage {
    MessageType type;       // 'A'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint64_t timestamp;
    uint64_t order_ref;
    Side side;
    uint32_t shares;
    char stock[8];
    uint32_t price;         // Price in fixed-point (4 decimal places)
    
    uint64_t getOrderRef() const {
        return __builtin_bswap64(order_ref);
    }
    
    uint32_t getShares() const {
        return __builtin_bswap32(shares);
    }
    
    uint32_t getPrice() const {
        return __builtin_bswap32(price);
    }
    
    double getPriceAsDouble() const {
        return getPrice() / 10000.0;
    }
    
    std::string getStock() const {
        return std::string(stock, 8);
    }
};

// Add Order with MPID Message (F)
struct AddOrderMpidMessage {
    MessageType type;       // 'F'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint64_t timestamp;
    uint64_t order_ref;
    Side side;
    uint32_t shares;
    char stock[8];
    uint32_t price;
    char mpid[4];
    
    uint64_t getOrderRef() const {
        return __builtin_bswap64(order_ref);
    }
    
    uint32_t getShares() const {
        return __builtin_bswap32(shares);
    }
    
    uint32_t getPrice() const {
        return __builtin_bswap32(price);
    }
};

// Order Executed Message (E)
struct OrderExecutedMessage {
    MessageType type;       // 'E'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint64_t timestamp;
    uint64_t order_ref;
    uint32_t executed_shares;
    uint64_t match_number;
    
    uint64_t getOrderRef() const {
        return __builtin_bswap64(order_ref);
    }
    
    uint32_t getExecutedShares() const {
        return __builtin_bswap32(executed_shares);
    }
};

// Order Executed with Price Message (C)
struct OrderExecutedWithPriceMessage {
    MessageType type;       // 'C'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint64_t timestamp;
    uint64_t order_ref;
    uint32_t executed_shares;
    uint64_t match_number;
    char printable;
    uint32_t execution_price;
    
    uint32_t getExecutionPrice() const {
        return __builtin_bswap32(execution_price);
    }
};

// Order Cancel Message (X)
struct OrderCancelMessage {
    MessageType type;       // 'X'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint64_t timestamp;
    uint64_t order_ref;
    uint32_t cancelled_shares;
    
    uint64_t getOrderRef() const {
        return __builtin_bswap64(order_ref);
    }
    
    uint32_t getCancelledShares() const {
        return __builtin_bswap32(cancelled_shares);
    }
};

// Order Delete Message (D)
struct OrderDeleteMessage {
    MessageType type;       // 'D'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint64_t timestamp;
    uint64_t order_ref;
    
    uint64_t getOrderRef() const {
        return __builtin_bswap64(order_ref);
    }
};

// Order Replace Message (U)
struct OrderReplaceMessage {
    MessageType type;       // 'U'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint64_t timestamp;
    uint64_t original_order_ref;
    uint64_t new_order_ref;
    uint32_t shares;
    uint32_t price;
    
    uint64_t getOriginalOrderRef() const {
        return __builtin_bswap64(original_order_ref);
    }
    
    uint64_t getNewOrderRef() const {
        return __builtin_bswap64(new_order_ref);
    }
    
    uint32_t getShares() const {
        return __builtin_bswap32(shares);
    }
    
    uint32_t getPrice() const {
        return __builtin_bswap32(price);
    }
};

// Trade Message (P) - Non-cross
struct TradeMessage {
    MessageType type;       // 'P'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint64_t timestamp;
    uint64_t order_ref;
    Side side;
    uint32_t shares;
    char stock[8];
    uint32_t price;
    uint64_t match_number;
    
    uint32_t getShares() const {
        return __builtin_bswap32(shares);
    }
    
    uint32_t getPrice() const {
        return __builtin_bswap32(price);
    }
    
    std::string getStock() const {
        return std::string(stock, 8);
    }
};

// Cross Trade Message (Q)
struct CrossTradeMessage {
    MessageType type;       // 'Q'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint64_t timestamp;
    uint64_t shares;
    char stock[8];
    uint32_t cross_price;
    uint64_t match_number;
    char cross_type;
};

#pragma pack(pop)

// Helper to get message type from buffer
inline MessageType getMessageType(const uint8_t* data) {
    return static_cast<MessageType>(data[2]); // After 2-byte length
}

// Helper to get message size by type
inline size_t getMessageSize(MessageType type) {
    switch (type) {
        case MessageType::SystemEvent: return sizeof(SystemEventMessage) + 2;
        case MessageType::StockDirectory: return sizeof(StockDirectoryMessage) + 2;
        case MessageType::AddOrder: return sizeof(AddOrderMessage) + 2;
        case MessageType::AddOrderMpid: return sizeof(AddOrderMpidMessage) + 2;
        case MessageType::OrderExecuted: return sizeof(OrderExecutedMessage) + 2;
        case MessageType::OrderExecutedWithPrice: return sizeof(OrderExecutedWithPriceMessage) + 2;
        case MessageType::OrderCancel: return sizeof(OrderCancelMessage) + 2;
        case MessageType::OrderDelete: return sizeof(OrderDeleteMessage) + 2;
        case MessageType::OrderReplace: return sizeof(OrderReplaceMessage) + 2;
        case MessageType::Trade: return sizeof(TradeMessage) + 2;
        case MessageType::CrossTrade: return sizeof(CrossTradeMessage) + 2;
        default: return 0;
    }
}

} // namespace itch
} // namespace feedhandler
