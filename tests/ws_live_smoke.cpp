#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "ws_helper.hpp"

namespace
{
    std::string get_env(const char *name, const std::string &fallback = "")
    {
        const char *v = std::getenv(name);
        return v ? std::string{v} : fallback;
    }
}

TEST_CASE("websocket_public_ticker_stream", "[smoke][network][ws]")
{
    const std::string symbol = get_env("BYBIT_SYMBOL", "BTCUSDT");
    // Bybit public linear WS endpoint per docs.
    const std::string url = get_env("BYBIT_WS_PUBLIC_URL", "wss://stream.bybit.com/v5/public/linear");

    WsHelper ws(url);

    std::mutex m;
    std::condition_variable cv;
    std::atomic<bool> got_msg{false};

    ws.connect([&](const std::string &msg)
               {
                   try
                   {
                       const auto response = nlohmann::json::parse(msg);
                       if (response.value("topic", "") != "tickers." + symbol ||
                           !response.contains("data") ||
                           response["data"].value("symbol", "") != symbol)
                           return;
                       {
                           std::lock_guard<std::mutex> lock(m);
                           got_msg = true;
                       }
                       cv.notify_one();
                   }
                   catch (...)
                   {
                   }
               });

    ws.subscribe_tickers({symbol});

    std::unique_lock<std::mutex> lk(m);
    const auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    cv.wait_until(lk, timeout, [&]
                  { return got_msg.load(); });

    ws.close();

    REQUIRE(got_msg.load());
}
