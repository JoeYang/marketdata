#pragma once

#include "src/cme/cme_protocol.h"
#include "src/feedhandler/multicast.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace cme_simulator {

// Simulated book state for one security
struct SimulatedBook {
    uint32_t security_id;
    std::string symbol;

    // Price levels (simplified - just track best 5 levels)
    struct Level {
        int64_t price = 0;
        int32_t quantity = 0;
        uint8_t order_count = 0;
    };

    std::array<Level, 5> bids;
    std::array<Level, 5> asks;

    int64_t mid_price;
    int64_t tick_size;

    uint32_t rpt_seq = 0;  // Per-symbol sequence

    void initialize(int64_t initial_mid, int64_t tick);
    void randomUpdate(std::mt19937& rng);
};

class CmeSimulator {
public:
    struct Config {
        std::string incremental_group = cme::CME_INCREMENTAL_GROUP;
        uint16_t incremental_port = cme::CME_INCREMENTAL_PORT;
        std::string snapshot_group = cme::CME_SNAPSHOT_GROUP;
        uint16_t snapshot_port = cme::CME_SNAPSHOT_PORT;
        std::string interface = "0.0.0.0";

        uint32_t updates_per_second = 100;  // Rate of incremental updates
        uint32_t snapshot_interval_ms = 1000;  // Snapshot publishing interval

        bool simulate_gaps = false;         // Simulate packet gaps for testing
        uint32_t gap_frequency = 100;       // Every N packets, simulate a gap
    };

    explicit CmeSimulator(const Config& config);
    ~CmeSimulator();

    bool start();
    void stop();
    void run();

    uint32_t getIncrPacketSeq() const { return incr_packet_seq_; }
    uint32_t getSnapPacketSeq() const { return snap_packet_seq_; }

private:
    void initializeBooks();
    void sendSecurityDefinitions();
    void sendIncrementalUpdate();
    void sendSnapshots();

    // Build and send packet
    void sendIncrementalPacket(const std::vector<cme::MDIncrementalRefreshEntry>& entries);
    void sendSnapshotPacket(const SimulatedBook& book);

    uint64_t getCurrentTimeNs();

    Config config_;

    std::unique_ptr<feedhandler::MulticastSender> incremental_sender_;
    std::unique_ptr<feedhandler::MulticastSender> snapshot_sender_;

    std::array<SimulatedBook, 4> books_;

    uint32_t incr_packet_seq_ = 0;   // Incremental feed sequence
    uint32_t snap_packet_seq_ = 0;   // Snapshot feed sequence
    std::atomic<bool> running_{false};

    std::mt19937 rng_;
    std::vector<uint8_t> send_buffer_;
};

} // namespace cme_simulator
