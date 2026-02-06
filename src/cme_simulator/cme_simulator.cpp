#include "cme_simulator.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

namespace cme_simulator {

void SimulatedBook::initialize(int64_t initial_mid, int64_t tick) {
    mid_price = initial_mid;
    tick_size = tick;

    // Initialize bid levels (below mid)
    for (int i = 0; i < 5; ++i) {
        bids[i].price = mid_price - (i + 1) * tick_size;
        bids[i].quantity = 50 + (4 - i) * 25;  // More at best bid
        bids[i].order_count = 5 + (4 - i) * 2;
    }

    // Initialize ask levels (above mid)
    for (int i = 0; i < 5; ++i) {
        asks[i].price = mid_price + (i + 1) * tick_size;
        asks[i].quantity = 50 + (4 - i) * 25;
        asks[i].order_count = 5 + (4 - i) * 2;
    }
}

void SimulatedBook::randomUpdate(std::mt19937& rng) {
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int> level_dist(0, 4);
    std::uniform_int_distribution<int> qty_change(-20, 30);
    std::uniform_int_distribution<int> price_move(-1, 1);

    bool is_bid = side_dist(rng) == 0;
    int level = level_dist(rng);

    auto& levels = is_bid ? bids : asks;

    // Update quantity
    int32_t new_qty = levels[level].quantity + qty_change(rng);
    levels[level].quantity = std::max(10, new_qty);

    // Occasionally move price
    if (price_move(rng) != 0 && level == 0) {
        int64_t move = price_move(rng) * tick_size;
        mid_price += move;

        // Recalculate all prices
        for (int i = 0; i < 5; ++i) {
            bids[i].price = mid_price - (i + 1) * tick_size;
            asks[i].price = mid_price + (i + 1) * tick_size;
        }
    }

    rpt_seq++;
}

CmeSimulator::CmeSimulator(const Config& config)
    : config_(config)
    , rng_(std::random_device{}()) {
    send_buffer_.resize(1500);  // MTU size
}

CmeSimulator::~CmeSimulator() {
    stop();
}

bool CmeSimulator::start() {
    incremental_sender_ = std::make_unique<feedhandler::MulticastSender>(
        config_.incremental_group, config_.incremental_port, config_.interface);

    snapshot_sender_ = std::make_unique<feedhandler::MulticastSender>(
        config_.snapshot_group, config_.snapshot_port, config_.interface);

    if (!incremental_sender_->start()) {
        std::cerr << "Failed to start incremental sender" << std::endl;
        return false;
    }

    if (!snapshot_sender_->start()) {
        std::cerr << "Failed to start snapshot sender" << std::endl;
        return false;
    }

    initializeBooks();
    running_ = true;
    return true;
}

void CmeSimulator::stop() {
    running_ = false;
    if (incremental_sender_) {
        incremental_sender_->stop();
    }
    if (snapshot_sender_) {
        snapshot_sender_->stop();
    }
}

void CmeSimulator::initializeBooks() {
    // ESH26 - E-mini S&P 500
    books_[0].security_id = cme::SECURITY_ID_ESH26;
    books_[0].symbol = "ESH26";
    books_[0].initialize(45000000000LL, 2500000LL);  // $4500.00, $0.25 tick

    // NQM26 - E-mini NASDAQ
    books_[1].security_id = cme::SECURITY_ID_NQM26;
    books_[1].symbol = "NQM26";
    books_[1].initialize(180000000000LL, 2500000LL);  // $18000.00, $0.25 tick

    // CLK26 - Crude Oil
    books_[2].security_id = cme::SECURITY_ID_CLK26;
    books_[2].symbol = "CLK26";
    books_[2].initialize(750000000LL, 10000000LL);  // $75.00, $0.01 tick

    // GCZ26 - Gold
    books_[3].security_id = cme::SECURITY_ID_GCZ26;
    books_[3].symbol = "GCZ26";
    books_[3].initialize(20000000000LL, 1000000LL);  // $2000.00, $0.10 tick
}

void CmeSimulator::run() {
    std::cout << "CME Simulator starting..." << std::endl;
    std::cout << "  Incremental: " << config_.incremental_group << ":" << config_.incremental_port << std::endl;
    std::cout << "  Snapshot: " << config_.snapshot_group << ":" << config_.snapshot_port << std::endl;

    sendSecurityDefinitions();

    auto update_interval = std::chrono::microseconds(1000000 / config_.updates_per_second);
    auto snapshot_interval = std::chrono::milliseconds(config_.snapshot_interval_ms);

    auto last_snapshot = std::chrono::steady_clock::now();
    auto last_stats = std::chrono::steady_clock::now();

    uint64_t total_updates = 0;

    while (running_) {
        auto now = std::chrono::steady_clock::now();

        // Send incremental update
        sendIncrementalUpdate();
        total_updates++;

        // Send snapshots periodically
        if (now - last_snapshot >= snapshot_interval) {
            sendSnapshots();
            last_snapshot = now;
        }

        // Print stats every 10 seconds
        if (now - last_stats >= std::chrono::seconds(10)) {
            std::cout << "Simulator: sent " << total_updates << " updates, incr_seq=" << incr_packet_seq_
                      << ", snap_seq=" << snap_packet_seq_ << std::endl;
            last_stats = now;
        }

        std::this_thread::sleep_for(update_interval);
    }

    std::cout << "CME Simulator stopped" << std::endl;
}

void CmeSimulator::sendSecurityDefinitions() {
    for (auto& book : books_) {
        std::memset(send_buffer_.data(), 0, send_buffer_.size());

        auto* pkt = reinterpret_cast<cme::PacketHeader*>(send_buffer_.data());
        pkt->msg_seq_num = ++incr_packet_seq_;
        pkt->sending_time = getCurrentTimeNs();

        auto* msg = reinterpret_cast<cme::SecurityDefinition*>(send_buffer_.data() + sizeof(cme::PacketHeader));
        msg->init();
        msg->security_id = book.security_id;
        std::strncpy(msg->symbol, book.symbol.c_str(), sizeof(msg->symbol) - 1);
        msg->min_price_increment = book.tick_size;
        msg->display_factor = 1;
        msg->security_trading_status = 17;  // Trading

        size_t packet_size = sizeof(cme::PacketHeader) + sizeof(cme::SecurityDefinition);
        incremental_sender_->send(send_buffer_.data(), packet_size);

        std::cout << "Sent SecurityDefinition for " << book.symbol
                  << " (id=" << book.security_id << ")" << std::endl;
    }
}

void CmeSimulator::sendIncrementalUpdate() {
    // Pick a random book and update it
    std::uniform_int_distribution<int> book_dist(0, 3);
    int book_idx = book_dist(rng_);
    auto& book = books_[book_idx];

    book.randomUpdate(rng_);

    // Build incremental entries for updated levels
    std::vector<cme::MDIncrementalRefreshEntry> entries;

    // Send top 3 levels for both sides
    for (int i = 0; i < 3; ++i) {
        cme::MDIncrementalRefreshEntry bid_entry = {};
        bid_entry.md_entry_px = book.bids[i].price;
        bid_entry.md_entry_size = book.bids[i].quantity;
        bid_entry.security_id = book.security_id;
        bid_entry.rpt_seq = book.rpt_seq;
        bid_entry.md_entry_type = static_cast<uint8_t>(cme::MDEntryType::Bid);
        bid_entry.md_update_action = static_cast<uint8_t>(cme::MDUpdateAction::Overlay);
        bid_entry.md_price_level = i + 1;  // 1-based
        bid_entry.number_of_orders = book.bids[i].order_count;
        entries.push_back(bid_entry);

        cme::MDIncrementalRefreshEntry ask_entry = {};
        ask_entry.md_entry_px = book.asks[i].price;
        ask_entry.md_entry_size = book.asks[i].quantity;
        ask_entry.security_id = book.security_id;
        ask_entry.rpt_seq = book.rpt_seq;
        ask_entry.md_entry_type = static_cast<uint8_t>(cme::MDEntryType::Offer);
        ask_entry.md_update_action = static_cast<uint8_t>(cme::MDUpdateAction::Overlay);
        ask_entry.md_price_level = i + 1;
        ask_entry.number_of_orders = book.asks[i].order_count;
        entries.push_back(ask_entry);
    }

    // Simulate gap if configured
    if (config_.simulate_gaps && (incr_packet_seq_ % config_.gap_frequency) == 0) {
        incr_packet_seq_++;  // Skip a sequence number
        std::cout << "SIMULATED GAP at incr_seq=" << incr_packet_seq_ << std::endl;
    }

    sendIncrementalPacket(entries);
}

void CmeSimulator::sendIncrementalPacket(const std::vector<cme::MDIncrementalRefreshEntry>& entries) {
    std::memset(send_buffer_.data(), 0, send_buffer_.size());

    // Packet header
    auto* pkt = reinterpret_cast<cme::PacketHeader*>(send_buffer_.data());
    pkt->msg_seq_num = ++incr_packet_seq_;
    pkt->sending_time = getCurrentTimeNs();

    // Message header + body
    auto* msg = reinterpret_cast<cme::MDIncrementalRefreshBook*>(
        send_buffer_.data() + sizeof(cme::PacketHeader));
    msg->init(static_cast<uint8_t>(entries.size()));
    msg->transact_time = pkt->sending_time;

    // Copy entries
    auto* entry_ptr = msg->getEntries();
    for (size_t i = 0; i < entries.size(); ++i) {
        entry_ptr[i] = entries[i];
    }

    size_t packet_size = sizeof(cme::PacketHeader) + cme::calcIncrementalSize(entries.size());
    incremental_sender_->send(send_buffer_.data(), packet_size);
}

void CmeSimulator::sendSnapshots() {
    for (const auto& book : books_) {
        sendSnapshotPacket(book);
    }
}

void CmeSimulator::sendSnapshotPacket(const SimulatedBook& book) {
    std::memset(send_buffer_.data(), 0, send_buffer_.size());

    // Packet header
    auto* pkt = reinterpret_cast<cme::PacketHeader*>(send_buffer_.data());
    pkt->msg_seq_num = ++snap_packet_seq_;
    pkt->sending_time = getCurrentTimeNs();

    // Snapshot message
    uint8_t num_entries = 10;  // 5 bids + 5 asks
    auto* msg = reinterpret_cast<cme::MDSnapshotFullRefresh*>(
        send_buffer_.data() + sizeof(cme::PacketHeader));
    msg->init(num_entries);
    msg->last_msg_seq_num_processed = incr_packet_seq_;  // Reference incremental sequence
    msg->security_id = book.security_id;
    msg->rpt_seq = book.rpt_seq;
    msg->transact_time = pkt->sending_time;

    // Fill entries
    auto* entries = msg->getEntries();
    for (int i = 0; i < 5; ++i) {
        // Bid
        entries[i].md_entry_px = book.bids[i].price;
        entries[i].md_entry_size = book.bids[i].quantity;
        entries[i].md_entry_type = static_cast<uint8_t>(cme::MDEntryType::Bid);
        entries[i].md_price_level = i + 1;
        entries[i].number_of_orders = book.bids[i].order_count;

        // Ask
        entries[5 + i].md_entry_px = book.asks[i].price;
        entries[5 + i].md_entry_size = book.asks[i].quantity;
        entries[5 + i].md_entry_type = static_cast<uint8_t>(cme::MDEntryType::Offer);
        entries[5 + i].md_price_level = i + 1;
        entries[5 + i].number_of_orders = book.asks[i].order_count;
    }

    size_t packet_size = sizeof(cme::PacketHeader) + cme::calcSnapshotSize(num_entries);
    snapshot_sender_->send(send_buffer_.data(), packet_size);
}

uint64_t CmeSimulator::getCurrentTimeNs() {
    auto now = std::chrono::system_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
    return static_cast<uint64_t>(ns);
}

} // namespace cme_simulator
