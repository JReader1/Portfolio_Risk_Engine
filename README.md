# Portfolio Risk Engine

**C++20 | CMake | CTest | Python reference validation**

A configurable, reproducible historical risk calculator for portfolios of sector ETFs. It analyzes annualized volatility, sample covariance, historical Value at Risk (VaR), Expected Shortfall (ES), maximum drawdown and named stress windows relative to SPY. Designed as a quantitative research and systems-programming demonstration; it does not predict returns or place trades.

## Features

- **Configurable portfolios:** `TICKER=WEIGHT` CLI inputs. Default is XLK 40%, XLE 30%, XLP 30%. Long-only weights must sum to 1.0; benchmark SPY cannot be a holding.
- **Robust input handling:** Header-based CSV parsing, checked numeric values, duplicate ticker/date detection, consistent SPY daily returns, date validation, and complete-case date alignment.
- **Numerical methods:** Annualized volatility using `sqrt(252)`, sample covariance using `N-1`, and independent covariance-based volatility `sqrt(252 wᵀΣw)`, validated against volatility of daily portfolio returns.
- **Tail risk:** 95% historical VaR (nearest-rank empirical loss quantile) and ES (mean of the worst `ceil((1-confidence)*N)` losses). Confidence can be changed using `--confidence`.
- **Drawdown and stress:** Maximum compounded peak-to-trough drawdown, worst daily return, and compounded returns for COVID selloff (2020-02-19 to 2020-04-30) and calendar 2022; periods outside the input dataset are omitted.
- **Performance benchmark:** `--bench N` times repeated in-memory calculations, excluding file reading, output formatting and startup overhead. Use Release mode for meaningful timings; results depend on hardware and compiler.
- **Testing:** C++ unit tests, CLI integration tests and optional independent Python standard-library numerical cross-check on a committed synthetic fixture. GitHub Actions builds/tests on Windows and Linux.

## Build

Requires **CMake 3.20+** and a C++20 compiler (GCC, Clang or MSVC). Python 3 is optional for the independent reference test.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Windows (CLion): open the folder as a CMake project. In **Run → Edit Configurations**, set the working directory to the project root so the default data path resolves. Alternatively use an absolute path with `--data`.

## Input data

Export `sector_market_daily.csv` from [Market-Risk-Analytics](https://github.com/JReader1/Market-Risk-Analytics) and place it at `data/sector_market_daily.csv`. The large original dataset is deliberately not committed. A **synthetic** example is committed as `tests/fixtures/market.csv` to make the project testable without downloading proprietary or live market data.

Required CSV headers (column order may vary; extra columns allowed):

| Column | Meaning |
| --- | --- |
| `trade_date` (or `date`) | ISO `YYYY-MM-DD` date |
| `ticker` | ETF ticker (e.g., `XLK`) |
| `sector_return` | Decimal daily simple return for the ETF, e.g. `-0.02` means -2% |
| `market_return` | Decimal daily SPY simple return, the same for every ticker on each date |

The loader expects straightforward unquoted CSV fields (quoted commas are rejected). Only selected ticker observations participate in the calculation. Days on which any selected ticker is absent are **excluded for both portfolio and benchmark**, making their risk statistics comparable across the same set of dates. The engine requires at least two complete dates.

## Run

```bash
# Default allocation (requires local data/sector_market_daily.csv)
./build/Portfolio_Risk_Engine

# Choose an arbitrary long-only allocation
./build/Portfolio_Risk_Engine XLV=0.25 XLU=0.25 XLF=0.25 XLI=0.25

# Select file and quantile confidence, benchmark in-memory calculations
./build/Portfolio_Risk_Engine --data tests/fixtures/market.csv --confidence 0.99 --bench 1000 XLK=0.5 XLE=0.5

# Command-line help
./build/Portfolio_Risk_Engine --help
```

On Windows, execute `build/Release/Portfolio_Risk_Engine.exe` if using a multi-configuration generator or CLion's configured executable.

## Regression benchmarks from the prior implementation

On the original 2019–2025 dataset (**1,759 aligned trading days**) using the default portfolio, the previous local C++ implementation produced:

| Metric | Portfolio | SPY |
| --- | ---: | ---: |
| Annualized volatility | 20.4728% | 19.7814% |
| 95% daily VaR | 1.6996% | 1.7686% |
| 95% daily ES | 3.0946% | 2.9974% |

**These regression values remain to be checked against the full local dataset with the current release.** The fixture integration and independent reference tests are not a substitute for that verification.

## Statistical assumptions

All portfolio returns use **constant daily rebalanced weights**, not buy-and-hold drifting weights. Calculations use unadjusted-for-trading-cost daily returns as supplied by the upstream project. Maximum drawdown is reported as **positive loss magnitude** rather than negative peak-to-trough return. Historical VaR can be negative if the empirical confidence quantile represents a gain; the tool deliberately does not floor it at zero. Historical ES uses a fixed count of worst observations, which may differ from interpolated quantile conventions. Annualized arithmetic mean (`mean(daily returns) × 252`) is **not** the CAGR and should not be interpreted as realized annual investment growth. Stress windows use compounded daily returns over trading days present in the data; windows are descriptive, not calibrated regulatory stress tests. Independent calculation agreement establishes internal consistency for tested examples, not future predictive accuracy.

## Project layout

```text
main.cpp                  CLI, formatted reports, timing
src/risk.hpp              Public interfaces and data structures
src/risk.cpp              Market loading and quantitative calculations
tests/risk_tests.cpp      Numerical and input-validation tests
tests/fixtures/market.csv Synthetic integration fixture
tests/reference_check.py  Independent Python numerical cross-check
.github/workflows/ci.yml Windows and Linux CI
CMakeLists.txt            Compiler and CTest configuration
```

## Verification checklist

1. Run `ctest --test-dir build -C Release --output-on-failure`: all 5 tests should pass when Python 3 is installed (4 without Python).
2. Run the default allocation on your original full CSV. Expect 1,759 days and the three benchmark figures above to 4 decimals.
3. Run a different sector allocation. Check that `Portfolio volatility` and `SPY` are calculated on the same aligned trading dates.
4. Compare Release benchmark times on the **same device and dataset**; Debug vs Release is not an apples-to-apples comparison.

## Status

Implemented and tested against synthetic examples locally; original market data remains user-local. No live trading, forward forecasting, Monte Carlo simulation, parameter optimization or investment recommendations are claimed.
