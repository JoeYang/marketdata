#pragma once

#include "../feedhandler/itch_protocol.h"
#include "../feedhandler/multicast.h"

#include <atomic>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace simulator {

struct SimulatorConfig {
    std::string multicast_group = "239.1.1.1";
    uint16_t port = 30001;
    std::string interface = "0.0.0.0";
    int ttl = 1;
    
    int messages_per_second = 1000;
    std::vector<std::string> symbols = {"AAPL    ", "GOOGL   ", "MSFT    ", "AMZN    ", "META    "};
    
    // Price ranges (in cents, will be converted to fixed-point)
    uint32_t min_price = 10000;   // $100.00
    uint32_t max_price = 50000;   // $500.00
    uint32_t price_tick = 100;    // $1.00 tick size
    
    // Order sizes
    uint32_t min_qty = 100;
    uint32_t max_qty = 10000;
    uint32_t qty_round = 100;
};

class ItchSimulator {
public:
    explicit ItchSimulator(const SimulatorConfig& config);
    ~ItchSimulator();
    
    bool start();
    void stop();
    void run();  // Blocking run loop
    
    bool isRunning() const { return running_; }
    uint64_t getMessagesSent() const { return messages_sent_; }
    
private:
    SimulatorConfig config_;
    std::atomic<bool> running_{false};
    
    std::unique_ptr<feedhandler::MulticastSender> sender_;
    
    // Random generators
    std::mt19937 rng_;
    std::uniform_int_distribution<size_t> symbol_dist_;
    std::uniform_int_distribution<uint32_t> price_dist_;
    std::uniform_int_distribution<uint32_t> qty_dist_;
    std::uniform_int_distribution<int> side_dist_;
    std::uniform_int_distribution<int> action_dist_;
    
    // State
    uint64_t next_order_ref_ = 1;
    uint64_t messages_sent_ = 0;
    
    // Active orders for cancel/execute simulation
    struct ActiveOrder {
        uint64_t order_ref;
        std::string symbol;
        uint32_t price;
        uint32_t remaining_qty;
        feedhandler::itch::Side side;
    };
    std::vector<ActiveOrder> active_orders_;
    
    // Message generation
    void generateMessage();
    void sendAddOrder();
    void sendDeleteOrder();
    void sendExecuteOrder();
    void sendTrade();
    
    // Helpers
    void sendMessage(const uint8_t* data, size_t length);
    uint32_t roundPrice(uint32_t price) const;
    uint32_t roundQty(uint32_t qty) const;
};

} // namespace simulator
