#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace risk {

constexpr double TradingDays = 252.0;

struct Holding {
    std::string ticker;
    double weight;
};

struct Observation {
    std::string date;
    double portfolio;
    double benchmark;
};

struct LoadedSeries {
    std::vector<Observation> observations;
    std::vector<std::vector<double>> assetReturns; // aligned, asset-major
    std::size_t incompleteDates = 0;
    std::size_t totalDates = 0;
};

struct RiskMetrics {
    double annualizedVolatility;
    double annualizedArithmeticMean;
    double historicalVaR;
    double expectedShortfall;
    double maximumDrawdown;
    double worstDay;
};

struct StressPeriod {
    std::string name;
    std::string start;
    std::string end;
};

struct StressResult {
    std::string name;
    std::string start;
    std::string end;
    std::size_t days;
    double portfolioReturn;
    double benchmarkReturn;
};

std::vector<Holding> parseHoldings(const std::vector<std::string>& specs);
LoadedSeries loadSeries(const std::string& path, const std::vector<Holding>& holdings);
std::vector<double> portfolioReturns(const LoadedSeries& data);
std::vector<double> benchmarkReturns(const LoadedSeries& data);
double mean(const std::vector<double>& values);
double sampleCovariance(const std::vector<double>& x, const std::vector<double>& y);
double annualizedVolatility(const std::vector<double>& values);
double covariancePortfolioVolatility(const std::vector<std::vector<double>>& assets,
                                     const std::vector<Holding>& holdings);
double historicalVaR(const std::vector<double>& values, double confidence = 0.95);
double historicalES(const std::vector<double>& values, double confidence = 0.95);
double maxDrawdown(const std::vector<double>& values);
RiskMetrics calculateMetrics(const std::vector<double>& values, double confidence = 0.95);
std::vector<StressResult> calculateStress(const std::vector<Observation>& observations,
                                         const std::vector<StressPeriod>& periods);

} // namespace risk
