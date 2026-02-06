#include "src/cme/cme_protocol.h"
#include "src/cme/l2_sbe_messages.h"
#include "src/feedhandler/multicast.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

static std::atomic<bool> g_running{true};

void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ", stopping receiver..." << std::endl;
    g_running = false;
}

std::string formatTimestamp(uint64_t timestamp_ns) {
    // Convert nanoseconds since epoch to readable format
    auto seconds = timestamp_ns / 1000000000ULL;
    auto nanos = timestamp_ns % 1000000000ULL;

    std::time_t time = static_cast<std::time_t>(seconds);
    std::tm* tm = std::localtime(&time);

    std::ostringstream ss;
    ss << std::put_time(tm, "%Y-%m-%d %H:%M:%S")
       << "." << std::setfill('0') << std::setw(6) << (nanos / 1000);  // microseconds
    return ss.str();
}

std::string formatSbePrice(int64_t sbePrice) {
    // SBE price has 7 implied decimal places
    double p = l2md::priceToDouble(sbePrice);
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << p;
    return ss.str();
}

void printOrderBook(const l2md::L2SnapshotDecoder& snap) {
    std::string symbol = snap.symbol();
    // Trim trailing spaces/nulls
    while (!symbol.empty() && (symbol.back() == ' ' || symbol.back() == '\0')) {
        symbol.pop_back();
    }

    std::cout << "\n" << symbol << " @ " << formatTimestamp(snap.timestamp())
              << " (seq=" << snap.sequenceNumber() << ")" << std::endl;

    std::cout << "  BID                    ASK" << std::endl;
    std::cout << "  ---                    ---" << std::endl;

    size_t max_levels = std::max(static_cast<size_t>(snap.bidCount()),
                                  static_cast<size_t>(snap.askCount()));
    max_levels = std::min(max_levels, static_cast<size_t>(5));  // Show top 5

    for (size_t i = 0; i < max_levels; ++i) {
        std::ostringstream line;

        // Bid side
        const auto* bid = snap.getBid(i);
        if (bid && i < snap.bidCount()) {
            line << "  " << std::setw(5) << bid->quantity
                 << " @ " << std::setw(10) << formatSbePrice(bid->price);
        } else {
            line << "                       ";
        }

        line << "    ";

        // Ask side
        const auto* ask = snap.getAsk(i);
        if (ask && i < snap.askCount()) {
            line << std::setw(5) << ask->quantity
                 << " @ " << std::setw(10) << formatSbePrice(ask->price);
        }

        std::cout << line.str() << std::endl;
    }

    // Show last trade if available
    if (snap.lastTradePrice() > 0) {
        std::cout << "  Last: " << formatSbePrice(snap.lastTradePrice())
                  << " x " << snap.lastTradeQty()
                  << " | Volume: " << snap.totalVolume() << std::endl;
    }
}

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "\nOptions:\n"
              << "  --group <ip>        Multicast group (default: " << cme::CME_OUTPUT_GROUP << ")\n"
              << "  --port <port>       Port (default: " << cme::CME_OUTPUT_PORT << ")\n"
              << "  --interface <ip>    Network interface (default: 0.0.0.0)\n"
              << "  --filter <symbol>   Only show this symbol\n"
              << "  --raw               Show raw SBE message details\n"
              << "  -h, --help          Show this help\n"
              << "\nSBE Schema: ID=" << l2md::SCHEMA_ID << ", Version=" << l2md::SCHEMA_VERSION << "\n"
              << std::endl;
}

void printRawMessage(const uint8_t* data, size_t len) {
    l2md::MessageDecoder decoder(data, len);
    if (!decoder.isValid()) {
        std::cout << "  [Invalid SBE message]" << std::endl;
        return;
    }

    const auto* header = decoder.getHeader();
    std::cout << "  SBE Header: blockLength=" << header->blockLength
              << " templateId=" << header->templateId
              << " schemaId=" << header->schemaId
              << " version=" << header->version << std::endl;

    if (decoder.isL2Snapshot()) {
        l2md::L2SnapshotDecoder snap(data, len);
        std::cout << "  L2Snapshot: symbol=" << snap.symbol()
                  << " seq=" << snap.sequenceNumber()
                  << " bids=" << (int)snap.numBids()
                  << " asks=" << (int)snap.numAsks() << std::endl;
    } else if (decoder.isHeartbeat()) {
        l2md::HeartbeatDecoder hb(data, len);
        std::cout << "  Heartbeat: seq=" << hb.sequenceNumber() << std::endl;
    }
}

int main(int argc, char* argv[]) {
    std::string group = cme::CME_OUTPUT_GROUP;
    uint16_t port = cme::CME_OUTPUT_PORT;
    std::string interface = "0.0.0.0";
    std::string filter_symbol;
    bool show_raw = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--group") == 0 && i + 1 < argc) {
            group = argv[++i];
        } else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--interface") == 0 && i + 1 < argc) {
            interface = argv[++i];
        } else if (std::strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
            filter_symbol = argv[++i];
        } else if (std::strcmp(argv[i], "--raw") == 0) {
            show_raw = true;
        } else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            printUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << argv[i] << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    feedhandler::MulticastReceiver receiver(group, port, interface);

    if (!receiver.start()) {
        std::cerr << "Failed to start receiver" << std::endl;
        return 1;
    }

    std::cout << "CME Consumer listening on " << group << ":" << port << std::endl;
    std::cout << "SBE Schema: ID=" << l2md::SCHEMA_ID << ", Version=" << l2md::SCHEMA_VERSION << std::endl;
    if (!filter_symbol.empty()) {
        std::cout << "Filtering for symbol: " << filter_symbol << std::endl;
    }

    std::vector<uint8_t> buffer(65536);
    uint64_t messages_received = 0;
    uint64_t snapshots_received = 0;
    uint64_t heartbeats_received = 0;

    while (g_running) {
        int ret = receiver.poll(100);  // 100ms timeout

        if (ret > 0) {
            ssize_t len = receiver.read(buffer.data(), buffer.size());
            if (len > 0) {
                messages_received++;

                // Use SBE decoder
                l2md::MessageDecoder decoder(buffer.data(), static_cast<size_t>(len));

                if (!decoder.isValid()) {
                    std::cerr << "Invalid SBE message received" << std::endl;
                    continue;
                }

                if (show_raw) {
                    printRawMessage(buffer.data(), static_cast<size_t>(len));
                }

                if (decoder.isL2Snapshot()) {
                    l2md::L2SnapshotDecoder snap(buffer.data(), static_cast<size_t>(len));

                    if (!snap.isValid()) {
                        std::cerr << "Invalid L2 Snapshot message" << std::endl;
                        continue;
                    }

                    snapshots_received++;

                    // Apply filter if set
                    if (!filter_symbol.empty()) {
                        std::string symbol = snap.symbol();
                        while (!symbol.empty() && (symbol.back() == ' ' || symbol.back() == '\0')) {
                            symbol.pop_back();
                        }
                        if (symbol != filter_symbol) {
                            continue;
                        }
                    }

                    printOrderBook(snap);

                } else if (decoder.isHeartbeat()) {
                    heartbeats_received++;
                    // Silent heartbeat
                }
            }
        }
    }

    std::cout << "\nReceived " << messages_received << " messages total" << std::endl;
    std::cout << "  L2 Snapshots: " << snapshots_received << std::endl;
    std::cout << "  Heartbeats: " << heartbeats_received << std::endl;
    return 0;
}
