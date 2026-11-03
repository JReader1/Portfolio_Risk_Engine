
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

struct DailyReturns {
    double xlk = 0.0;
    double xle = 0.0;
    double xlp = 0.0;
    double spy = 0.0;

    bool hasXLK = false;
    bool hasXLE = false;
    bool hasXLP = false;
};

// ============================================================
// STATISTICAL FUNCTIONS
// ============================================================

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
// CSV LOADING
// ============================================================

std::map<std::string, DailyReturns> loadMarketData(
    const std::string& path
) {
    std::ifstream file(path);

    if (!file) {
        throw std::runtime_error("Cannot open input CSV: " + path);
    }

    std::string header;

    if (!std::getline(file, header)) {
        throw std::runtime_error("CSV is empty");
    }

    // Expected sector_market_daily.csv column positions:
    // 0 = trade_date
    // 1 = ticker
    // 3 = sector_return
    // 4 = market_return
    //
    // Returns must be decimal values, e.g. 0.01 = 1%.

    std::map<std::string, DailyReturns> days;

    std::string line;
    std::size_t lineNumber = 1;

    while (std::getline(file, line)) {
        ++lineNumber;

        if (line.empty()) {
            continue;
        }

        std::stringstream stream(line);
        std::vector<std::string> fields;
        std::string field;

        while (std::getline(stream, field, ',')) {
            fields.push_back(field);
        }

        if (fields.size() < 5) {
            throw std::runtime_error(
                "Malformed CSV row at line " +
                std::to_string(lineNumber)
            );
        }

        const std::string& date = fields[0];
        const std::string& ticker = fields[1];

        if (ticker != "XLK" &&
            ticker != "XLE" &&
            ticker != "XLP") {
            continue;
        }

        if (date.empty()) {
            throw std::runtime_error(
                "Missing trade date at line " +
                std::to_string(lineNumber)
            );
        }

        double sectorReturn = 0.0;
        double spyReturn = 0.0;

        try {
            std::size_t sectorChars = 0;
            std::size_t spyChars = 0;

            sectorReturn = std::stod(fields[3], &sectorChars);
            spyReturn = std::stod(fields[4], &spyChars);

            if (sectorChars != fields[3].size() ||
                spyChars != fields[4].size()) {
                throw std::invalid_argument("Unexpected characters");
            }
        } catch (const std::exception&) {
            throw std::runtime_error(
                "Invalid return at line " +
                std::to_string(lineNumber)
            );
        }

        if (!std::isfinite(sectorReturn) ||
            !std::isfinite(spyReturn)) {
            throw std::runtime_error(
                "Non-finite return at line " +
                std::to_string(lineNumber)
            );
        }

        DailyReturns& day = days[date];

        if (ticker == "XLK") {
            if (day.hasXLK) {
                throw std::runtime_error("Duplicate XLK on " + date);
            }

            day.xlk = sectorReturn;
            day.hasXLK = true;
        }
        else if (ticker == "XLE") {
            if (day.hasXLE) {
                throw std::runtime_error("Duplicate XLE on " + date);
            }

            day.xle = sectorReturn;
            day.hasXLE = true;
        }
        else {
            if (day.hasXLP) {
                throw std::runtime_error("Duplicate XLP on " + date);
            }

            day.xlp = sectorReturn;
            day.hasXLP = true;
        }

        // Every sector row should report the same SPY return
        // for a given date.
        const int priorObservations =
            static_cast<int>(day.hasXLK) +
            static_cast<int>(day.hasXLE) +
            static_cast<int>(day.hasXLP) - 1;

        if (priorObservations > 0 &&
            std::abs(day.spy - spyReturn) > 1e-10) {
            throw std::runtime_error(
                "Inconsistent SPY return on " + date
            );
        }

        day.spy = spyReturn;
    }

    if (days.empty()) {
        throw std::runtime_error("No matching ETF data found");
    }

    for (const auto& [date, day] : days) {
        if (!day.hasXLK || !day.hasXLE || !day.hasXLP) {
            throw std::runtime_error(
                "Missing ETF observation on " + date
            );
        }
    }

    return days;
}

// ============================================================
// MAIN PROGRAM
// ============================================================

int main(int argc, char* argv[]) {
    try {
        const std::string path =
            argc > 1
                ? argv[1]
                : "data/sector_market_daily.csv";

        const auto days = loadMarketData(path);

        // Portfolio allocation
        const std::vector<double> weights = {
            0.40, // XLK - Technology
            0.30, // XLE - Energy
            0.30  // XLP - Consumer Staples
        };

        std::vector<double> xlkReturns;
        std::vector<double> xleReturns;
        std::vector<double> xlpReturns;

        std::vector<double> portfolioReturns;
        std::vector<double> benchmarkReturns;

        for (const auto& [date, day] : days) {
            const double portfolioReturn =
                weights[0] * day.xlk +
                weights[1] * day.xle +
                weights[2] * day.xlp;

            xlkReturns.push_back(day.xlk);
            xleReturns.push_back(day.xle);
            xlpReturns.push_back(day.xlp);

            portfolioReturns.push_back(portfolioReturn);
            benchmarkReturns.push_back(day.spy);
        }

        std::cout << std::fixed << std::setprecision(4);

        // ----------------------------------------------------
        // VOLATILITY
        // ----------------------------------------------------

        const double portfolioVolatility =
            annualizedVolatility(portfolioReturns);

        const double spyVolatility =
            annualizedVolatility(benchmarkReturns);

        std::cout << "PORTFOLIO RISK ENGINE\n";
        std::cout << "---------------------\n";

        std::cout << "Trading days: "
                  << portfolioReturns.size() << "\n";

        std::cout << "Portfolio volatility: "
                  << portfolioVolatility << "%\n";

        std::cout << "SPY volatility: "
                  << spyVolatility << "%\n";

        // ----------------------------------------------------
        // COVARIANCE VALIDATION
        // ----------------------------------------------------

        const std::vector<std::vector<double>> assets = {
            xlkReturns,
            xleReturns,
            xlpReturns
        };

        const double matrixVolatility =
            matrixPortfolioVolatility(assets, weights);

        const double difference =
            std::abs(matrixVolatility - portfolioVolatility);

        std::cout << "\nCOVARIANCE VALIDATION\n";
        std::cout << "---------------------\n";

        std::cout << "Matrix-based volatility: "
                  << matrixVolatility << "%\n";

        std::cout << "Numerical difference: "
                  << difference << " percentage points\n";

        if (difference > 1e-8) {
            throw std::runtime_error(
                "Portfolio variance calculations disagree"
            );
        }

        // ----------------------------------------------------
        // HISTORICAL TAIL RISK
        // ----------------------------------------------------

        const double portfolioVaR =
            historicalVaR(portfolioReturns);

        const double portfolioES =
            historicalES(portfolioReturns);

        const double spyVaR =
            historicalVaR(benchmarkReturns);

        const double spyES =
            historicalES(benchmarkReturns);

        std::cout << "\nHISTORICAL TAIL RISK (DAILY)\n";
        std::cout << "----------------------------\n";

        std::cout << "Portfolio VaR (95%): "
                  << portfolioVaR << "%\n";

        std::cout << "Portfolio ES (95%): "
                  << portfolioES << "%\n";

        std::cout << "SPY VaR (95%): "
                  << spyVaR << "%\n";

        std::cout << "SPY ES (95%): "
                  << spyES << "%\n";

        // ----------------------------------------------------
        // RISK VALIDATION
        // ----------------------------------------------------

        if (portfolioES + 1e-10 < portfolioVaR ||
            spyES + 1e-10 < spyVaR) {
            throw std::runtime_error(
                "Expected Shortfall is below Value at Risk"
            );
        }

        std::cout << "\nVALIDATION\n";
        std::cout << "----------\n";
        std::cout << "Covariance check: PASSED\n";
        std::cout << "Tail risk check: PASSED\n";

    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    }

    return 0;
}
