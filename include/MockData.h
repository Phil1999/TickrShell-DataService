#pragma once
#include "IStockDataProvider.h"
#include <random>
#include <unordered_map>

namespace StockTracker {

	class MockDataProvider: public IStockDataProvider {
	private:
		std::mt19937 rng{ std::random_device{}() };


		struct StockConfig {
			double base_price;
			double volatility; // How much the price can change
			double trend; // The general price direction (positive=up, negative=down)
		};

		// Keep track of last prices so we can calculate changes
		std::unordered_map<std::string, double> last_prices;

		// Config for each stock
		const std::unordered_map<std::string, StockConfig> stock_configs;

	public:

		MockDataProvider();

		// Generate new quote with realistic price movement
		StockQuote generateQuote(const std::string& symbol) override;

		// Check if symbol exists
		bool isValidSymbol(const std::string& symbol) const override;

		// Get list of available symbols
		std::vector<std::string> getAvailableSymbols() const override;
	};

}