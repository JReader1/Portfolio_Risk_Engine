#include "risk.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace risk {
namespace {

void checkFinite(const std::vector<double>& values) {
    for (double x : values) {
        if (!std::isfinite(x)) throw std::invalid_argument("Non-finite return");
    }
}

void checkConfidence(double confidence) {
    if (!std::isfinite(confidence) || confidence <= 0.0 || confidence >= 1.0)
        throw std::invalid_argument("Confidence must be strictly between 0 and 1");
}

// This exported input is plain CSV; reject quoted fields rather than silently
// misinterpreting a fully general RFC 4180 file.
std::vector<std::string> parseRow(const std::string& line, std::size_t lineNumber) {
    if (line.find('"') != std::string::npos)
        throw std::runtime_error("Quoted fields unsupported at line " + std::to_string(lineNumber));
    std::stringstream stream(line);
    std::vector<std::string> fields;
    std::string token;
    while (std::getline(stream, token, ',')) fields.push_back(token);
    if (!line.empty() && line.back() == ',') fields.emplace_back();
    for (auto& field : fields) {
        if (!field.empty() && field.back() == '\r') field.pop_back();
    }
    return fields;
}

double parseDouble(const std::string& token, const std::string& location) {
    try {
        std::size_t consumed = 0;
        const double result = std::stod(token, &consumed);
        if (consumed != token.size() || !std::isfinite(result)) throw std::invalid_argument("invalid");
        return result;
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid numeric value at " + location);
    }
}

struct Day {
    std::map<std::string, double> byTicker;
    double benchmark = 0.0;
    bool benchmarkSeen = false;
};

std::size_t columnIndex(const std::vector<std::string>& header,
                        const std::string& expected,
                        const std::string& alternative = "") {
    auto it = std::find(header.begin(), header.end(), expected);
    if (it == header.end() && !alternative.empty())
        it = std::find(header.begin(), header.end(), alternative);
    if (it == header.end()) throw std::runtime_error("CSV missing column: " + expected);
    return static_cast<std::size_t>(it - header.begin());
}

} // namespace

std::vector<Holding> parseHoldings(const std::vector<std::string>& specs) {
    const std::vector<std::string> defaults = {"XLK=0.40", "XLE=0.30", "XLP=0.30"};
    const auto& chosen = specs.empty() ? defaults : specs;
    std::vector<Holding> holdings;
    std::set<std::string> seen;
    double total = 0.0;
    for (const auto& spec : chosen) {
        const auto separator = spec.find('=');
        if (separator == std::string::npos || separator == 0 || separator == spec.size() - 1 ||
            spec.find('=', separator + 1) != std::string::npos)
            throw std::invalid_argument("Expected TICKER=WEIGHT, got: " + spec);
        const std::string ticker = spec.substr(0, separator);
        if (ticker == "SPY") throw std::invalid_argument("SPY is reserved for benchmark");
        if (!std::all_of(ticker.begin(), ticker.end(), [](unsigned char c) {
            return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
        })) throw std::invalid_argument("Invalid ticker: " + ticker);
        if (!seen.insert(ticker).second) throw std::invalid_argument("Duplicate ticker: " + ticker);
        const double weight = parseDouble(spec.substr(separator + 1), "holding " + spec);
        if (weight < 0.0 || weight > 1.0) throw std::invalid_argument("Weights must be in [0, 1]");
        total += weight;
        holdings.push_back({ticker, weight});
    }
    if (std::abs(total - 1.0) > 1e-10)
        throw std::invalid_argument("Portfolio weights must sum to 1.0");
    return holdings;
}

LoadedSeries loadSeries(const std::string& path, const std::vector<Holding>& holdings) {
    if (holdings.empty()) throw std::invalid_argument("No holdings");
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot open CSV: " + path);
    std::string line;
    if (!std::getline(input, line)) throw std::runtime_error("CSV is empty");
    const auto header = parseRow(line, 1);
    const std::size_t dateColumn = columnIndex(header, "trade_date", "date");
    const std::size_t tickerColumn = columnIndex(header, "ticker");
    const std::size_t assetColumn = columnIndex(header, "sector_return");
    const std::size_t marketColumn = columnIndex(header, "market_return");
    const auto maxIndex = std::max({dateColumn, tickerColumn, assetColumn, marketColumn});

    std::set<std::string> selected;
    for (const auto& holding : holdings) selected.insert(holding.ticker);
    std::map<std::string, Day> days;
    std::size_t rowNumber = 1;
    while (std::getline(input, line)) {
        ++rowNumber;
        if (line.empty()) continue;
        const auto fields = parseRow(line, rowNumber);
        if (fields.size() <= maxIndex)
            throw std::runtime_error("Malformed CSV line " + std::to_string(rowNumber));
        const std::string& ticker = fields[tickerColumn];
        if (!selected.count(ticker)) continue;
        const std::string& date = fields[dateColumn];
        if (date.size() != 10 || date[4] != '-' || date[7] != '-')
            throw std::runtime_error("Expected ISO YYYY-MM-DD date at line " + std::to_string(rowNumber));
        const double assetReturn = parseDouble(fields[assetColumn], "CSV line " + std::to_string(rowNumber));
        const double spyReturn = parseDouble(fields[marketColumn], "CSV line " + std::to_string(rowNumber));
        if (assetReturn < -1.0 || spyReturn < -1.0)
            throw std::runtime_error("Simple daily return below -100% on " + date);
        auto& day = days[date];
        if (!day.byTicker.emplace(ticker, assetReturn).second)
            throw std::runtime_error("Duplicate " + ticker + " on " + date);
        if (day.benchmarkSeen && std::abs(day.benchmark - spyReturn) > 1e-10)
            throw std::runtime_error("Inconsistent SPY return on " + date);
        day.benchmark = spyReturn;
        day.benchmarkSeen = true;
    }
    if (days.empty()) throw std::runtime_error("No rows for selected holdings");

    LoadedSeries result;
    result.totalDates = days.size();
    result.assetReturns.resize(holdings.size());
    for (const auto& [date, day] : days) {
        if (day.byTicker.size() != holdings.size()) {
            ++result.incompleteDates;
            continue;
        }
        double portfolio = 0.0;
        for (std::size_t i = 0; i < holdings.size(); ++i) {
            const double r = day.byTicker.at(holdings[i].ticker);
            result.assetReturns[i].push_back(r);
            portfolio += holdings[i].weight * r;
        }
        result.observations.push_back({date, portfolio, day.benchmark});
    }
    if (result.observations.size() < 2)
        throw std::runtime_error("Need at least two complete trading dates");
    return result;
}

std::vector<double> portfolioReturns(const LoadedSeries& data) {
    std::vector<double> returns;
    returns.reserve(data.observations.size());
    for (const auto& item : data.observations) returns.push_back(item.portfolio);
    return returns;
}
std::vector<double> benchmarkReturns(const LoadedSeries& data) {
    std::vector<double> returns;
    returns.reserve(data.observations.size());
    for (const auto& item : data.observations) returns.push_back(item.benchmark);
    return returns;
}

double mean(const std::vector<double>& values) {
    if (values.empty()) throw std::invalid_argument("Mean requires observations");
    checkFinite(values);
    return std::accumulate(values.begin(), values.end(), 0.0) /
           static_cast<double>(values.size());
}

double sampleCovariance(const std::vector<double>& x, const std::vector<double>& y) {
    if (x.size() != y.size() || x.size() < 2)
        throw std::invalid_argument("Covariance requires equal-length samples with >=2 entries");
    const double a = mean(x);
    const double b = mean(y);
    double sum = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) sum += (x[i] - a) * (y[i] - b);
    return sum / static_cast<double>(x.size() - 1);
}

double annualizedVolatility(const std::vector<double>& values) {
    return std::sqrt(sampleCovariance(values, values) * TradingDays) * 100.0;
}

double covariancePortfolioVolatility(const std::vector<std::vector<double>>& assets,
                                     const std::vector<Holding>& holdings) {
    if (assets.empty() || assets.size() != holdings.size())
        throw std::invalid_argument("Asset-weight shape mismatch");
    const auto n = assets.front().size();
    if (n < 2) throw std::invalid_argument("Insufficient observations");
    double total = 0.0;
    for (std::size_t i = 0; i < assets.size(); ++i) {
        if (assets[i].size() != n) throw std::invalid_argument("Unaligned asset returns");
        if (!std::isfinite(holdings[i].weight)) throw std::invalid_argument("Invalid weight");
        total += holdings[i].weight;
    }
    if (std::abs(total - 1.0) > 1e-10) throw std::invalid_argument("Weights must sum to one");
    double variance = 0.0;
    for (std::size_t i = 0; i < assets.size(); ++i)
        for (std::size_t j = 0; j < assets.size(); ++j)
            variance += holdings[i].weight * holdings[j].weight * sampleCovariance(assets[i], assets[j]);
    if (variance < -1e-12) throw std::runtime_error("Negative covariance-based variance");
    return std::sqrt(std::max(0.0, variance) * TradingDays) * 100.0;
}

double historicalVaR(const std::vector<double>& values, double confidence) {
    checkConfidence(confidence);
    if (values.empty()) throw std::invalid_argument("VaR requires observations");
    checkFinite(values);
    std::vector<double> losses;
    losses.reserve(values.size());
    for (double r : values) losses.push_back(-r);
    std::sort(losses.begin(), losses.end());
    const auto rank = static_cast<std::size_t>(std::ceil(confidence * losses.size()));
    return losses[rank - 1] * 100.0;
}

double historicalES(const std::vector<double>& values, double confidence) {
    checkConfidence(confidence);
    if (values.empty()) throw std::invalid_argument("ES requires observations");
    checkFinite(values);
    std::vector<double> losses;
    losses.reserve(values.size());
    for (double r : values) losses.push_back(-r);
    std::sort(losses.begin(), losses.end(), std::greater<double>{});
    const auto count = static_cast<std::size_t>(std::ceil((1.0 - confidence) * losses.size()));
    return (std::accumulate(losses.begin(), losses.begin() + count, 0.0) /
            static_cast<double>(count)) * 100.0;
}

double maxDrawdown(const std::vector<double>& values) {
    if (values.empty()) throw std::invalid_argument("Drawdown requires observations");
    checkFinite(values);
    double wealth = 1.0;
    double peak = 1.0;
    double worst = 0.0;
    for (double r : values) {
        if (r < -1.0) throw std::invalid_argument("Simple return below -100%");
        wealth *= (1.0 + r);
        peak = std::max(peak, wealth);
        worst = std::min(worst, wealth / peak - 1.0);
    }
    return -worst * 100.0; // positive loss magnitude
}

RiskMetrics calculateMetrics(const std::vector<double>& values, double confidence) {
    if (values.size() < 2) throw std::invalid_argument("At least two returns required");
    checkConfidence(confidence);
    checkFinite(values);
    auto metrics = RiskMetrics{annualizedVolatility(values), mean(values) * TradingDays * 100.0,
                               historicalVaR(values, confidence), historicalES(values, confidence),
                               maxDrawdown(values), *std::min_element(values.begin(), values.end()) * 100.0};
    if (metrics.expectedShortfall + 1e-10 < metrics.historicalVaR)
        throw std::runtime_error("Expected Shortfall below historical VaR");
    return metrics;
}

std::vector<StressResult> calculateStress(const std::vector<Observation>& observations,
                                         const std::vector<StressPeriod>& periods) {
    std::vector<StressResult> results;
    for (const auto& period : periods) {
        if (period.start > period.end) throw std::invalid_argument("Invalid stress window");
        double portfolioWealth = 1.0;
        double benchmarkWealth = 1.0;
        std::size_t count = 0;
        for (const auto& observation : observations) {
            if (observation.date < period.start || observation.date > period.end) continue;
            if (!std::isfinite(observation.portfolio) || !std::isfinite(observation.benchmark))
                throw std::invalid_argument("Non-finite stress return");
            portfolioWealth *= 1.0 + observation.portfolio;
            benchmarkWealth *= 1.0 + observation.benchmark;
            ++count;
        }
        if (count) results.push_back({period.name, period.start, period.end, count,
                                     (portfolioWealth - 1.0) * 100.0,
                                     (benchmarkWealth - 1.0) * 100.0});
    }
    return results;
}

} // namespace risk
