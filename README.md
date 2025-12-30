# Bybit Market Maker (C++)

Lightweight market-making example for Bybit linear perpetuals. Runs indefinitely, quotes laddered orders with inventory skew, take-profit, optional stop-loss, and gross exposure guard, while tracking PnL (realized/unrealized, fees, funding) via private websockets.

## Features

- Laddered quotes (configurable levels) with min-spread floor and spread factor.
- Inventory skew and max net cap to avoid over-exposure.
- Take-profit orders across the spread.
- Optional stop-loss (bps from entry) and gross notional cap to pause quoting.
- Funding and fee-aware PnL tracker (private execution/position streams).
- Drift guard cancels stale orders if mid moves multiple ticks.
- Colorized logs for POS/EXE/PNL (green/red for signed numbers).

## Prerequisites

- CMake + C++17 toolchain.
- Bybit API key/secret (keep private).

## Setup

1. Copy `.env.example` to `.env` and fill keys:

```
cp .env.example .env
```

2. Edit `.env`:

- `BYBIT_API_KEY`, `BYBIT_API_SECRET`
- `BYBIT_SYMBOL` (default `SUIUSDT` for low notional)
- Risk/behavior: `BYBIT_MIN_SPREAD_BPS`, `BYBIT_LADDER_LEVELS`, `BYBIT_MAX_NET_QTY`,
  `BYBIT_TP_SPREAD_BPS`, `BYBIT_STOP_LOSS_BPS`, `BYBIT_GROSS_NOTIONAL_CAP`
- Set `BYBIT_RUN_LIVE=1` to trade, or `0` for dry-run.

## Build

```
cmake -S . -B build
cmake --build build -j4
```

## Run

```
./build/market_maker_example          # uses .env and defaults
./build/market_maker_example SUIUSDT  # override symbol
```

## Notes

- Stop-loss is opt-in via `BYBIT_STOP_LOSS_BPS` (set positive bps, e.g., 50 = 0.5%).
- Gross exposure guard (`BYBIT_GROSS_NOTIONAL_CAP`) pauses new quotes when (long+short)\*mid exceeds cap; TP/SL still run.
- Keep API secrets out of git; `.env` is local-only.
