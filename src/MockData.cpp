#include "MockData.h"
#include <stdexcept>
#include <spdlog/spdlog.h>

namespace StockTracker {

	MockDataProvider::MockDataProvider()
		: stock_configs{
			{"AAPL", {175.0, 0.002, 0.0001}},   // Stable, slight upward trend
			{"MSFT", {320.0, 0.0015, 0.00012}}, // Very stable
			{"GOOGL", {140.0, 0.0025, 0.00008}}, // More volatile
			{"AMZN", {130.0, 0.003, 0.00015}},   // High volatility
			{"META", {270.0, 0.0035, -0.00005}}  // High volatility, slight downtrend
		}
	{}

	StockQuote MockDataProvider::generateQuote(const std::string& symbol) {
		auto it = stock_configs.find(symbol);

		// Couldn't find the symbol
		if (it == stock_configs.end()) {
			throw std::runtime_error("Invalid symbol: " + symbol);
		}

		spdlog::info("Generating quote for {}", symbol);

		const auto& config = it->second;
		auto last_price = last_prices[symbol];

		// Initialize last price if first time
		if (last_price == 0.0) {
			last_price = config.base_price;
		}

		// generate random walk with drift
		std::normal_distribution<> dist(config.trend, config.volatility);
		double change = dist(rng);
		double new_price = last_price * (1.0 + change);

		// Calculate percent change
		double percent_change = ((new_price - last_price) / last_price) * 100;
		last_price = new_price;

		// Create a quote with the new price.
		auto quote = StockQuote::create(symbol, new_price);
		quote.change_percent = percent_change;
		return quote;
		
	}

	bool MockDataProvider::isValidSymbol(const std::string& symbol) const {
		return stock_configs.find(symbol) != stock_configs.end();
	}

	std::vector<std::string> MockDataProvider::getAvailableSymbols() const {
		std::vector<std::string> symbols;
		symbols.reserve(stock_configs.size());

		for (const auto& [symbol, _] : stock_configs) {
			symbols.push_back(symbol);
		}

		return symbols;
	}
}