#include "itch_simulator.h"

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace {
    simulator::ItchSimulator* g_simulator = nullptr;
    
    void signalHandler(int sig) {
        std::cout << "\nReceived signal " << sig << ", shutting down..." << std::endl;
        if (g_simulator) {
            g_simulator->stop();
        }
    }
}

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "\nITCH Protocol Simulator - Generates test market data\n"
              << "\nOptions:\n"
              << "  --multicast-group <ip>   Multicast group (default: 239.1.1.1)\n"
              << "  --port <port>            Port (default: 30001)\n"
              << "  --interface <ip>         Network interface (default: 0.0.0.0)\n"
              << "  --rate <n>               Messages per second (default: 1000)\n"
              << "  --symbols <list>         Comma-separated symbols (default: AAPL,GOOGL,MSFT,AMZN,META)\n"
              << "  --min-price <cents>      Min price in cents (default: 10000 = $100)\n"
              << "  --max-price <cents>      Max price in cents (default: 50000 = $500)\n"
              << "  --help                   Show this help\n"
              << std::endl;
}

std::vector<std::string> parseSymbols(const std::string& input) {
    std::vector<std::string> symbols;
    std::stringstream ss(input);
    std::string item;
    
    while (std::getline(ss, item, ',')) {
        // Pad to 8 characters
        while (item.length() < 8) {
            item += ' ';
        }
        if (item.length() > 8) {
            item = item.substr(0, 8);
        }
        symbols.push_back(item);
    }
    
    return symbols;
}

int main(int argc, char* argv[]) {
    simulator::SimulatorConfig config;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        }
        else if (arg == "--multicast-group" && i + 1 < argc) {
            config.multicast_group = argv[++i];
        }
        else if (arg == "--port" && i + 1 < argc) {
            config.port = static_cast<uint16_t>(std::atoi(argv[++i]));
        }
        else if (arg == "--interface" && i + 1 < argc) {
            config.interface = argv[++i];
        }
        else if (arg == "--rate" && i + 1 < argc) {
            config.messages_per_second = std::atoi(argv[++i]);
        }
        else if (arg == "--symbols" && i + 1 < argc) {
            config.symbols = parseSymbols(argv[++i]);
        }
        else if (arg == "--min-price" && i + 1 < argc) {
            config.min_price = static_cast<uint32_t>(std::atoi(argv[++i]));
        }
        else if (arg == "--max-price" && i + 1 < argc) {
            config.max_price = static_cast<uint32_t>(std::atoi(argv[++i]));
        }
        else {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }
    
    // Validate
    if (config.symbols.empty()) {
        std::cerr << "No symbols specified" << std::endl;
        return 1;
    }
    
    if (config.min_price >= config.max_price) {
        std::cerr << "min-price must be less than max-price" << std::endl;
        return 1;
    }
    
    // Set up signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    // Create and run simulator
    simulator::ItchSimulator simulator(config);
    g_simulator = &simulator;
    
    std::cout << "Starting ITCH Simulator..." << std::endl;
    
    simulator.run();
    
    g_simulator = nullptr;
    return 0;
}
