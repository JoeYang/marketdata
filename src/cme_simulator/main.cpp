#include "cme_simulator.h"

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>

static cme_simulator::CmeSimulator* g_simulator = nullptr;

void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ", stopping simulator..." << std::endl;
    if (g_simulator) {
        g_simulator->stop();
    }
}

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "\nOptions:\n"
              << "  --interface <ip>      Network interface (default: 0.0.0.0)\n"
              << "  --rate <n>            Updates per second (default: 100)\n"
              << "  --snapshot-interval <ms>  Snapshot interval in ms (default: 1000)\n"
              << "  --simulate-gaps       Simulate packet gaps for testing recovery\n"
              << "  --gap-frequency <n>   Gap every N packets (default: 100)\n"
              << "  -h, --help            Show this help\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    cme_simulator::CmeSimulator::Config config;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--interface") == 0 && i + 1 < argc) {
            config.interface = argv[++i];
        } else if (std::strcmp(argv[i], "--rate") == 0 && i + 1 < argc) {
            config.updates_per_second = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--snapshot-interval") == 0 && i + 1 < argc) {
            config.snapshot_interval_ms = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--simulate-gaps") == 0) {
            config.simulate_gaps = true;
        } else if (std::strcmp(argv[i], "--gap-frequency") == 0 && i + 1 < argc) {
            config.gap_frequency = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            printUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << argv[i] << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    cme_simulator::CmeSimulator simulator(config);
    g_simulator = &simulator;

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    if (!simulator.start()) {
        std::cerr << "Failed to start simulator" << std::endl;
        return 1;
    }

    simulator.run();

    return 0;
}
