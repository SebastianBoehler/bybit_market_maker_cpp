# Bybit Market Maker C++

[![Build and test](https://github.com/SebastianBoehler/bybit_market_maker_cpp/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/SebastianBoehler/bybit_market_maker_cpp/actions/workflows/build.yml)
[![Version](https://img.shields.io/github/v/tag/SebastianBoehler/bybit_market_maker_cpp?label=version&sort=semver)](https://github.com/SebastianBoehler/bybit_market_maker_cpp/tags)
![C++](https://img.shields.io/badge/C%2B%2B-17-00599c?logo=cplusplus&logoColor=white)
![CMake](https://img.shields.io/badge/CMake-3.20%2B-064f8c?logo=cmake&logoColor=white)
![Bybit API](https://img.shields.io/badge/Bybit_API-V5-f7a600)
[![License: MIT](https://img.shields.io/badge/license-MIT-yellow.svg)](LICENSE)

A compact C++17 reference market maker for Bybit linear perpetuals. It maintains passive quote ladders, fee-aware reduce-only take profits, optional mark-price stops, and hard net and gross exposure limits. Live mode fails closed when market data, account truth, or order state becomes uncertain.

> [!WARNING]
> This is experimental open-source software for engineering and educational use. It is not investment advice, has not been independently audited, and does not promise profitability. Live trading can lose money. Review the code, start in dry-run mode, and use credentials with the minimum required permissions.

## Highlights

- Event-driven decisions from sequence-checked Bybit V5 order book and ticker streams.
- Exact-symbol REST bootstrap for instruments, fees, hedge positions, and active orders.
- Passive price rounding, fee-aware exits, inventory skew, and bounded quote mutation.
- Hard base-quantity and quote-notional caps that include positions, external orders, current quotes, and intended orders.
- Mark-price stop logic with a restart-lifetime kill latch and reduce-only market exits.
- Session-owned order reconciliation, legacy-order migration cleanup, bounded REST resync, and verified cancellation.
- Optional Bybit disconnect-cancel-all subscription only when the account reports derivative DCP as enabled.

## Requirements

- CMake 3.20 or newer
- A C++17 compiler
- OpenSSL, libcurl, and zlib development packages
- A Bybit account and API credentials only for live mode

On Debian or Ubuntu:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config libssl-dev libcurl4-openssl-dev zlib1g-dev
```

## Quick start

```bash
git clone https://github.com/SebastianBoehler/bybit_market_maker_cpp.git
cd bybit_market_maker_cpp
cp .env.example .env

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBYBIT_MM_NETWORK_TESTS=OFF
cmake --build build --parallel
ctest --test-dir build --build-config Release --output-on-failure

./build/market_maker_example
```

Dry-run mode is the default. It connects to public Bybit endpoints and computes plans, but it does not submit or cancel orders. A command-line symbol overrides `BYBIT_SYMBOL`:

```bash
./build/market_maker_example BTCUSDT
./build/market_maker_long_only SUIUSDT
```

## Configuration

Copy `.env.example` to `.env`. The application loads `.env` without overwriting variables already present in the process environment.

| Setting | Runtime default | Unit and meaning |
| --- | ---: | --- |
| `BYBIT_SYMBOL` | `SUIUSDT` | Exact linear-perpetual symbol; the first CLI argument overrides it |
| `BYBIT_RUN_LIVE` | `0` | `0` plans only; `1` enables order mutations |
| `BYBIT_SIDE_MODE` | `both` | `both` or `long_only` |
| `BYBIT_BUDGET_USD` | `10.0` | Total quote-notional budget divided across opening ladder legs |
| `BYBIT_MIN_SPREAD_BPS` | `0.2` | Minimum full spread in basis points; 1 bp = 0.01% |
| `BYBIT_SPREAD_FACTOR` | `1.0` | Multiplier applied to the live spread; quotes never move inside the BBO |
| `BYBIT_LADDER_LEVELS` | `3` | Levels per enabled side; live mode allows at most 9 |
| `BYBIT_MAX_NET_QTY` | `100.0` | Maximum net contract/base-asset quantity for linear perpetuals |
| `BYBIT_GROSS_NOTIONAL_CAP` | `-1` | Quote-notional cap; a positive value is mandatory in live mode |
| `BYBIT_TP_SPREAD_BPS` | `0.5` | Safety buffer beyond fee-aware break-even, in basis points |
| `BYBIT_STOP_LOSS_BPS` | `-1` | Mark-price distance from entry; negative disables it, enabled values must be below 10,000 |
| `BYBIT_API_KEY` | empty | Required in live mode |
| `BYBIT_API_SECRET` | empty | Required in live mode |
| `BYBIT_BASE_URL` | `https://api.bybit.com` | REST endpoint; an explicit empty value is rejected |
| `BYBIT_WS_PUBLIC_URL` | Bybit linear public V5 URL | Required in every mode |
| `BYBIT_WS_PRIVATE_URL` | Bybit private V5 URL | Required in live mode |

To enable live mutations, set `BYBIT_RUN_LIVE=1`, provide both credentials, and choose a positive `BYBIT_GROSS_NOTIONAL_CAP`. The application also requires hedge mode and configures it for the exact symbol before trading.

## Safety model

Live startup establishes a complete REST snapshot, removes stale session and recognized legacy bot orders, and then starts authenticated private streams. A guarded second REST bootstrap prevents websocket events that arrive during startup from being overwritten.

The runtime quotes only while all required state is healthy:

- public order book state is connected, sequence-valid, and fresh;
- mark price is valid and uses a stricter freshness window than the book;
- private authentication and execution, position, and order subscriptions are ready;
- both hedge legs and the complete active-order set are known;
- any uncertain mutation is reconciled through bounded REST reads.

A stop breach suppresses quotes, cancels owned orders, and attempts reduce-only exits for every non-dust hedge leg. The kill state remains latched until restart. Disconnect-cancel-all is defense in depth only; local cancellation and confirmation remain authoritative.

## Testing

The default CTest suite is deterministic and makes no exchange network calls:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBYBIT_MM_NETWORK_TESTS=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Live REST and websocket smoke tests are built but not registered with CTest by default. Register them only when you intentionally want network access:

```bash
cmake -S . -B build-network -DBYBIT_MM_NETWORK_TESTS=ON
cmake --build build-network --parallel
ctest --test-dir build-network --output-on-failure
```

Network smoke tests depend on current Bybit availability and may require credentials. Passing unit tests does not prove exchange connectivity, account permissions, strategy profitability, or production readiness.

## Architecture

| Area | Main files | Responsibility |
| --- | --- | --- |
| Configuration | `app_config.*`, `main.cpp` | Environment loading, validation, and process entry |
| Public market data | `market_data_feed.*`, `market_state.*` | Websocket lifecycle, topic integrity, sequence merging, and freshness |
| Private account data | `private_session.*`, `private_state.*`, `private_position.cpp` | Authentication, subscriptions, atomic position/order reduction, and PnL inputs |
| Strategy | `strategy_plan.*`, `strategy_runtime.*` | Pure order planning, risk exits, kill latch, and mutation cadence |
| Orders | `order_reconciler.*`, `order_batch.*`, `account_state.*` | Ownership classification, delta construction, batching, and REST parsing |
| Exchange adapter | `trading_helper.*`, `bybit_response.*` | Thin Bybit V5 REST calls and strict response validation |

The pinned [`bybit-cpp-client`](https://github.com/SebastianBoehler/bybit-cpp-client) dependency provides the REST and websocket transport layer. This repository owns market-making policy and account-state safety.

## Limitations

- Linear perpetuals and hedge-mode position indices are the supported trading model.
- One process manages one configured symbol and does not persist strategy state across restarts.
- Websocket disconnects require a clean restart; automatic reconnect is intentionally disabled.
- DCP availability depends on the account and is not assumed when the capability response is empty or off.
- The strategy is a reference implementation, not a latency-optimized colocated engine, backtester, or audited risk system.

The exchange interface follows the [Bybit V5 API documentation](https://bybit-exchange.github.io/docs/v5/guide).

## Versioning and license

The current project version is **0.1.0**. Until 1.0.0, behavior and public interfaces may change between minor releases. Release tags use the `vMAJOR.MINOR.PATCH` form.

Licensed under the [MIT License](LICENSE).
