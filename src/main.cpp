#include "feedhandler/feedhandler.h"

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace {
    feedhandler::FeedHandler* g_handler = nullptr;
    
    void signalHandler(int sig) {
        std::cout << "\nReceived signal " << sig << ", shutting down..." << std::endl;
        if (g_handler) {
            g_handler->stop();
        }
    }
}

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "\nOptions:\n"
              << "  --mode <tick|conflated>     Processing mode (default: tick)\n"
              << "  --interval-ms <ms>          Conflation interval in ms (default: 100)\n"
              << "  --input-group <ip>          Input multicast group (default: 239.1.1.1)\n"
              << "  --input-port <port>         Input port (default: 30001)\n"
              << "  --output-group <ip>         Output multicast group (default: 239.1.1.2)\n"
              << "  --output-port <port>        Output port (default: 30002)\n"
              << "  --interface <ip>            Network interface (default: 0.0.0.0)\n"
              << "  --depth <n>                 Order book depth (default: 10)\n"
              << "  --stats-interval <sec>      Stats print interval (default: 10)\n"
              << "  --help                      Show this help\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    feedhandler::FeedHandlerConfig config;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        }
        else if (arg == "--mode" && i + 1 < argc) {
            std::string mode = argv[++i];
            if (mode == "tick") {
                config.mode = feedhandler::ProcessingMode::TickByTick;
            } else if (mode == "conflated") {
                config.mode = feedhandler::ProcessingMode::Conflated;
            } else {
                std::cerr << "Unknown mode: " << mode << std::endl;
                return 1;
            }
        }
        else if (arg == "--interval-ms" && i + 1 < argc) {
            config.conflation_interval_ms = std::atoi(argv[++i]);
        }
        else if (arg == "--input-group" && i + 1 < argc) {
            config.input_group = argv[++i];
        }
        else if (arg == "--input-port" && i + 1 < argc) {
            config.input_port = static_cast<uint16_t>(std::atoi(argv[++i]));
        }
        else if (arg == "--output-group" && i + 1 < argc) {
            config.output_group = argv[++i];
        }
        else if (arg == "--output-port" && i + 1 < argc) {
            config.output_port = static_cast<uint16_t>(std::atoi(argv[++i]));
        }
        else if (arg == "--interface" && i + 1 < argc) {
            config.input_interface = argv[++i];
            config.output_interface = config.input_interface;
        }
        else if (arg == "--depth" && i + 1 < argc) {
            config.book_depth = static_cast<size_t>(std::atoi(argv[++i]));
        }
        else if (arg == "--stats-interval" && i + 1 < argc) {
            config.stats_interval_sec = std::atoi(argv[++i]);
        }
        else {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }
    
    // Set up signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    // Create and run feed handler
    feedhandler::FeedHandler handler(config);
    g_handler = &handler;
    
    std::cout << "Starting market data feed handler..." << std::endl;
    std::cout << "Input:  " << config.input_group << ":" << config.input_port << std::endl;
    std::cout << "Output: " << config.output_group << ":" << config.output_port << std::endl;
    
    handler.run();
    
    g_handler = nullptr;
    return 0;
}
