#include "market_snapshot.hpp"

#include "json_value.hpp"

#include <stdexcept>

MarketTop parse_market_top(const MarketDataSnapshot &snapshot)
{
    const nlohmann::json *book = &snapshot.orderbook;
    if (book->contains("result"))
        book = &book->at("result");
    if (!book->contains("b") || !book->contains("a") || book->at("b").empty() || book->at("a").empty())
        throw std::runtime_error("orderbook missing best bid/ask for " + snapshot.symbol);

    const nlohmann::json *ticker = &snapshot.ticker;
    if (ticker->contains("result") && ticker->at("result").contains("list"))
    {
        ticker = nullptr;
        for (const auto &row : snapshot.ticker.at("result").at("list"))
            if (row.value("symbol", "") == snapshot.symbol)
            {
                ticker = &row;
                break;
            }
    }
    if (ticker == nullptr || (ticker->contains("symbol") && ticker->value("symbol", "") != snapshot.symbol) ||
        !ticker->contains("markPrice"))
        throw std::runtime_error("ticker missing exact-symbol markPrice for " + snapshot.symbol);
    const double bid = json_finite_number(book->at("b").at(0).at(0), "best bid");
    const double ask = json_finite_number(book->at("a").at(0).at(0), "best ask");
    const double mark_price = json_finite_number(ticker->at("markPrice"), "markPrice");
    if (bid <= 0.0 || ask <= bid)
        throw std::runtime_error("orderbook best bid/ask is invalid for " + snapshot.symbol);
    if (mark_price <= 0.0)
        throw std::runtime_error("ticker markPrice is invalid for " + snapshot.symbol);
    return {bid, ask, mark_price};
}
