#pragma once

#include "cme_order_book.h"
#include "cme_protocol.h"
#include "recovery_state.h"
#include "src/feedhandler/market_data.h"
#include "src/feedhandler/multicast.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace cme {

class CmeFeedHandler {
public:
    struct Config {
        // Input feeds
        std::string incremental_group = CME_INCREMENTAL_GROUP;
        uint16_t incremental_port = CME_INCREMENTAL_PORT;
        std::string snapshot_group = CME_SNAPSHOT_GROUP;
        uint16_t snapshot_port = CME_SNAPSHOT_PORT;

        // Output feed
        std::string output_group = CME_OUTPUT_GROUP;
        uint16_t output_port = CME_OUTPUT_PORT;

        std::string interface = "0.0.0.0";

        // Conflation settings
        uint32_t conflation_interval_ms = 100;  // 10 Hz output rate

        // Recovery settings
        uint64_t recovery_timeout_ms = 5000;  // 5 seconds
    };

    explicit CmeFeedHandler(const Config& config);
    ~CmeFeedHandler();

    bool start();
    void stop();
    void run();

    // Stats
    const feedhandler::FeedStats& getStats() const { return stats_; }

private:
    // Message processing
    void processIncrementalPacket(const uint8_t* data, size_t len);
    void processSnapshotPacket(const uint8_t* data, size_t len);

    void handleSecurityDefinition(const SecurityDefinition* msg);
    void handleIncrementalRefresh(const MDIncrementalRefreshBook* msg);
    void handleSnapshotFullRefresh(const MDSnapshotFullRefresh* msg);
    void handleChannelReset(const ChannelReset* msg);
    void handleHeartbeat(const Heartbeat* msg);

    // Output
    void publishConflatedSnapshots();
    void publishSnapshot(const feedhandler::OrderBookSnapshot& snap);

    // Utility
    uint64_t getCurrentTimeNs();
    void printStats();

    Config config_;

    // Receivers and sender
    std::unique_ptr<feedhandler::MulticastReceiver> incremental_receiver_;
    std::unique_ptr<feedhandler::MulticastReceiver> snapshot_receiver_;
    std::unique_ptr<feedhandler::MulticastSender> output_sender_;

    // State
    CmeOrderBookManager book_manager_;
    RecoveryManager recovery_manager_;

    // Packet sequence tracking
    uint32_t last_packet_seq_ = 0;
    bool first_packet_ = true;

    // Output sequence
    uint64_t output_seq_ = 0;

    // Timing
    std::chrono::steady_clock::time_point last_conflation_time_;
    std::chrono::steady_clock::time_point last_stats_time_;

    // Stats
    feedhandler::FeedStats stats_;

    // Running state
    std::atomic<bool> running_{false};

    // Buffers
    std::vector<uint8_t> recv_buffer_;
    std::vector<uint8_t> send_buffer_;
};

} // namespace cme
