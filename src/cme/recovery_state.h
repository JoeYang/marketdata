#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace cme {

// Per-symbol recovery state
enum class RecoveryState {
    Normal,         // Processing incrementals normally
    GapDetected,    // Gap detected, waiting for snapshot
    Recovering,     // Processing snapshot, buffering incrementals
};

// Tracks recovery state for a single security
struct SecurityRecoveryState {
    RecoveryState state = RecoveryState::Normal;
    uint32_t expected_rpt_seq = 1;          // Next expected rpt_seq
    uint32_t last_good_rpt_seq = 0;         // Last successfully processed
    uint32_t snapshot_rpt_seq = 0;          // rpt_seq from snapshot to sync to
    uint64_t gap_detected_time = 0;         // When gap was detected (for timeout)
    uint32_t recovery_attempts = 0;

    // Buffered incrementals during recovery
    struct BufferedUpdate {
        uint32_t rpt_seq;
        std::vector<uint8_t> data;
    };
    std::vector<BufferedUpdate> buffered_updates;
};

// Manages recovery state for all securities
class RecoveryManager {
public:
    RecoveryManager() = default;

    // Called when incremental message arrives
    // Returns true if message should be applied to book
    // If false, message should be discarded or buffered
    bool onIncrementalMessage(uint32_t security_id, uint32_t rpt_seq);

    // Called when snapshot message arrives
    // Returns true if snapshot should be applied (we were waiting for it)
    bool onSnapshotMessage(uint32_t security_id, uint32_t snapshot_rpt_seq, uint32_t last_incr_seq);

    // Called after snapshot is applied, resumes incremental processing
    void completeRecovery(uint32_t security_id, uint32_t rpt_seq);

    // Reset expected sequence (e.g., after channel reset)
    void resetExpectedSeq(uint32_t security_id, uint32_t seq);

    // Initialize security with starting sequence
    void initSecurity(uint32_t security_id, uint32_t initial_seq = 1);

    // Check if any security needs recovery
    bool needsRecovery() const;

    // Get list of securities currently in recovery
    std::vector<uint32_t> getRecoveringSecurities() const;

    // Get state for a security
    RecoveryState getState(uint32_t security_id) const;

    // Get expected rpt_seq for security
    uint32_t getExpectedRptSeq(uint32_t security_id) const;

    // Check and handle recovery timeout (returns securities that timed out)
    std::vector<uint32_t> checkTimeouts(uint64_t current_time, uint64_t timeout_ns = 5000000000ULL);

    // Get statistics
    struct Stats {
        uint64_t gaps_detected = 0;
        uint64_t recoveries_completed = 0;
        uint64_t messages_dropped = 0;
        uint64_t messages_buffered = 0;
    };
    const Stats& getStats() const { return stats_; }

private:
    std::unordered_map<uint32_t, SecurityRecoveryState> states_;
    Stats stats_;
};

} // namespace cme
