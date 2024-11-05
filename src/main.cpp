#include "DataService.h"
#include <spdlog/spdlog.h>
#include <iostream>

int main() {
    try {
        // Set up logging
        spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");

        std::cout << "Starting Stock Data Service...\n";

        // Create and run service
        StockTracker::DataService service;
        service.run();

        return 0;
    }
    catch (const std::exception& e) {
        spdlog::error("Fatal error: {}", e.what());
        return 1;
    }
}