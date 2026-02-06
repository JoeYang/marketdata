#include "feedhandler.h"

#include <cstring>
#include <iostream>
#include <iomanip>

namespace feedhandler {

FeedHandler::FeedHandler(const FeedHandlerConfig& config)
    : config_(config) {
    receiver_ = std::make_unique<MulticastReceiver>(
        config_.input_group, config_.input_port,
        config_.input_interface, config_.input_buffer_size);
    
    sender_ = std::make_unique<MulticastSender>(
        config_.output_group, config_.output_port,
        config_.output_interface, config_.output_ttl);
    
    book_manager_ = std::make_unique<OrderBookManager>(config_.book_depth);
}

FeedHandler::~FeedHandler() {
    stop();
}

bool FeedHandler::start() {
    if (running_) return true;
    
    if (!receiver_->start()) {
        std::cerr << "Failed to start receiver" << std::endl;
        return false;
    }
    
    if (!sender_->start()) {
        std::cerr << "Failed to start sender" << std::endl;
        receiver_->stop();
        return false;
    }
    
    running_ = true;
    last_conflation_time_ = std::chrono::steady_clock::now();
    last_stats_time_ = std::chrono::steady_clock::now();
    
    std::cout << "Feed handler started" << std::endl;
    std::cout << "  Mode: " << (config_.mode == ProcessingMode::TickByTick ? "tick-by-tick" : "conflated") << std::endl;
    if (config_.mode == ProcessingMode::Conflated) {
        std::cout << "  Conflation interval: " << config_.conflation_interval_ms << "ms" << std::endl;
    }
    
    return true;
}

void FeedHandler::stop() {
    if (!running_) return;
    
    running_ = false;
    receiver_->stop();
    sender_->stop();
    
    std::cout << "Feed handler stopped" << std::endl;
    printStats();
}

void FeedHandler::run() {
    if (!running_) {
        if (!start()) return;
    }
    
    std::vector<uint8_t> buffer(config_.input_buffer_size);
    
    while (running_) {
        // Poll with timeout for stats printing
        int ret = receiver_->poll(100);
        
        if (ret > 0) {
            ssize_t len = receiver_->read(buffer.data(), buffer.size());
            if (len > 0) {
                processMessage(buffer.data(), static_cast<size_t>(len));
            }
        }
        
        // Check conflation timer
        if (config_.mode == ProcessingMode::Conflated) {
            checkConflation();
        }
        
        // Print stats periodically
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_stats_time_).count();
        if (elapsed >= config_.stats_interval_sec) {
            printStats();
            last_stats_time_ = now;
        }
    }
}

void FeedHandler::processMessage(const uint8_t* data, size_t length) {
    stats_.messages_received++;
    stats_.bytes_received += length;
    
    // ITCH packets may contain multiple messages
    size_t offset = 0;
    while (offset + 2 < length) {
        // Get message length (big-endian 16-bit)
        uint16_t msg_len = (static_cast<uint16_t>(data[offset]) << 8) | data[offset + 1];
        
        if (msg_len == 0 || offset + 2 + msg_len > length) break;
        
        processItchMessage(data + offset + 2, msg_len);
        offset += 2 + msg_len;
    }
}

void FeedHandler::processItchMessage(const uint8_t* data, size_t length) {
    if (length < 1) return;
    
    auto type = static_cast<itch::MessageType>(data[0]);
    
    switch (type) {
        case itch::MessageType::AddOrder: {
            if (length < sizeof(itch::AddOrderMessage)) break;
            auto* msg = reinterpret_cast<const itch::AddOrderMessage*>(data);
            
            std::string symbol = msg->getStock();
            // Trim trailing spaces
            symbol.erase(symbol.find_last_not_of(' ') + 1);
            
            auto& book = book_manager_->getBook(symbol);
            book.addOrder(msg->getOrderRef(), msg->side, msg->getPrice(), msg->getShares());
            stats_.add_orders++;
            
            if (config_.mode == ProcessingMode::TickByTick) {
                auto quote = book.getBBO(0, ++sequence_);
                sendQuote(quote);
            }
            break;
        }
        
        case itch::MessageType::AddOrderMpid: {
            if (length < sizeof(itch::AddOrderMpidMessage)) break;
            auto* msg = reinterpret_cast<const itch::AddOrderMpidMessage*>(data);
            
            std::string symbol(msg->stock, 8);
            symbol.erase(symbol.find_last_not_of(' ') + 1);
            
            auto& book = book_manager_->getBook(symbol);
            book.addOrder(msg->getOrderRef(), msg->side, msg->getPrice(), msg->getShares());
            stats_.add_orders++;
            
            if (config_.mode == ProcessingMode::TickByTick) {
                auto quote = book.getBBO(0, ++sequence_);
                sendQuote(quote);
            }
            break;
        }
        
        case itch::MessageType::OrderDelete: {
            if (length < sizeof(itch::OrderDeleteMessage)) break;
            // Note: In production, maintain order_ref -> symbol mapping
            // to properly route deletes to the correct book
            (void)data;  // Suppress unused warning
            stats_.delete_orders++;
            break;
        }
        
        case itch::MessageType::OrderCancel: {
            if (length < sizeof(itch::OrderCancelMessage)) break;
            stats_.delete_orders++;
            break;
        }
        
        case itch::MessageType::OrderExecuted: {
            if (length < sizeof(itch::OrderExecutedMessage)) break;
            stats_.executions++;
            break;
        }
        
        case itch::MessageType::OrderExecutedWithPrice: {
            if (length < sizeof(itch::OrderExecutedWithPriceMessage)) break;
            stats_.executions++;
            break;
        }
        
        case itch::MessageType::Trade: {
            if (length < sizeof(itch::TradeMessage)) break;
            auto* msg = reinterpret_cast<const itch::TradeMessage*>(data);
            
            TradeTick trade{};
            std::memcpy(trade.symbol, msg->stock, 8);
            trade.timestamp = 0; // TODO: extract from message
            trade.sequence = ++sequence_;
            trade.price = msg->getPrice();
            trade.quantity = msg->getShares();
            trade.side = static_cast<char>(msg->side);
            
            stats_.trades++;
            
            if (config_.mode == ProcessingMode::TickByTick) {
                sendTrade(trade);
            }
            break;
        }
        
        default:
            // Ignore other message types
            break;
    }
}

void FeedHandler::sendSnapshot(const OrderBookSnapshot& snap) {
    std::vector<uint8_t> buffer;
    buffer.resize(sizeof(OutputHeader) + sizeof(OrderBookSnapshot));
    
    auto* header = reinterpret_cast<OutputHeader*>(buffer.data());
    header->length = static_cast<uint16_t>(buffer.size());
    header->type = OutputMessageType::OrderBookSnapshot;
    header->flags = 0;
    header->timestamp = snap.timestamp;
    
    std::memcpy(buffer.data() + sizeof(OutputHeader), &snap, sizeof(snap));
    
    if (sender_->send(buffer)) {
        stats_.messages_sent++;
        stats_.bytes_sent += buffer.size();
    }
}

void FeedHandler::sendQuote(const QuoteUpdate& quote) {
    std::vector<uint8_t> buffer;
    buffer.resize(sizeof(OutputHeader) + sizeof(QuoteUpdate));
    
    auto* header = reinterpret_cast<OutputHeader*>(buffer.data());
    header->length = static_cast<uint16_t>(buffer.size());
    header->type = OutputMessageType::QuoteUpdate;
    header->flags = 0;
    header->timestamp = quote.timestamp;
    
    std::memcpy(buffer.data() + sizeof(OutputHeader), &quote, sizeof(quote));
    
    if (sender_->send(buffer)) {
        stats_.messages_sent++;
        stats_.bytes_sent += buffer.size();
    }
}

void FeedHandler::sendTrade(const TradeTick& trade) {
    std::vector<uint8_t> buffer;
    buffer.resize(sizeof(OutputHeader) + sizeof(TradeTick));
    
    auto* header = reinterpret_cast<OutputHeader*>(buffer.data());
    header->length = static_cast<uint16_t>(buffer.size());
    header->type = OutputMessageType::TradeTick;
    header->flags = 0;
    header->timestamp = trade.timestamp;
    
    std::memcpy(buffer.data() + sizeof(OutputHeader), &trade, sizeof(trade));
    
    if (sender_->send(buffer)) {
        stats_.messages_sent++;
        stats_.bytes_sent += buffer.size();
    }
}

void FeedHandler::checkConflation() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_conflation_time_).count();
    
    if (elapsed >= config_.conflation_interval_ms) {
        sendConflatedSnapshots();
        last_conflation_time_ = now;
    }
}

void FeedHandler::sendConflatedSnapshots() {
    auto dirty_symbols = book_manager_->getDirtySymbols();
    
    for (const auto& symbol : dirty_symbols) {
        auto snap = book_manager_->getSnapshot(symbol, 0, ++sequence_);
        sendSnapshot(snap);
    }
    
    book_manager_->clearAllDirty();
}

void FeedHandler::printStats() {
    std::cout << "\n=== Feed Handler Stats ===" << std::endl;
    std::cout << "Messages received: " << stats_.messages_received << std::endl;
    std::cout << "Messages sent:     " << stats_.messages_sent << std::endl;
    std::cout << "Bytes received:    " << stats_.bytes_received << std::endl;
    std::cout << "Bytes sent:        " << stats_.bytes_sent << std::endl;
    std::cout << "Add orders:        " << stats_.add_orders << std::endl;
    std::cout << "Delete orders:     " << stats_.delete_orders << std::endl;
    std::cout << "Executions:        " << stats_.executions << std::endl;
    std::cout << "Trades:            " << stats_.trades << std::endl;
    std::cout << "==========================\n" << std::endl;
}

} // namespace feedhandler
