#include "cme_feedhandler.h"

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>

static cme::CmeFeedHandler* g_handler = nullptr;

void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ", stopping feed handler..." << std::endl;
    if (g_handler) {
        g_handler->stop();
    }
}

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "\nOptions:\n"
              << "  --interface <ip>          Network interface (default: 0.0.0.0)\n"
              << "  --conflation-interval <ms> Conflation interval in ms (default: 100)\n"
              << "  --recovery-timeout <ms>   Recovery timeout in ms (default: 5000)\n"
              << "  -h, --help                Show this help\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    cme::CmeFeedHandler::Config config;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--interface") == 0 && i + 1 < argc) {
            config.interface = argv[++i];
        } else if (std::strcmp(argv[i], "--conflation-interval") == 0 && i + 1 < argc) {
            config.conflation_interval_ms = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--recovery-timeout") == 0 && i + 1 < argc) {
            config.recovery_timeout_ms = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            printUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << argv[i] << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    cme::CmeFeedHandler handler(config);
    g_handler = &handler;

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    if (!handler.start()) {
        std::cerr << "Failed to start feed handler" << std::endl;
        return 1;
    }

    handler.run();

    return 0;
}
