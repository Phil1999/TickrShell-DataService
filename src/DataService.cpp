// StockTracker.DataService/src/DataService.cpp
#include "DataService.h"
#include <spdlog/spdlog.h>
#include <thread>
#include <chrono>

namespace StockTracker {

    DataService::DataService()
        : subscriber(zmq::socket_type::sub)
        , publisher(zmq::socket_type::pub)
        , db_service("stocktracker.db")
    {
        // Set up ZeroMQ sockets
        subscriber.connect("tcp://localhost:5556");  // Listen for commands from CLI
        publisher.bind("tcp://*:5555");             // Publish updates to CLI

        // Subscribe to all command messages
        subscriber.setSubscribe("");

        // Load any previously subscribed stocks from SQLite
        auto subscriptions = db_service.getSubscriptions();
        for (const auto& symbol : subscriptions) {
            subscribed_stocks.insert(symbol);
            spdlog::info("Restored subscription for {}", symbol);
        }

        spdlog::info("DataService initialized");
    }


    void DataService::handleMessage(const Message& msg) {
        try {
            switch (msg.type) {
            case MessageType::Subscribe:
                subscribeStock(msg.symbol);
                break;

            case MessageType::Unsubscribe:
                unsubscribeStock(msg.symbol);
                break;

            case MessageType::Query:
                queryStock(msg.symbol);
                break;

            case MessageType::PriceHistoryRequest:
                sendPriceHistory(msg.symbol);
                break;

            case MessageType::RequestSubscriptions:
                spdlog::info("Handling RequestSubscriptions");
                sendSubscriptionsList();
                break;

            default:
                spdlog::warn("Received unexpected message type");
                break;
            }
        }
        catch (const std::exception& e) {
            spdlog::error("Error handling message: {}", e.what());
            publisher.send(Message::makeError(e.what()));
        }
    }

    void DataService::subscribeStock(const std::string& symbol) {
        // Check if the symbol is valid (exists in mock data)
        if (mock_data.isValidSymbol(symbol)) {
            // Insert into the subscribed stocks set
            if (subscribed_stocks.insert(symbol).second) {
                spdlog::info("Subscribed to {}", symbol);

                // Persist the subscription in SQLite
                db_service.saveSubscription(symbol);

                // Send a confirmation message to the CLI
                publisher.send(Message::makeSubscribe(symbol));

                // Send an immediate stock update upon subscribing
                auto quote = mock_data.generateQuote(symbol);
                publisher.send(Message::makeQuoteUpdate(quote));
                storeStockPrice(symbol, quote.price, quote.timestamp);
            }
            else {
                spdlog::info("Already subscribed to {}", symbol);
                publisher.send(Message::makeError("Already subscribed to " + symbol));
            }
        }
        else {
            // Send an error if the symbol is invalid
            publisher.send(Message::makeError("Invalid symbol: " + symbol));
        }
    }

    void DataService::unsubscribeStock(const std::string& symbol) {
        // Check if the symbol exists in the subscription list
        if (subscribed_stocks.find(symbol) != subscribed_stocks.end()) {
            // Remove the subscription from SQLite
            db_service.removeSubscription(symbol);

            // Erase from the set and log success
            subscribed_stocks.erase(symbol);

            spdlog::info("Unsubscribed from {}", symbol);
            publisher.send(Message::makeUnsubscribe(symbol)); // Notify client
        }
        else {
            // Log and send error if symbol is not found in subscriptions
            spdlog::warn("Attempted to unsubscribe from non-subscribed symbol: {}", symbol);
            publisher.send(Message::makeError("Symbol " + symbol + " is not subscribed."));
        }
    }

    void DataService::queryStock(const std::string& symbol) {
        if (mock_data.isValidSymbol(symbol)) {
            auto quote = mock_data.generateQuote(symbol);
            publisher.send(Message::makeQuoteUpdate(quote));
            storeStockPrice(symbol, quote.price, quote.timestamp);
            spdlog::info("Sent one-time quote for {}: ${:.2f}", symbol, quote.price);
        }
        else {
            publisher.send(Message::makeError("Invalid symbol: " + symbol));
        }
    }

    // Send price history to CLI
    void DataService::sendPriceHistory(const std::string& symbol) {
        auto history = db_service.getPriceHistory(symbol);

        publisher.send(Message::makePriceHistory(symbol, history));

        spdlog::info("Sent price history for {}", symbol);
    }

    void DataService::sendSubscriptionsList() {
        auto subscriptions = db_service.getSubscriptions();
        spdlog::info("Sending subscription list with {} entries to CLI", subscriptions.size());
        for (const auto& symbol : subscriptions) {
            spdlog::info("Subscription symbol: {}", symbol);
        }

        Message msg = Message::makeSubscriptionsList(subscriptions);
        publisher.send(msg);  // Send to CLI
        spdlog::info("SubscriptionsList message sent to CLI.");
    }

    void DataService::storeStockPrice(const std::string& symbol, double price,
        const std::chrono::system_clock::time_point& timestamp) {
        
        StockQuote quote{ symbol, price, timestamp };
        db_service.savePrice(quote);
    }

    void DataService::run() {
        // Start update thread for subscribed stocks
        std::thread update_thread([this]() {
            while (running) {
                for (const auto& symbol : subscribed_stocks) {
                    try {
                        auto quote = mock_data.generateQuote(symbol);
                        publisher.send(Message::makeQuoteUpdate(quote));
                        storeStockPrice(symbol, quote.price, quote.timestamp);
                    }
                    catch (const std::exception& e) {
                        spdlog::error("Error generating quote for {}: {}", symbol, e.what());
                    }
                }
                std::this_thread::sleep_for(std::chrono::seconds(10));
            }
            });

        // Handle incoming messages in main thread
        while (running) {
            try {
                // Use blocking receive (no "true" flag), so it waits until a message is received
                if (auto msg = subscriber.receive(false)) {  // Now it's a blocking receive
                    spdlog::info("Received message of type: {}", static_cast<int>(msg->type));
                    handleMessage(*msg);
                }
                else {
                    spdlog::warn("Blocking receive unexpectedly returned no message.");
                }
            }
            catch (const std::exception& e) {
                spdlog::error("Error receiving message: {}", e.what());
            }
        }

        if (update_thread.joinable()) {
            update_thread.join();
        }
    }

    void DataService::stop() {
        running = false;
    }

}