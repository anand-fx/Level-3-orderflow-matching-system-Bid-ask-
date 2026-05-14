# L3 Order Flow Visualizer

A real-time **Level 3 order book visualizer** built in C++17. Reads raw L3 market data from a CSV feed and renders a live Depth of Market (DOM) terminal display — the same view professional traders use to read order flow.

---

## What is L3 Data?

| Level | What you see |
|---|---|
| L1 | Best bid + best ask only |
| L2 | Price + total size per level |
| **L3** | **Every individual order ID at every price level** |

L3 is the most granular market data available. It lets you see not just *how much* liquidity exists at a price, but *whose* orders are there and *how they behave over time* — the foundation of order flow analysis and spoofing detection.

---

## Features

- **Live DOM** — Depth of Market with 10 price levels per side (configurable)
- **Time & Sales tape** — every execution printed in real time with direction
- **Order imbalance bar** — visual bid vs ask pressure across top 5 levels
- **Cumulative delta** — running buy volume minus sell volume
- **Full L3 event processing** — Add, Cancel, Modify, Trade, Fill, Reset
- **Auto-detects delimiter** — tab or comma separated CSV
- **Configurable** — book depth, refresh rate, tick size all adjustable in constants

---

## Display Layout

```
 DOM  Last: 25301.00 [+]  Spread: 0.25  Mid: 25301.13
 Vol:45231  Buy:28441  Sell:16790  Delta:+11651

 TIME       SIZE    PRICE  | ORD [BID DEPTH  ]  BID SZ   PRICE   ASK SZ  [ASK DEPTH] ORD
                           |                             25303.00    500  [######    ]  3
 13:29:44    50  25301.00 ^|                             25302.75    200  [###       ]  1
 13:29:44   200  25300.50 v|                             25302.50    800  [##########]  5
 13:29:43   100  25301.00 v|                             25302.25    150  [##        ]  1
 13:29:42    50  25301.25 ^|                             25302.00    300  [####      ]  2
 13:29:41   300  25300.75 v| ──── SPREAD 0.25  MID 25301.13 ────
 13:29:41    75  25301.00 ^|  2  [##        ]   300  25301.00
 13:29:40   500  25300.50 v|  5  [##########] 1000  25300.75
 13:29:40   150  25301.00 v|  1  [##        ]   200  25300.50
                           |  3  [######    ]   600  25300.25
                           |  2  [###       ]   400  25300.00
                           |
                           |  IMB[------#####|--------]  D:+11651  imb:+0.234
```

**Left panel** — Time & Sales: `^` = buyer aggressor, `v` = seller aggressor

**Right panel** — DOM: asks in red above spread, bids in green below

---

## CSV Format

Tab or comma separated. Auto-detected.

```
timestamp                          action  side  price    size  order_id      source
2026-04-05T13:29:43.776471934+0000 Add     Buy   25300.75 5     687509000001  TradingFirm_A
2026-04-05T13:29:43.776891234+0000 Add     Sell  25301.00 3     687509000002  TradingFirm_B
2026-04-05T13:29:43.812340000+0000 Trade   Buy   25301.00 2     0             
2026-04-05T13:29:43.812340000+0000 Fill    Buy   25301.00 2     687509000002  
2026-04-05T13:29:44.001230000+0000 Cancel  Buy   25300.75 5     687509000001  
```

| Column | Description |
|---|---|
| `timestamp` | ISO 8601 with nanoseconds |
| `action` | `Add` `Cancel` `Modify` `Trade` `Fill` `Reset` |
| `side` | `Buy` or `Sell` |
| `price` | Limit price |
| `size` | Number of contracts |
| `order_id` | Unique order identifier (supports scientific notation) |
| `source` | Optional — market participant identifier |

---

## Build & Run

**Requirements:** C++17 compiler

```bash
# Linux / macOS
g++ -std=c++17 -O2 -o market L3OF_VIZ.cpp
./market

# Windows (MinGW)
g++ -std=c++17 -O2 -o market.exe L3OF_VIZ.cpp
market.exe
```

**Code::Blocks:**
1. File → New → Project → Console Application → C++
2. Paste code into `main.cpp`
3. Ctrl+F9 to build → Ctrl+F10 to run

---

## Configuration

All tuning constants are at the top of the file in **Section 2**:

```cpp
static constexpr int BOOK_DEPTH = 10;    // price levels shown per side
static constexpr int REFRESH_MS = 80;    // redraw interval in milliseconds
static constexpr int TAPE_ROWS  = BOOK_DEPTH * 2 + 1;  // tape height
```

Set your CSV path in `main()`:
```cpp
const std::string FALLBACK_PATH =
    "D:\\your\\path\\to\\data.csv";
```

---

## How the L3 Engine Works

Every message from the feed routes through a single `process()` function:

```
FeedMessage
     │
     ├── ADD    → inserts order into bids or asks map + order_map (O(log n))
     ├── CANCEL → removes order from level, erases level if empty
     ├── MODIFY → adjusts size in place; if price changed, moves to new level
     ├── TRADE  → records execution, updates cumulative delta
     ├── FILL   → reduces resting order size, removes if fully filled
     └── RESET  → clears entire book for feed restart
```

The book uses two sorted maps:
- `bids` — `std::map<double, PriceLevel, std::greater<double>>` high→low
- `asks` — `std::map<double, PriceLevel>` low→high

Each `PriceLevel` stores the vector of individual `order_id`s — this is what makes it L3 rather than L2.

---

## Metrics Explained

| Metric | Formula | Interpretation |
|---|---|---|
| **Spread** | best ask − best bid | Market liquidity — tighter = more liquid |
| **Mid** | (best bid + best ask) / 2 | Fair value reference price |
| **Delta** | buy volume − sell volume | Positive = buyers aggressive |
| **Imbalance** | (bid size − ask size) / (bid size + ask size) | Range −1 to +1, +1 = all liquidity on bid |

---

## Roadmap

- [ ] Spoof detection engine — flag large orders cancelled with zero fills
- [ ] Live socket feed — replace CSV with real-time WebSocket/TCP input
- [ ] Volume profile — track volume traded at each price level
- [ ] Footprint chart — bid vs ask volume per candle
- [ ] Alert system — configurable triggers for unusual order behaviour

---

## Requirements

- C++17 or later
- Terminal with ANSI colour support
- Terminal height of 35+ rows (50+ recommended for BOOK_DEPTH > 10)
- UTF-8 encoding

---

## License

MIT License — free to use, modify, and distribute.

---

## Author
Anand pandya
Built from scratch as a learning project in market microstructure and L3 order flow analysis.
