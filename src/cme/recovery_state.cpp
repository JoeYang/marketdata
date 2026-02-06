#include "recovery_state.h"

namespace cme {

void RecoveryManager::initSecurity(uint32_t security_id, uint32_t initial_seq) {
    auto& state = states_[security_id];
    state.expected_rpt_seq = initial_seq;
    state.last_good_rpt_seq = initial_seq > 0 ? initial_seq - 1 : 0;
    state.state = RecoveryState::Normal;
}

bool RecoveryManager::onIncrementalMessage(uint32_t security_id, uint32_t rpt_seq) {
    auto it = states_.find(security_id);
    if (it == states_.end()) {
        // First time seeing this security
        initSecurity(security_id, rpt_seq + 1);
        return true;
    }

    auto& state = it->second;

    switch (state.state) {
        case RecoveryState::Normal:
            // Multiple entries can share the same rpt_seq, so accept >= last_good
            if (rpt_seq >= state.last_good_rpt_seq && rpt_seq <= state.expected_rpt_seq) {
                // Valid sequence (same as last or expected next)
                if (rpt_seq > state.last_good_rpt_seq) {
                    state.expected_rpt_seq = rpt_seq + 1;
                    state.last_good_rpt_seq = rpt_seq;
                }
                return true;
            } else if (rpt_seq < state.last_good_rpt_seq) {
                // Old message - discard
                stats_.messages_dropped++;
                return false;
            } else {
                // Gap detected: rpt_seq > expected
                state.state = RecoveryState::GapDetected;
                state.gap_detected_time = 0;  // Will be set by caller with current time
                state.recovery_attempts++;
                stats_.gaps_detected++;
                return false;
            }

        case RecoveryState::GapDetected:
        case RecoveryState::Recovering:
            // Buffer messages during recovery
            // In a full implementation, we'd store these
            // For now, just drop them - snapshot will resync us
            stats_.messages_dropped++;
            return false;
    }

    return false;
}

bool RecoveryManager::onSnapshotMessage(uint32_t security_id, uint32_t snapshot_rpt_seq,
                                        uint32_t last_incr_seq) {
    auto it = states_.find(security_id);
    if (it == states_.end()) {
        // Security not tracked - initialize from snapshot
        initSecurity(security_id, snapshot_rpt_seq + 1);
        return true;
    }

    auto& state = it->second;

    switch (state.state) {
        case RecoveryState::Normal:
            // Not in recovery - we might use snapshot to refresh anyway
            // Only accept if it's reasonably fresh
            if (snapshot_rpt_seq >= state.last_good_rpt_seq) {
                return false;  // Don't need it, we're up to date
            }
            return false;

        case RecoveryState::GapDetected:
            // We were waiting for a snapshot
            // Accept it and transition to Recovering
            state.state = RecoveryState::Recovering;
            state.snapshot_rpt_seq = snapshot_rpt_seq;
            return true;

        case RecoveryState::Recovering:
            // Already got a snapshot, ignore unless this one is fresher
            if (snapshot_rpt_seq > state.snapshot_rpt_seq) {
                state.snapshot_rpt_seq = snapshot_rpt_seq;
                return true;
            }
            return false;
    }

    return false;
}

void RecoveryManager::completeRecovery(uint32_t security_id, uint32_t rpt_seq) {
    auto it = states_.find(security_id);
    if (it == states_.end()) {
        return;
    }

    auto& state = it->second;
    state.state = RecoveryState::Normal;
    state.expected_rpt_seq = rpt_seq + 1;
    state.last_good_rpt_seq = rpt_seq;
    state.buffered_updates.clear();
    stats_.recoveries_completed++;
}

void RecoveryManager::resetExpectedSeq(uint32_t security_id, uint32_t seq) {
    auto& state = states_[security_id];
    state.expected_rpt_seq = seq;
    state.last_good_rpt_seq = seq > 0 ? seq - 1 : 0;
    state.state = RecoveryState::Normal;
    state.buffered_updates.clear();
}

bool RecoveryManager::needsRecovery() const {
    for (const auto& pair : states_) {
        if (pair.second.state != RecoveryState::Normal) {
            return true;
        }
    }
    return false;
}

std::vector<uint32_t> RecoveryManager::getRecoveringSecurities() const {
    std::vector<uint32_t> result;
    for (const auto& pair : states_) {
        if (pair.second.state != RecoveryState::Normal) {
            result.push_back(pair.first);
        }
    }
    return result;
}

RecoveryState RecoveryManager::getState(uint32_t security_id) const {
    auto it = states_.find(security_id);
    if (it == states_.end()) {
        return RecoveryState::Normal;
    }
    return it->second.state;
}

uint32_t RecoveryManager::getExpectedRptSeq(uint32_t security_id) const {
    auto it = states_.find(security_id);
    if (it == states_.end()) {
        return 1;
    }
    return it->second.expected_rpt_seq;
}

std::vector<uint32_t> RecoveryManager::checkTimeouts(uint64_t current_time, uint64_t timeout_ns) {
    std::vector<uint32_t> timed_out;

    for (auto& pair : states_) {
        auto& state = pair.second;
        if (state.state != RecoveryState::Normal) {
            if (state.gap_detected_time == 0) {
                // First time checking - record the time
                state.gap_detected_time = current_time;
            } else if (current_time - state.gap_detected_time > timeout_ns) {
                // Timeout - reset and wait for fresh snapshot
                timed_out.push_back(pair.first);
                state.recovery_attempts++;
                state.gap_detected_time = current_time;  // Reset timeout
            }
        }
    }

    return timed_out;
}

} // namespace cme
