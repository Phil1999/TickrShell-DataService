#pragma once
#include <StockTracker/Types.h>
#include <vector>
#include <string>


namespace StockTracker {


	class IStockDataProvider {
	public:
		virtual ~IStockDataProvider() = default;
		virtual StockQuote generateQuote(const std::string& symbol) = 0;
		virtual bool isValidSymbol(const std::string& symbol) const = 0;
		virtual std::vector<std::string> getAvailableSymbols() const = 0;
	};
}