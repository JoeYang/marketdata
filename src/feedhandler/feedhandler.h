#pragma once

#include "itch_protocol.h"
#include "market_data.h"
#include "multicast.h"
#include "order_book.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

namespace feedhandler {

enum class ProcessingMode {
    TickByTick,     // Forward processed data immediately
    Conflated,      // Batch updates and send at intervals
};

struct FeedHandlerConfig {
    // Input
    std::string input_group = "239.1.1.1";
    uint16_t input_port = 30001;
    std::string input_interface = "0.0.0.0";
    size_t input_buffer_size = 65536;
    
    // Output
    std::string output_group = "239.1.1.2";
    uint16_t output_port = 30002;
    std::string output_interface = "0.0.0.0";
    int output_ttl = 1;
    
    // Processing
    ProcessingMode mode = ProcessingMode::TickByTick;
    int conflation_interval_ms = 100;
    size_t book_depth = 10;
    
    // Stats
    int stats_interval_sec = 10;
};

class FeedHandler {
public:
    explicit FeedHandler(const FeedHandlerConfig& config);
    ~FeedHandler();
    
    // Non-copyable
    FeedHandler(const FeedHandler&) = delete;
    FeedHandler& operator=(const FeedHandler&) = delete;
    
    bool start();
    void stop();
    void run();  // Blocking run loop
    
    const FeedStats& getStats() const { return stats_; }
    bool isRunning() const { return running_; }
    
private:
    FeedHandlerConfig config_;
    std::atomic<bool> running_{false};
    
    std::unique_ptr<MulticastReceiver> receiver_;
    std::unique_ptr<MulticastSender> sender_;
    std::unique_ptr<OrderBookManager> book_manager_;
    
    FeedStats stats_;
    uint64_t sequence_ = 0;
    
    // Conflation timer
    std::chrono::steady_clock::time_point last_conflation_time_;
    
    // Message processing
    void processMessage(const uint8_t* data, size_t length);
    void processItchMessage(const uint8_t* data, size_t length);
    
    // Output
    void sendSnapshot(const OrderBookSnapshot& snap);
    void sendQuote(const QuoteUpdate& quote);
    void sendTrade(const TradeTick& trade);
    
    // Conflation
    void checkConflation();
    void sendConflatedSnapshots();
    
    // Stats
    void printStats();
    std::chrono::steady_clock::time_point last_stats_time_;
};

} // namespace feedhandler
