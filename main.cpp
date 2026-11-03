
#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// ============================================================
// PORTFOLIO RISK ENGINE
// Historical market risk analysis using C++
// Portfolio: 40% XLK, 30% XLE, 30% XLP
// Benchmark: SPY
// ============================================================

constexpr double TRADING_DAYS = 252.0;
constexpr double CONFIDENCE_LEVEL = 0.95;

struct Day {
    std::map<std::string, double> returns;
    double benchmark = 0.0;
    bool hasBenchmark = false;
};

struct Portfolio {
    std::vector<std::string> tickers;
    std::vector<double> weights;
};

struct MarketData {
    std::map<std::string, Day> days;
    std::size_t rawDates = 0;
};

constexpr double TOLERANCE = 1e-8;

Portfolio parsePortfolio(int argc, char* argv[]) {
    Portfolio result;
    if (argc == 1) {
        result.tickers = {"XLK", "XLE", "XLP"};
        result.weights = {0.40, 0.30, 0.30};
        return result;
    }

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        const auto equals = argument.find('=');
        if (equals == std::string::npos || equals == 0 || equals == argument.size() - 1) {
            throw std::invalid_argument(
                "Expected TICKER=WEIGHT (example XLK=0.40); arguments: " + argument
            );
        }
        const std::string ticker = argument.substr(0, equals);
        if (ticker == "SPY") {
            throw std::invalid_argument("SPY is reserved as the benchmark");
        }
        if (std::find(result.tickers.begin(), result.tickers.end(), ticker) != result.tickers.end()) {
            throw std::invalid_argument("Duplicate portfolio ticker: " + ticker);
        }
        std::size_t consumed = 0;
        double weight = 0.0;
        try {
            weight = std::stod(argument.substr(equals + 1), &consumed);
        } catch (...) {
            throw std::invalid_argument("Invalid weight in: " + argument);
        }
        if (consumed != argument.size() - equals - 1 ||
            !std::isfinite(weight) || weight < 0.0) {
            throw std::invalid_argument("Invalid nonnegative weight in: " + argument);
        }
        result.tickers.push_back(ticker);
        result.weights.push_back(weight);
    }
    double total = 0.0;
    for (double weight : result.weights) total += weight;
    if (std::abs(total - 1.0) > 1e-10) {
        throw std::invalid_argument("Portfolio weights must sum to 1.0");
    }
    return result;
}

// Read a simple (unquoted) CSV row. The exported market dataset contains
// no quoted commas. Reject unexpected quotes rather than silently misparse.
std::vector<std::string> splitCSV(const std::string& line) {
    if (line.find('"') != std::string::npos)
        throw std::runtime_error("Quoted CSV fields are unsupported");
    std::stringstream stream(line);
    std::vector<std::string> fields;
    std::string field;
    while (std::getline(stream, field, ',')) fields.push_back(field);
    if (!line.empty() && line.back() == ',') fields.emplace_back();
    return fields;
}

double parseNumber(const std::string& field, std::size_t row) {
    try {
        std::size_t consumed = 0;
        const double value = std::stod(field, &consumed);
        if (consumed != field.size() || !std::isfinite(value))
            throw std::invalid_argument("Invalid number");
        return value;
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid numeric return at CSV line " + std::to_string(row));
    }
}

double calculateMean(const std::vector<double>& values) {
    if (values.empty()) {
        throw std::invalid_argument("Cannot calculate mean of empty data");
    }

    double sum = 0.0;

    for (double value : values) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument("Non-finite value encountered");
        }
        sum += value;
    }

    return sum / static_cast<double>(values.size());
}

double sampleVariance(const std::vector<double>& values) {
    if (values.size() < 2) {
        throw std::invalid_argument("Variance requires at least two observations");
    }

    const double mean = calculateMean(values);
    double squaredDeviations = 0.0;

    for (double value : values) {
        const double difference = value - mean;
        squaredDeviations += difference * difference;
    }

    return squaredDeviations /
           static_cast<double>(values.size() - 1);
}

double annualizedVolatility(const std::vector<double>& returns) {
    return std::sqrt(sampleVariance(returns) * TRADING_DAYS) * 100.0;
}

double sampleCovariance(
    const std::vector<double>& x,
    const std::vector<double>& y
) {
    if (x.size() != y.size() || x.size() < 2) {
        throw std::invalid_argument(
            "Covariance requires equal-length samples of at least two observations"
        );
    }

    const double meanX = calculateMean(x);
    const double meanY = calculateMean(y);

    double sum = 0.0;

    for (std::size_t i = 0; i < x.size(); ++i) {
        sum += (x[i] - meanX) * (y[i] - meanY);
    }

    return sum / static_cast<double>(x.size() - 1);
}

// ============================================================
// HISTORICAL VALUE AT RISK
// ============================================================

// Empirical nearest-rank quantile of daily losses.
// Returns a percentage, not a decimal fraction.

double historicalVaR(
    const std::vector<double>& returns,
    double confidence = CONFIDENCE_LEVEL
) {
    if (returns.empty() ||
        !std::isfinite(confidence) ||
        confidence <= 0.0 ||
        confidence >= 1.0) {
        throw std::invalid_argument("Invalid VaR input");
    }

    std::vector<double> losses;
    losses.reserve(returns.size());

    for (double r : returns) {
        if (!std::isfinite(r)) {
            throw std::invalid_argument("Non-finite return in VaR");
        }

        losses.push_back(-r);
    }

    std::sort(losses.begin(), losses.end());

    const std::size_t index =
        static_cast<std::size_t>(
            std::ceil(confidence * losses.size())
        ) - 1;

    return losses[index] * 100.0;
}

// ============================================================
// HISTORICAL EXPECTED SHORTFALL
// ============================================================

// Average loss among the worst ceil((1-confidence)*N)
// observations. Uses the same convention as the earlier version.

double historicalES(
    const std::vector<double>& returns,
    double confidence = CONFIDENCE_LEVEL
) {
    if (returns.empty() ||
        !std::isfinite(confidence) ||
        confidence <= 0.0 ||
        confidence >= 1.0) {
        throw std::invalid_argument("Invalid ES input");
    }

    std::vector<double> losses;
    losses.reserve(returns.size());

    for (double r : returns) {
        if (!std::isfinite(r)) {
            throw std::invalid_argument("Non-finite return in ES");
        }

        losses.push_back(-r);
    }

    std::sort(
        losses.begin(),
        losses.end(),
        std::greater<double>()
    );

    const std::size_t tailCount =
        static_cast<std::size_t>(
            std::ceil((1.0 - confidence) * losses.size())
        );

    double tailSum = 0.0;

    for (std::size_t i = 0; i < tailCount; ++i) {
        tailSum += losses[i];
    }

    return (tailSum / static_cast<double>(tailCount)) * 100.0;
}

// ============================================================
// PORTFOLIO COVARIANCE CALCULATION
// ============================================================

double matrixPortfolioVolatility(
    const std::vector<std::vector<double>>& assets,
    const std::vector<double>& weights
) {
    if (assets.empty() || assets.size() != weights.size()) {
        throw std::invalid_argument("Asset and weight dimensions do not match");
    }

    const std::size_t observations = assets.front().size();

    if (observations < 2) {
        throw std::invalid_argument("Insufficient portfolio observations");
    }

    double totalWeight = 0.0;

    for (std::size_t i = 0; i < assets.size(); ++i) {
        if (assets[i].size() != observations) {
            throw std::invalid_argument("Asset return histories are not aligned");
        }

        if (!std::isfinite(weights[i])) {
            throw std::invalid_argument("Invalid portfolio weight");
        }

        totalWeight += weights[i];
    }

    if (std::abs(totalWeight - 1.0) > 1e-10) {
        throw std::invalid_argument("Portfolio weights must sum to 1");
    }

    double variance = 0.0;

    for (std::size_t i = 0; i < assets.size(); ++i) {
        for (std::size_t j = 0; j < assets.size(); ++j) {
            variance +=
                weights[i] *
                weights[j] *
                sampleCovariance(assets[i], assets[j]);
        }
    }

    if (variance < -1e-12) {
        throw std::runtime_error("Negative portfolio variance");
    }

    return std::sqrt(std::max(0.0, variance) * TRADING_DAYS) * 100.0;
}

// ============================================================
// MARKET DATA AND ALIGNED RETURN SERIES
// ============================================================

MarketData loadMarketData(const std::string& path, const Portfolio& portfolio) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot open input CSV: " + path);

    std::string line;
    if (!std::getline(file, line)) throw std::runtime_error("CSV is empty");

    const auto columns = splitCSV(line);
    // Match by header instead of relying on fixed positions.
    auto column = [&](const std::string& name) -> std::size_t {
        const auto it = std::find(columns.begin(), columns.end(), name);
        if (it == columns.end()) throw std::runtime_error("Missing CSV column: " + name);
        return static_cast<std::size_t>(it - columns.begin());
    };
    const auto dateCol = column("trade_date");
    const auto tickerCol = column("ticker");
    const auto sectorCol = column("sector_return");
    const auto spyCol = column("market_return");
    const auto required = std::max({dateCol, tickerCol, sectorCol, spyCol});

    MarketData data;
    std::size_t row = 1;
    while (std::getline(file, line)) {
        ++row;
        if (line.empty()) continue;
        const auto fields = splitCSV(line);
        if (fields.size() <= required)
            throw std::runtime_error("Malformed CSV line " + std::to_string(row));
        const std::string& date = fields[dateCol];
        const std::string& ticker = fields[tickerCol];
        if (date.empty()) throw std::runtime_error("Missing date at CSV line " + std::to_string(row));

        // Rows of non-selected tickers do not affect the portfolio.
        if (std::find(portfolio.tickers.begin(), portfolio.tickers.end(), ticker)
            == portfolio.tickers.end()) continue;

        const double value = parseNumber(fields[sectorCol], row);
        const double spy = parseNumber(fields[spyCol], row);
        Day& day = data.days[date];
        if (!day.returns.emplace(ticker, value).second)
            throw std::runtime_error("Duplicate " + ticker + " on " + date);
        if (day.hasBenchmark && std::abs(day.benchmark - spy) > 1e-10)
            throw std::runtime_error("Conflicting SPY return on " + date);
        day.benchmark = spy;
        day.hasBenchmark = true;
    }
    data.rawDates = data.days.size();
    if (data.days.empty()) throw std::runtime_error("No observations for requested tickers");
    return data;
}

int main(int argc, char* argv[]) {
    try {
        // Use CLion's project root as the working directory.
        const std::string dataPath = "data/sector_market_daily.csv";
        const Portfolio portfolio = parsePortfolio(argc, argv);
        const MarketData market = loadMarketData(dataPath, portfolio);

        std::vector<std::vector<double>> assetReturns(portfolio.tickers.size());
        std::vector<double> portfolioReturns;
        std::vector<double> benchmarkReturns;
        std::size_t skipped = 0;

        for (const auto& [date, day] : market.days) {
            if (day.returns.size() != portfolio.tickers.size()) {
                ++skipped;
                continue; // complete-case alignment on trading date
            }
            double value = 0.0;
            for (std::size_t i = 0; i < portfolio.tickers.size(); ++i) {
                const double r = day.returns.at(portfolio.tickers[i]);
                assetReturns[i].push_back(r);
                value += portfolio.weights[i] * r;
            }
            portfolioReturns.push_back(value);
            benchmarkReturns.push_back(day.benchmark);
        }
        if (portfolioReturns.size() < 2)
            throw std::runtime_error("Fewer than two aligned trading days");

        const double direct = annualizedVolatility(portfolioReturns);
        const double matrix = matrixPortfolioVolatility(assetReturns, portfolio.weights);
        const double gap = std::abs(direct - matrix);
        const double portfolioVaR = historicalVaR(portfolioReturns);
        const double portfolioES = historicalES(portfolioReturns);
        const double spyVaR = historicalVaR(benchmarkReturns);
        const double spyES = historicalES(benchmarkReturns);

        if (gap > TOLERANCE)
            throw std::runtime_error("Covariance and direct volatility disagree");
        if (portfolioES + 1e-10 < portfolioVaR || spyES + 1e-10 < spyVaR)
            throw std::runtime_error("Expected Shortfall is below VaR");

        std::cout << std::fixed << std::setprecision(4);
        std::cout << "PORTFOLIO RISK ENGINE\n---------------------\nPortfolio: ";
        for (std::size_t i = 0; i < portfolio.tickers.size(); ++i) {
            if (i) std::cout << ", ";
            std::cout << portfolio.tickers[i] << "=" << portfolio.weights[i];
        }
        std::cout << "\nTrading days: " << portfolioReturns.size()
                  << "\nExcluded incomplete dates: " << skipped
                  << "\nPortfolio volatility: " << direct << "%"
                  << "\nSPY volatility: " << annualizedVolatility(benchmarkReturns) << "%\n";
        std::cout << "\nCOVARIANCE VALIDATION\n---------------------\n"
                  << "Matrix-based volatility: " << matrix << "%"
                  << "\nNumerical difference: " << gap << " percentage points\n";
        std::cout << "\nHISTORICAL TAIL RISK (DAILY)\n----------------------------\n"
                  << "Portfolio VaR (95%): " << portfolioVaR << "%"
                  << "\nPortfolio ES (95%): " << portfolioES << "%"
                  << "\nSPY VaR (95%): " << spyVaR << "%"
                  << "\nSPY ES (95%): " << spyES << "%\n";
        std::cout << "\nVALIDATION\n----------\nCovariance check: PASSED"
                  << "\nTail risk check: PASSED\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
