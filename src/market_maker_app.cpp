#include "market_maker_app.hpp"

#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "account_state.hpp"
#include "dcp_state.hpp"
#include "market_data_feed.hpp"
#include "private_session.hpp"
#include "strategy.hpp"
#include "trading_helper.hpp"

namespace
{
constexpr auto kFreshnessLimit = std::chrono::seconds{5};
constexpr auto kStartupTimeout = std::chrono::seconds{10};
volatile std::sig_atomic_t stop_requested = 0;

void request_stop(int)
{
    stop_requested = 1;
}

std::unique_ptr<IStrategy> make_strategy(const AppConfig &config,
                                         const InstrumentMeta &instrument,
                                         const FeeRates &fees)
{
    if (config.side_mode == "long_only")
        return std::make_unique<LongOnlyMarketMakerStrategy>(
            config.symbol, instrument, config.budget_usd, config.min_spread_bps,
            config.spread_factor, 1, 2, config.max_net_qty, config.tp_safety_bps,
            config.ladder_levels, config.stop_loss_bps, config.gross_notional_cap, fees);
    return std::make_unique<ExampleMarketMakerStrategy>(
        config.symbol, instrument, config.budget_usd, config.min_spread_bps,
        config.spread_factor, 1, 2, config.max_net_qty, config.tp_safety_bps,
        config.ladder_levels, config.stop_loss_bps, config.gross_notional_cap, fees);
}

bool enable_optional_dcp(TradingHelper &helper)
{
    try
    {
        helper.enable_disconnect_cancel_all(10);
        if (!derivatives_dcp_enabled(helper.fetch_disconnect_cancel_all()))
        {
            std::cerr << "Warning: disconnect-cancel-all is not ON for DERIVATIVES\n";
            return false;
        }
        return true;
    }
    catch (const std::exception &error)
    {
        std::cerr << "Warning: disconnect-cancel-all unavailable: " << error.what() << '\n';
        return false;
    }
}

MarketDataSnapshot current_snapshot(MarketDataFeed &feed, const std::string &symbol)
{
    const auto ticker = feed.latest_ticker(symbol);
    const auto orderbook = feed.latest_orderbook(symbol);
    if (!ticker || !orderbook)
        throw std::runtime_error("fresh market snapshot unavailable for " + symbol);
    return {symbol, *ticker, *orderbook};
}

void require_live_connections(const AppConfig &config,
                              const MarketDataFeed &feed,
                              const PrivateSession &session,
                              const PrivateState &state)
{
    if (!feed.ready(config.symbol, kFreshnessLimit))
        throw std::runtime_error("public market stream disconnected or stale");
    if (!session.ready())
    {
        const auto detail = state.error();
        throw std::runtime_error(detail.empty() ? "private stream disconnected or not ready" : detail);
    }
}

void guarded_rebootstrap(TradingHelper &helper,
                         PrivateState &state,
                         IStrategy &strategy,
                         const std::string &symbol)
{
    constexpr int kMaxAttempts = 3;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt)
    {
        const auto revisions = state.revisions();
        strategy.cancel_working(helper);
        const auto position = parse_hedge_positions(helper.fetch_positions(symbol), symbol);
        const auto orders = parse_open_orders(helper.fetch_open_orders(symbol), symbol);
        if (state.seed_if_unchanged(position, orders, revisions))
        {
            strategy.sync_open_orders(state.order_snapshot());
            return;
        }
    }
    throw std::runtime_error("private account state changed during bounded REST re-bootstrap");
}
} // namespace

int run_market_maker(const AppConfig &config)
{
    stop_requested = 0;
    std::signal(SIGINT, request_stop);
    std::signal(SIGTERM, request_stop);

    TradingHelper helper(config.api_key, config.api_secret, "linear", config.base_url);
    const auto instrument = parse_instrument_meta(helper.fetch_instrument_info(config.symbol), config.symbol);

    FeeRates fees;
    PositionView position;
    std::vector<OpenOrderView> open_orders;
    if (config.run_live)
    {
        helper.configure_hedge_mode(config.symbol);
        fees = parse_fee_rates(helper.fetch_fee_rate(config.symbol), config.symbol);
        position = parse_hedge_positions(helper.fetch_positions(config.symbol), config.symbol);
        open_orders = parse_open_orders(helper.fetch_open_orders(config.symbol), config.symbol);
    }

    auto strategy = make_strategy(config, instrument, fees);
    PrivateState private_state(config.symbol, "linear", strategy->owned_order_prefix());
    std::unique_ptr<PrivateSession> private_session;
    if (config.run_live)
    {
        private_state.seed(position);
        private_state.seed_orders(open_orders);
        strategy->sync_open_orders(private_state.order_snapshot());
        strategy->cancel_working(helper);
        private_state.seed_orders(parse_open_orders(helper.fetch_open_orders(config.symbol), config.symbol));
        strategy->sync_open_orders(private_state.order_snapshot());
        private_session = std::make_unique<PrivateSession>(
            config.private_ws_url, config.api_key, config.api_secret, "linear",
            private_state, enable_optional_dcp(helper));
        private_session->start();
    }

    MarketDataFeed feed(config.public_ws_url);
    int result = 0;
    try
    {
        feed.start({config.symbol}, 1);
        if (!feed.wait_for_initial(std::chrono::duration_cast<std::chrono::milliseconds>(kStartupTimeout)))
            throw std::runtime_error("timed out waiting for fresh public market data");
        if (config.run_live &&
            !private_session->wait_until_ready(std::chrono::duration_cast<std::chrono::milliseconds>(kStartupTimeout)))
            throw std::runtime_error("timed out waiting for authenticated private subscriptions");
        if (config.run_live)
            guarded_rebootstrap(helper, private_state, *strategy, config.symbol);

        auto generation = feed.generation();
        while (!stop_requested)
        {
            if (config.run_live)
            {
                require_live_connections(config, feed, *private_session, private_state);
                strategy->sync_open_orders(private_state.order_snapshot());
                position = private_state.position();
            }
            strategy->on_snapshot(current_snapshot(feed, config.symbol), helper,
                                  config.run_live, position);

            feed.wait_for_update(generation, std::chrono::seconds{1});
            generation = feed.generation();
        }
    }
    catch (const std::exception &error)
    {
        std::cerr << "Runtime stopped: " << error.what() << '\n';
        result = 1;
    }

    if (config.run_live)
    {
        try
        {
            strategy->cancel_working(helper);
        }
        catch (const std::exception &error)
        {
            std::cerr << "Owned-order cleanup failed: " << error.what() << '\n';
            result = 1;
        }
        private_session->close();
    }
    feed.stop();
    return result;
}
