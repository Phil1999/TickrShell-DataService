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
        , currency_service()
    {
        // Set up ZeroMQ sockets
        subscriber.connect("tcp://localhost:5557");  // Listen for commands from CLI
        publisher.bind("tcp://*:5556");             // Publish updates to CLI

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

            case MessageType::SetCurrency:
                spdlog::info("Handling currency change request to: {}", msg.currency);
                if (CurrencyService::isValidCurrencyCode(msg.currency)) {
                    current_currency = msg.currency;
                    // Resend all current prices in new currency
                    for (const auto& symbol : subscribed_stocks) {
                        queryStock(symbol);
                    }
                    spdlog::info("Currency updated to {}", current_currency);
                }
                else {
                    publisher.send(Message::makeError("Invalid currency code: " + msg.currency));
                }
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

                // Send an immediate stock update using the current currency setting
                auto quote = mock_data.generateQuote(symbol);
                quote.currency = "USD";  // Set base currency

                // Convert if not USD
                if (current_currency != "USD") {
                    try {
                        // Get exchange rate
                        double exchange_rate = currency_service.convertCurrency(1.0, current_currency);
                        // Apply exchange rate to the price
                        double converted_price = quote.price * exchange_rate;

                        quote.price = converted_price;
                        quote.currency = current_currency;
                    }
                    catch (const std::exception& e) {
                        spdlog::error("Currency conversion failed on subscribe: {}", e.what());
                    }
                }

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
        if (!mock_data.isValidSymbol(symbol)) {
            publisher.send(Message::makeError("Invalid symbol: " + symbol));
            return;
        }

        try {
            // Get base quote in USD
            auto quote = mock_data.generateQuote(symbol);
            quote.currency = "USD";

            // Convert if not USD
            if (current_currency != "USD") {
                try {
                    double converted_price = currency_service.convertCurrency(quote.price, current_currency);
                    quote.price = converted_price;
                    quote.currency = current_currency;
                    spdlog::info("Converted price from USD ${:.2f} to {} {:.2f}",
                        quote.price / converted_price, current_currency, converted_price);
                }
                catch (const std::exception& e) {
                    spdlog::error("Currency conversion failed: {}", e.what());
                    // Continue with USD price if conversion fails
                }
            }

            publisher.send(Message::makeQuoteUpdate(quote));
            storeStockPrice(symbol, quote.price, quote.timestamp);
            spdlog::info("Sent quote for {} in {}: {}", symbol, quote.currency, quote.price);
        }
        catch (const std::exception& e) {
            spdlog::error("Error querying stock {}: {}", symbol, e.what());
            publisher.send(Message::makeError(e.what()));
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

    StockQuote DataService::convertQuoteCurrency(const StockQuote& quote) {
        if (current_currency == "USD" || current_currency == quote.currency) {
            return quote; // No conversion needed
        }

        try {
            StockQuote converted = quote;
            converted.price = currency_service.convertCurrency(quote.price, current_currency);
            converted.currency = current_currency;
            return converted;
        }
        catch (const std::exception& e) {
            spdlog::warn("Currency conversion failed: {}. Using original USD price.", e.what());
            return quote; // Return original quote on error
        }
    }

    void DataService::run() {
        // Start update thread for subscribed stocks
        std::thread update_thread([this]() {
            while (running) {
                for (const auto& symbol : subscribed_stocks) {
                    try {
                        // Get base quote in USD
                        auto quote = mock_data.generateQuote(symbol);
                        quote.currency = "USD";

                        // Convert if not USD
                        if (current_currency != "USD") {
                            try {
                                double converted_price = currency_service.convertCurrency(quote.price, current_currency);
                                quote.price = converted_price;
                                quote.currency = current_currency;
                            }
                            catch (const std::exception& e) {
                                spdlog::error("Currency conversion failed for {}: {}", symbol, e.what());
                                // Continue with USD price if conversion fails
                            }
                        }

                        publisher.send(Message::makeQuoteUpdate(quote));
                        storeStockPrice(symbol, quote.price, quote.timestamp);
                    }
                    catch (const std::exception& e) {
                        spdlog::error("Error generating quote for {}: {}", symbol, e.what());
                    }
                }
                std::this_thread::sleep_for(std::chrono::seconds(8));
            }
            });

        // Handle incoming messages in main thread
        while (running) {
            try {
                if (auto msg = subscriber.receive(false)) {  // Blocking receive
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