#include <iostream>
#include <iomanip>
#include <cstring>
#include <csignal>
#include <getopt.h>

#include "../feedhandler/multicast.h"
#include "../feedhandler/market_data.h"

using namespace feedhandler;

static volatile bool running = true;

void signalHandler(int) {
    running = false;
}

void printQuote(const QuoteUpdate& quote) {
    std::string symbol(quote.symbol, 8);
    symbol.erase(symbol.find_last_not_of(' ') + 1);

    std::cout << "[QUOTE] " << std::setw(8) << std::left << symbol
              << " | Bid: " << std::fixed << std::setprecision(2)
              << std::setw(10) << std::right << (quote.bid_price / 10000.0)
              << " x " << std::setw(6) << quote.bid_quantity
              << " | Ask: " << std::setw(10) << (quote.ask_price / 10000.0)
              << " x " << std::setw(6) << quote.ask_quantity
              << " | seq=" << quote.sequence
              << std::endl;
}

void printTrade(const TradeTick& trade) {
    std::string symbol(trade.symbol, 8);
    symbol.erase(symbol.find_last_not_of(' ') + 1);

    std::cout << "[TRADE] " << std::setw(8) << std::left << symbol
              << " | Price: " << std::fixed << std::setprecision(2)
              << std::setw(10) << std::right << (trade.price / 10000.0)
              << " | Qty: " << std::setw(6) << trade.quantity
              << " | Side: " << trade.side
              << " | seq=" << trade.sequence
              << std::endl;
}

void printSnapshot(const OrderBookSnapshot& snap) {
    std::string symbol(snap.symbol, 8);
    symbol.erase(symbol.find_last_not_of(' ') + 1);

    std::cout << "\n[SNAPSHOT] " << symbol << " (seq=" << snap.sequence << ")\n";
    std::cout << std::string(60, '-') << "\n";
    std::cout << std::setw(30) << "BIDS" << " | " << std::setw(28) << "ASKS" << "\n";
    std::cout << std::string(60, '-') << "\n";

    size_t max_levels = std::max(static_cast<size_t>(snap.bids.count),
                                  static_cast<size_t>(snap.asks.count));

    for (size_t i = 0; i < max_levels && i < MAX_DEPTH; ++i) {
        // Bid side
        if (i < snap.bids.count) {
            const auto& bid = snap.bids.levels[i];
            std::cout << std::fixed << std::setprecision(2)
                      << std::setw(8) << bid.quantity << " @ "
                      << std::setw(10) << (bid.price / 10000.0)
                      << " (" << std::setw(3) << bid.order_count << ")";
        } else {
            std::cout << std::setw(27) << "";
        }

        std::cout << " | ";

        // Ask side
        if (i < snap.asks.count) {
            const auto& ask = snap.asks.levels[i];
            std::cout << std::fixed << std::setprecision(2)
                      << std::setw(10) << (ask.price / 10000.0) << " x "
                      << std::setw(8) << ask.quantity
                      << " (" << std::setw(3) << ask.order_count << ")";
        }
        std::cout << "\n";
    }

    if (snap.last_price > 0) {
        std::cout << "Last: " << std::fixed << std::setprecision(2)
                  << (snap.last_price / 10000.0)
                  << " x " << snap.last_quantity
                  << " | Volume: " << snap.total_volume << "\n";
    }
    std::cout << std::string(60, '-') << "\n\n";
}

void processMessage(const uint8_t* data, size_t length) {
    if (length < sizeof(OutputHeader)) {
        std::cerr << "Message too short: " << length << " bytes\n";
        return;
    }

    const auto* header = reinterpret_cast<const OutputHeader*>(data);
    const uint8_t* payload = data + sizeof(OutputHeader);
    size_t payload_len = length - sizeof(OutputHeader);

    switch (header->type) {
        case OutputMessageType::QuoteUpdate:
            if (payload_len >= sizeof(QuoteUpdate)) {
                QuoteUpdate quote;
                std::memcpy(&quote, payload, sizeof(QuoteUpdate));
                printQuote(quote);
            }
            break;

        case OutputMessageType::TradeTick:
            if (payload_len >= sizeof(TradeTick)) {
                TradeTick trade;
                std::memcpy(&trade, payload, sizeof(TradeTick));
                printTrade(trade);
            }
            break;

        case OutputMessageType::OrderBookSnapshot:
            if (payload_len >= sizeof(OrderBookSnapshot)) {
                OrderBookSnapshot snap;
                std::memcpy(&snap, payload, sizeof(snap));
                printSnapshot(snap);
            }
            break;

        case OutputMessageType::Heartbeat:
            std::cout << "[HEARTBEAT] ts=" << header->timestamp << std::endl;
            break;

        default:
            std::cout << "[UNKNOWN] type=" << static_cast<int>(header->type)
                      << " len=" << header->length << std::endl;
            break;
    }
}

void printUsage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  -g, --group <ip>     Multicast group (default: 239.1.1.2)\n"
              << "  -p, --port <port>    Port number (default: 30002)\n"
              << "  -i, --interface <ip> Interface to bind (default: 0.0.0.0)\n"
              << "  -h, --help           Show this help\n";
}

int main(int argc, char* argv[]) {
    std::string group = "239.1.1.2";
    uint16_t port = 30002;
    std::string interface = "0.0.0.0";

    static struct option long_options[] = {
        {"group", required_argument, nullptr, 'g'},
        {"port", required_argument, nullptr, 'p'},
        {"interface", required_argument, nullptr, 'i'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "g:p:i:h", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'g':
                group = optarg;
                break;
            case 'p':
                port = static_cast<uint16_t>(std::stoi(optarg));
                break;
            case 'i':
                interface = optarg;
                break;
            case 'h':
            default:
                printUsage(argv[0]);
                return opt == 'h' ? 0 : 1;
        }
    }

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::cout << "Market Data Receiver\n"
              << "====================\n"
              << "Multicast group: " << group << "\n"
              << "Port:            " << port << "\n"
              << "Interface:       " << interface << "\n"
              << "Press Ctrl+C to stop\n\n";

    MulticastReceiver receiver(group, port, interface);

    if (!receiver.start()) {
        std::cerr << "Failed to start receiver\n";
        return 1;
    }

    std::cout << "Listening for market data...\n\n";

    std::vector<uint8_t> buffer(65536);
    uint64_t msg_count = 0;

    while (running) {
        int ret = receiver.poll(100);

        if (ret > 0) {
            ssize_t len = receiver.read(buffer.data(), buffer.size());
            if (len > 0) {
                processMessage(buffer.data(), static_cast<size_t>(len));
                msg_count++;
            }
        }
    }

    std::cout << "\nReceived " << msg_count << " messages\n";
    receiver.stop();

    return 0;
}
