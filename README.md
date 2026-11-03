# Portfolio Risk Engine (C++)

A C++20 historical portfolio-risk calculator for sector ETFs. It estimates annualized volatility, validates portfolio variance against a covariance-matrix implementation, and computes historical 95% Value at Risk (VaR) and Expected Shortfall (ES) versus SPY.

## Why this project

The objective is to build an auditable, configurable numerical risk tool—not to claim a predictive trading strategy. The engine uses a previously prepared market-data export from [Market Risk Analytics](https://github.com/JReader1/Market-Risk-Analytics), but computes the statistics independently in C++.

## Features

- Configurable long-only sector-ETF allocations using command-line arguments
- Automatic **complete-case** trading-date alignment across selected ETFs
- Sample covariance and annualized volatility (252 trading days)
- Two independent computations of portfolio volatility, checked for agreement
- Empirical 95% historical VaR (nearest-rank loss quantile)
- Historical ES (mean of the worst `ceil(0.05 * N)` daily losses)
- SPY benchmark calculations using the **same aligned dates**
- Validation for malformed data, duplicate selected ticker/date pairs, inconsistent benchmark values, and invalid weights

## Build and test

Requires CMake 3.20+ and a C++20 compiler.

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

In CLion, open this repository as a CMake project. Configure the run **working directory** to the repository root, so the default relative data path can be resolved.

## Market data

Place `sector_market_daily.csv` in `data/` (not tracked by Git). It must contain the following column headers:

```text
trade_date,ticker,sector,sector_return,market_return
```

Additional columns are permitted. `sector_return` and `market_return` are decimal **daily returns** (for example, `0.01` means 1%), not percentage-point values. The loader expects ordinary unquoted CSV fields, and the default dataset originates from a SQL/Python market-data pipeline; this repository deliberately excludes the raw data file.

## Usage

With no arguments, the default allocation is **40% XLK, 30% XLE, 30% XLP**.

```bash
./build/Portfolio_Risk_Engine
```

Provide any selection of sector ETF tickers available in the CSV, with nonnegative weights summing to 1:

```bash
./build/Portfolio_Risk_Engine XLV=0.25 XLU=0.25 XLF=0.25 XLI=0.25
```

In CLion, enter `XLV=0.25 XLU=0.25 XLF=0.25 XLI=0.25` into **Run → Edit Configurations → Program arguments**. The data path is currently fixed at `data/sector_market_daily.csv` relative to the working directory.

## Historical benchmark (default portfolio)

On the original 2019–2025 market dataset (1,759 aligned trading days), the previous C++ implementation produced:

| Metric | Portfolio | SPY |
| --- | ---: | ---: |
| Annualized volatility | 20.4728% | 19.7814% |
| Historical daily VaR, 95% | 1.6996% | 1.7686% |
| Historical daily ES, 95% | 3.0946% | 2.9974% |

The updated configurable implementation **still needs to be run on the full local CSV** to confirm this regression benchmark; do not interpret these as newly verified measurements.

## Interpretation and limitations

These are **historical descriptive risk estimates**, not forward forecasts or investment recommendations. VaR and ES depend on the sample period, chosen confidence level, and quantile convention. Market data include crisis periods; cross-portfolio comparisons should use the same aligned date range. The current implementation assumes **constant daily rebalanced weights** and ignores transaction costs, slippage, taxes, and rebalancing feasibility. All returns are assumed to be decimal daily simple returns. Missing selected-asset dates are excluded, which can change the historical comparison window.

## Next steps

1. Independently validate against Python using the full dataset.
2. Add maximum drawdown and historical stress-scenario attribution.
3. Extend automated tests to malformed inputs and data alignment.
4. Benchmark Release builds and document performance.

## License

No license has been selected yet.
