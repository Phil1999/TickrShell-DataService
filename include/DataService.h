#pragma once
#include "StockTracker/Messages.h"
#include "StockTracker/DatabaseService.h"
#include "StockTracker/CurrencyService.h"
#include "MockData.h"
#include <sqlite3.h>
#include <unordered_set>
#include <atomic>

namespace StockTracker {
    class DataService {
    private:
        MessageSocket subscriber;   // Receives commands from CLI
        MessageSocket publisher;    // Sends updates to CLI
        MockDataProvider mock_data; // mock stock data
        DatabaseService db_service; // Manages SQLite interactions
        CurrencyService currency_service;
        std::string current_currency{ "USD" };
        std::unordered_set<std::string> subscribed_stocks;
        std::atomic<bool> running{ true };

        // Message handling
        void handleMessage(const Message& msg);
        void subscribeStock(const std::string& symbol);
        void unsubscribeStock(const std::string& symbol);
        void queryStock(const std::string& symbol);
        void sendPriceHistory(const std::string& symbol);
        void sendSubscriptionsList();

        StockQuote convertQuoteCurrency(const StockQuote& quote);

        // Data storage (for SQLite)
        void storeStockPrice(const std::string& symbol, double price,
            const std::chrono::system_clock::time_point& timestamp);

    public:
        DataService();

        void run();
        void stop();

        // Prevent copying and moving
        DataService(const DataService&) = delete;
        DataService& operator=(const DataService&) = delete;
    };
}