#include "cme_feedhandler.h"
#include "l2_sbe_messages.h"

#include <cstring>
#include <iostream>
#include <poll.h>

namespace cme {

CmeFeedHandler::CmeFeedHandler(const Config& config)
    : config_(config) {
    recv_buffer_.resize(65536);
    send_buffer_.resize(1500);
}

CmeFeedHandler::~CmeFeedHandler() {
    stop();
}

bool CmeFeedHandler::start() {
    // Create receivers
    incremental_receiver_ = std::make_unique<feedhandler::MulticastReceiver>(
        config_.incremental_group, config_.incremental_port, config_.interface);

    snapshot_receiver_ = std::make_unique<feedhandler::MulticastReceiver>(
        config_.snapshot_group, config_.snapshot_port, config_.interface);

    // Create sender
    output_sender_ = std::make_unique<feedhandler::MulticastSender>(
        config_.output_group, config_.output_port, config_.interface);

    if (!incremental_receiver_->start()) {
        std::cerr << "Failed to start incremental receiver" << std::endl;
        return false;
    }

    if (!snapshot_receiver_->start()) {
        std::cerr << "Failed to start snapshot receiver" << std::endl;
        return false;
    }

    if (!output_sender_->start()) {
        std::cerr << "Failed to start output sender" << std::endl;
        return false;
    }

    running_ = true;
    last_conflation_time_ = std::chrono::steady_clock::now();
    last_stats_time_ = std::chrono::steady_clock::now();

    return true;
}

void CmeFeedHandler::stop() {
    running_ = false;
    if (incremental_receiver_) incremental_receiver_->stop();
    if (snapshot_receiver_) snapshot_receiver_->stop();
    if (output_sender_) output_sender_->stop();
}

void CmeFeedHandler::run() {
    std::cout << "CME Feed Handler starting..." << std::endl;
    std::cout << "  Incremental: " << config_.incremental_group << ":" << config_.incremental_port << std::endl;
    std::cout << "  Snapshot: " << config_.snapshot_group << ":" << config_.snapshot_port << std::endl;
    std::cout << "  Output: " << config_.output_group << ":" << config_.output_port << std::endl;

    struct pollfd fds[2];
    fds[0].fd = incremental_receiver_->getFd();
    fds[0].events = POLLIN;
    fds[1].fd = snapshot_receiver_->getFd();
    fds[1].events = POLLIN;

    auto conflation_interval = std::chrono::milliseconds(config_.conflation_interval_ms);

    while (running_) {
        auto now = std::chrono::steady_clock::now();

        // Calculate timeout based on conflation interval
        auto time_since_last = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_conflation_time_);
        int timeout_ms = std::max(1, static_cast<int>(config_.conflation_interval_ms - time_since_last.count()));

        int ret = poll(fds, 2, timeout_ms);

        if (ret > 0) {
            // Process incremental feed (priority)
            if (fds[0].revents & POLLIN) {
                ssize_t len = incremental_receiver_->read(recv_buffer_.data(), recv_buffer_.size());
                if (len > 0) {
                    processIncrementalPacket(recv_buffer_.data(), static_cast<size_t>(len));
                    stats_.messages_received++;
                    stats_.bytes_received += len;
                }
            }

            // Process snapshot feed (only when needed for recovery)
            if (fds[1].revents & POLLIN) {
                ssize_t len = snapshot_receiver_->read(recv_buffer_.data(), recv_buffer_.size());
                if (len > 0) {
                    if (recovery_manager_.needsRecovery()) {
                        processSnapshotPacket(recv_buffer_.data(), static_cast<size_t>(len));
                    }
                    stats_.messages_received++;
                    stats_.bytes_received += len;
                }
            }
        }

        // Conflation: publish snapshots at fixed interval
        now = std::chrono::steady_clock::now();
        if (now - last_conflation_time_ >= conflation_interval) {
            publishConflatedSnapshots();
            last_conflation_time_ = now;
        }

        // Print stats every 10 seconds
        if (now - last_stats_time_ >= std::chrono::seconds(10)) {
            printStats();
            last_stats_time_ = now;
        }

        // Check recovery timeouts
        auto timeout_ns = config_.recovery_timeout_ms * 1000000ULL;
        auto timed_out = recovery_manager_.checkTimeouts(getCurrentTimeNs(), timeout_ns);
        for (auto security_id : timed_out) {
            std::cout << "Recovery timeout for " << getSymbolName(security_id)
                      << " - will retry with next snapshot" << std::endl;
        }
    }

    std::cout << "CME Feed Handler stopped" << std::endl;
}

void CmeFeedHandler::processIncrementalPacket(const uint8_t* data, size_t len) {
    if (len < sizeof(PacketHeader)) {
        stats_.errors++;
        return;
    }

    const auto* pkt = reinterpret_cast<const PacketHeader*>(data);

    // Check packet sequence
    if (!first_packet_) {
        if (pkt->msg_seq_num != last_packet_seq_ + 1) {
            std::cout << "Packet gap detected: expected " << (last_packet_seq_ + 1)
                      << ", got " << pkt->msg_seq_num << std::endl;
            // Note: packet-level gaps affect all symbols, but we handle per-symbol via rpt_seq
        }
    }
    first_packet_ = false;
    last_packet_seq_ = pkt->msg_seq_num;

    // Process messages in packet
    size_t offset = sizeof(PacketHeader);
    while (offset + sizeof(SBEMessageHeader) <= len) {
        const auto* sbe = reinterpret_cast<const SBEMessageHeader*>(data + offset);

        switch (sbe->template_id) {
            case TEMPLATE_SECURITY_DEFINITION:
                if (offset + sizeof(SecurityDefinition) <= len) {
                    handleSecurityDefinition(reinterpret_cast<const SecurityDefinition*>(data + offset));
                    offset += sizeof(SecurityDefinition);
                } else {
                    return;
                }
                break;

            case TEMPLATE_MD_INCREMENTAL_REFRESH: {
                const auto* msg = reinterpret_cast<const MDIncrementalRefreshBook*>(data + offset);
                size_t msg_size = calcIncrementalSize(msg->entries_header.num_in_group);
                if (offset + msg_size <= len) {
                    handleIncrementalRefresh(msg);
                    offset += msg_size;
                } else {
                    return;
                }
                break;
            }

            case TEMPLATE_CHANNEL_RESET:
                if (offset + sizeof(ChannelReset) <= len) {
                    handleChannelReset(reinterpret_cast<const ChannelReset*>(data + offset));
                    offset += sizeof(ChannelReset);
                } else {
                    return;
                }
                break;

            case TEMPLATE_HEARTBEAT:
                if (offset + sizeof(Heartbeat) <= len) {
                    handleHeartbeat(reinterpret_cast<const Heartbeat*>(data + offset));
                    offset += sizeof(Heartbeat);
                } else {
                    return;
                }
                break;

            default:
                // Unknown message, skip based on block_length
                offset += sizeof(SBEMessageHeader) + sbe->block_length;
                break;
        }
    }
}

void CmeFeedHandler::processSnapshotPacket(const uint8_t* data, size_t len) {
    if (len < sizeof(PacketHeader)) {
        stats_.errors++;
        return;
    }

    size_t offset = sizeof(PacketHeader);
    while (offset + sizeof(SBEMessageHeader) <= len) {
        const auto* sbe = reinterpret_cast<const SBEMessageHeader*>(data + offset);

        if (sbe->template_id == TEMPLATE_MD_SNAPSHOT_FULL_REFRESH) {
            const auto* msg = reinterpret_cast<const MDSnapshotFullRefresh*>(data + offset);
            size_t msg_size = calcSnapshotSize(msg->entries_header.num_in_group);
            if (offset + msg_size <= len) {
                handleSnapshotFullRefresh(msg);
                offset += msg_size;
            } else {
                return;
            }
        } else {
            // Skip unknown messages
            offset += sizeof(SBEMessageHeader) + sbe->block_length;
        }
    }
}

void CmeFeedHandler::handleSecurityDefinition(const SecurityDefinition* msg) {
    std::cout << "Received SecurityDefinition: " << msg->symbol
              << " (id=" << msg->security_id << ")" << std::endl;

    // Initialize book and recovery state
    book_manager_.getBook(msg->security_id);
    recovery_manager_.initSecurity(msg->security_id, 1);
}

void CmeFeedHandler::handleIncrementalRefresh(const MDIncrementalRefreshBook* msg) {
    const auto* entries = msg->getEntries();
    uint8_t num_entries = msg->entries_header.num_in_group;

    for (uint8_t i = 0; i < num_entries; ++i) {
        const auto& entry = entries[i];

        // Check recovery state for this security
        if (recovery_manager_.onIncrementalMessage(entry.security_id, entry.rpt_seq)) {
            // Apply update to book
            book_manager_.applyIncremental(entry);

            // Track stats by type
            auto action = static_cast<MDUpdateAction>(entry.md_update_action);
            if (action == MDUpdateAction::New) {
                stats_.add_orders++;
            } else if (action == MDUpdateAction::Delete) {
                stats_.delete_orders++;
            }

            auto type = static_cast<MDEntryType>(entry.md_entry_type);
            if (type == MDEntryType::Trade) {
                stats_.trades++;
            }
        }
    }
}

void CmeFeedHandler::handleSnapshotFullRefresh(const MDSnapshotFullRefresh* msg) {
    // Check if we need this snapshot for recovery
    if (recovery_manager_.onSnapshotMessage(msg->security_id, msg->rpt_seq, msg->last_msg_seq_num_processed)) {
        std::cout << "Applying snapshot for " << getSymbolName(msg->security_id)
                  << " at rpt_seq=" << msg->rpt_seq << std::endl;

        // Apply snapshot to book
        book_manager_.applySnapshot(msg->security_id, msg->getEntries(),
                                    msg->entries_header.num_in_group, msg->rpt_seq);

        // Complete recovery
        recovery_manager_.completeRecovery(msg->security_id, msg->rpt_seq);

        std::cout << "Recovery complete for " << getSymbolName(msg->security_id) << std::endl;
    }
}

void CmeFeedHandler::handleChannelReset(const ChannelReset* msg) {
    std::cout << "Received ChannelReset at time " << msg->transact_time << std::endl;

    // Reset all books and recovery state
    book_manager_.clear();
    for (auto security_id : book_manager_.getAllSecurityIds()) {
        recovery_manager_.resetExpectedSeq(security_id, 1);
    }
}

void CmeFeedHandler::handleHeartbeat(const Heartbeat* msg) {
    // Just track that we're receiving heartbeats
    (void)msg;
}

void CmeFeedHandler::publishConflatedSnapshots() {
    auto dirty = book_manager_.getDirtySecurities();

    for (auto security_id : dirty) {
        // Only publish if not in recovery
        if (recovery_manager_.getState(security_id) == RecoveryState::Normal) {
            auto& book = book_manager_.getBook(security_id);
            auto snap = book.getSnapshot();
            snap.timestamp = getCurrentTimeNs();
            snap.sequence = ++output_seq_;
            publishSnapshot(snap);
        }
    }
}

void CmeFeedHandler::publishSnapshot(const feedhandler::OrderBookSnapshot& snap) {
    // Build SBE-encoded output message
    l2md::PriceLevelEntry bids[l2md::MAX_LEVELS];
    l2md::PriceLevelEntry asks[l2md::MAX_LEVELS];

    uint8_t numBids = std::min(static_cast<uint8_t>(snap.bids.count), l2md::MAX_LEVELS);
    uint8_t numAsks = std::min(static_cast<uint8_t>(snap.asks.count), l2md::MAX_LEVELS);

    // Convert bids to SBE format
    for (uint8_t i = 0; i < numBids; ++i) {
        bids[i].level = i + 1;  // 1-based
        bids[i].price = l2md::priceToSbe(snap.bids.levels[i].price);
        bids[i].quantity = snap.bids.levels[i].quantity;
        bids[i].numOrders = snap.bids.levels[i].order_count;
    }

    // Convert asks to SBE format
    for (uint8_t i = 0; i < numAsks; ++i) {
        asks[i].level = i + 1;  // 1-based
        asks[i].price = l2md::priceToSbe(snap.asks.levels[i].price);
        asks[i].quantity = snap.asks.levels[i].quantity;
        asks[i].numOrders = snap.asks.levels[i].order_count;
    }

    l2md::L2SnapshotEncoder encoder(send_buffer_.data(), send_buffer_.size());
    if (!encoder.encode(
            snap.symbol,
            snap.timestamp,
            snap.sequence,
            l2md::priceToSbe(snap.last_price),
            snap.last_quantity,
            snap.total_volume,
            numBids,
            numAsks,
            bids, numBids,
            asks, numAsks)) {
        stats_.errors++;
        return;
    }

    output_sender_->send(send_buffer_.data(), encoder.encodedLength());

    stats_.messages_sent++;
    stats_.bytes_sent += encoder.encodedLength();
}

uint64_t CmeFeedHandler::getCurrentTimeNs() {
    auto now = std::chrono::system_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
    return static_cast<uint64_t>(ns);
}

void CmeFeedHandler::printStats() {
    std::cout << "\n=== Feed Handler Stats ===" << std::endl;
    std::cout << "Messages received: " << stats_.messages_received << std::endl;
    std::cout << "Messages sent: " << stats_.messages_sent << std::endl;
    std::cout << "Bytes received: " << stats_.bytes_received << std::endl;
    std::cout << "Bytes sent: " << stats_.bytes_sent << std::endl;
    std::cout << "Add orders: " << stats_.add_orders << std::endl;
    std::cout << "Delete orders: " << stats_.delete_orders << std::endl;
    std::cout << "Trades: " << stats_.trades << std::endl;
    std::cout << "Errors: " << stats_.errors << std::endl;

    auto& rec_stats = recovery_manager_.getStats();
    std::cout << "Gaps detected: " << rec_stats.gaps_detected << std::endl;
    std::cout << "Recoveries completed: " << rec_stats.recoveries_completed << std::endl;

    // Print recovering securities
    auto recovering = recovery_manager_.getRecoveringSecurities();
    if (!recovering.empty()) {
        std::cout << "Securities in recovery:";
        for (auto id : recovering) {
            std::cout << " " << getSymbolName(id);
        }
        std::cout << std::endl;
    }

    std::cout << "=========================\n" << std::endl;
}

} // namespace cme
