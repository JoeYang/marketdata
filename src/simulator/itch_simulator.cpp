#include "itch_simulator.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

namespace simulator {

ItchSimulator::ItchSimulator(const SimulatorConfig& config)
    : config_(config)
    , rng_(std::random_device{}())
    , symbol_dist_(0, config.symbols.size() - 1)
    , price_dist_(config.min_price, config.max_price)
    , qty_dist_(config.min_qty, config.max_qty)
    , side_dist_(0, 1)
    , action_dist_(0, 99) {
    
    sender_ = std::make_unique<feedhandler::MulticastSender>(
        config_.multicast_group, config_.port,
        config_.interface, config_.ttl);
}

ItchSimulator::~ItchSimulator() {
    stop();
}

bool ItchSimulator::start() {
    if (running_) return true;
    
    if (!sender_->start()) {
        std::cerr << "Failed to start sender" << std::endl;
        return false;
    }
    
    running_ = true;
    std::cout << "ITCH Simulator started" << std::endl;
    std::cout << "  Target: " << config_.multicast_group << ":" << config_.port << std::endl;
    std::cout << "  Rate: " << config_.messages_per_second << " msg/sec" << std::endl;
    std::cout << "  Symbols: ";
    for (const auto& sym : config_.symbols) {
        std::cout << sym << " ";
    }
    std::cout << std::endl;
    
    return true;
}

void ItchSimulator::stop() {
    if (!running_) return;
    
    running_ = false;
    sender_->stop();
    
    std::cout << "ITCH Simulator stopped" << std::endl;
    std::cout << "  Total messages sent: " << messages_sent_ << std::endl;
}

void ItchSimulator::run() {
    if (!running_) {
        if (!start()) return;
    }
    
    const auto interval = std::chrono::microseconds(1000000 / config_.messages_per_second);
    auto next_send = std::chrono::steady_clock::now();
    
    while (running_) {
        auto now = std::chrono::steady_clock::now();
        
        if (now >= next_send) {
            generateMessage();
            next_send += interval;
            
            // Catch up if we're behind
            if (next_send < now) {
                next_send = now + interval;
            }
        } else {
            std::this_thread::sleep_until(next_send);
        }
    }
}

void ItchSimulator::generateMessage() {
    int action = action_dist_(rng_);
    
    // 60% add orders, 20% executes, 15% deletes, 5% trades
    if (action < 60) {
        sendAddOrder();
    } else if (action < 80 && !active_orders_.empty()) {
        sendExecuteOrder();
    } else if (action < 95 && !active_orders_.empty()) {
        sendDeleteOrder();
    } else {
        sendTrade();
    }
}

void ItchSimulator::sendAddOrder() {
    feedhandler::itch::AddOrderMessage msg{};
    
    // Select random symbol and side
    const std::string& symbol = config_.symbols[symbol_dist_(rng_)];
    auto side = side_dist_(rng_) == 0 ? feedhandler::itch::Side::Buy 
                                       : feedhandler::itch::Side::Sell;
    
    // Generate price and quantity
    uint32_t price = roundPrice(price_dist_(rng_));
    uint32_t qty = roundQty(qty_dist_(rng_));
    
    // Fill message (convert to big-endian where needed)
    msg.type = feedhandler::itch::MessageType::AddOrder;
    msg.stock_locate = 0;
    msg.tracking_number = 0;
    msg.timestamp = 0;
    msg.order_ref = __builtin_bswap64(next_order_ref_);
    msg.side = side;
    msg.shares = __builtin_bswap32(qty);
    std::memcpy(msg.stock, symbol.c_str(), 8);
    msg.price = __builtin_bswap32(price);
    
    // Track active order
    ActiveOrder active{next_order_ref_, symbol, price, qty, side};
    active_orders_.push_back(active);
    
    // Limit active orders to prevent unbounded growth
    if (active_orders_.size() > 10000) {
        active_orders_.erase(active_orders_.begin());
    }
    
    next_order_ref_++;
    
    sendMessage(reinterpret_cast<uint8_t*>(&msg), sizeof(msg));
}

void ItchSimulator::sendDeleteOrder() {
    if (active_orders_.empty()) return;
    
    // Pick random active order
    std::uniform_int_distribution<size_t> order_dist(0, active_orders_.size() - 1);
    size_t idx = order_dist(rng_);
    
    const auto& order = active_orders_[idx];
    
    feedhandler::itch::OrderDeleteMessage msg{};
    msg.type = feedhandler::itch::MessageType::OrderDelete;
    msg.stock_locate = 0;
    msg.tracking_number = 0;
    msg.timestamp = 0;
    msg.order_ref = __builtin_bswap64(order.order_ref);
    
    // Remove from active orders
    active_orders_.erase(active_orders_.begin() + static_cast<long>(idx));
    
    sendMessage(reinterpret_cast<uint8_t*>(&msg), sizeof(msg));
}

void ItchSimulator::sendExecuteOrder() {
    if (active_orders_.empty()) return;
    
    // Pick random active order
    std::uniform_int_distribution<size_t> order_dist(0, active_orders_.size() - 1);
    size_t idx = order_dist(rng_);
    
    auto& order = active_orders_[idx];
    
    // Execute partial or full
    std::uniform_int_distribution<uint32_t> exec_dist(1, order.remaining_qty);
    uint32_t exec_qty = roundQty(exec_dist(rng_));
    exec_qty = std::min(exec_qty, order.remaining_qty);
    
    feedhandler::itch::OrderExecutedMessage msg{};
    msg.type = feedhandler::itch::MessageType::OrderExecuted;
    msg.stock_locate = 0;
    msg.tracking_number = 0;
    msg.timestamp = 0;
    msg.order_ref = __builtin_bswap64(order.order_ref);
    msg.executed_shares = __builtin_bswap32(exec_qty);
    msg.match_number = __builtin_bswap64(messages_sent_);
    
    order.remaining_qty -= exec_qty;
    
    // Remove if fully executed
    if (order.remaining_qty == 0) {
        active_orders_.erase(active_orders_.begin() + static_cast<long>(idx));
    }
    
    sendMessage(reinterpret_cast<uint8_t*>(&msg), sizeof(msg));
}

void ItchSimulator::sendTrade() {
    feedhandler::itch::TradeMessage msg{};
    
    // Select random symbol and side
    const std::string& symbol = config_.symbols[symbol_dist_(rng_)];
    auto side = side_dist_(rng_) == 0 ? feedhandler::itch::Side::Buy 
                                       : feedhandler::itch::Side::Sell;
    
    // Generate price and quantity
    uint32_t price = roundPrice(price_dist_(rng_));
    uint32_t qty = roundQty(qty_dist_(rng_));
    
    msg.type = feedhandler::itch::MessageType::Trade;
    msg.stock_locate = 0;
    msg.tracking_number = 0;
    msg.timestamp = 0;
    msg.order_ref = 0;  // Not associated with specific order
    msg.side = side;
    msg.shares = __builtin_bswap32(qty);
    std::memcpy(msg.stock, symbol.c_str(), 8);
    msg.price = __builtin_bswap32(price);
    msg.match_number = __builtin_bswap64(messages_sent_);
    
    sendMessage(reinterpret_cast<uint8_t*>(&msg), sizeof(msg));
}

void ItchSimulator::sendMessage(const uint8_t* data, size_t length) {
    // Wrap message with length prefix (ITCH format)
    std::vector<uint8_t> packet(2 + length);
    
    uint16_t len = static_cast<uint16_t>(length);
    packet[0] = static_cast<uint8_t>(len >> 8);    // Big-endian
    packet[1] = static_cast<uint8_t>(len & 0xFF);
    std::memcpy(packet.data() + 2, data, length);
    
    if (sender_->send(packet)) {
        messages_sent_++;
    }
}

uint32_t ItchSimulator::roundPrice(uint32_t price) const {
    return (price / config_.price_tick) * config_.price_tick;
}

uint32_t ItchSimulator::roundQty(uint32_t qty) const {
    return (qty / config_.qty_round) * config_.qty_round;
}

} // namespace simulator
