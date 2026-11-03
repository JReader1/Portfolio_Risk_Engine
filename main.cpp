#include "src/risk.hpp"

#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Options {
    std::string data = "data/sector_market_daily.csv";
    double confidence = 0.95;
    int benchIterations = 0;
    std::vector<std::string> holdingArgs;
};

void usage() {
    std::cout << "Portfolio Risk Engine\n"
              << "Usage: Portfolio_Risk_Engine [--data FILE] [--confidence 0.95] "
                 "[--bench 100] [TICKER=WEIGHT ...]\n"
              << "Example: Portfolio_Risk_Engine XLV=0.25 XLU=0.25 XLF=0.25 XLI=0.25\n"
              << "With no holdings: XLK=0.40 XLE=0.30 XLP=0.30\n";
}

Options optionsFromArgs(int argc, char* argv[]) {
    Options result;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { usage(); std::exit(0); }
        if (arg == "--data" || arg == "--confidence" || arg == "--bench") {
            if (++i >= argc) throw std::invalid_argument("Missing value for " + arg);
            const std::string value = argv[i];
            if (arg == "--data") result.data = value;
            else if (arg == "--confidence") {
                std::size_t used = 0;
                try { result.confidence = std::stod(value, &used); }
                catch (...) { throw std::invalid_argument("Invalid confidence"); }
                if (used != value.size() || !std::isfinite(result.confidence) ||
                    result.confidence <= 0.0 || result.confidence >= 1.0)
                    throw std::invalid_argument("Confidence must be between 0 and 1");
            } else {
                std::size_t used = 0;
                try { result.benchIterations = std::stoi(value, &used); }
                catch (...) { throw std::invalid_argument("Invalid benchmark iteration count"); }
                if (used != value.size() || result.benchIterations < 1 || result.benchIterations > 100000)
                    throw std::invalid_argument("--bench must be between 1 and 100000");
            }
        } else if (arg.starts_with("--")) {
            throw std::invalid_argument("Unknown option: " + arg);
        } else {
            result.holdingArgs.push_back(arg);
        }
    }
    return result;
}

void report(const char* name, const risk::RiskMetrics& metric) {
    std::cout << name << " annualized volatility: " << metric.annualizedVolatility << "%\n"
              << name << " annualized arithmetic mean: " << metric.annualizedArithmeticMean << "%\n"
              << name << " historical VaR: " << metric.historicalVaR << "%\n"
              << name << " historical ES: " << metric.expectedShortfall << "%\n"
              << name << " maximum drawdown (loss): " << metric.maximumDrawdown << "%\n"
              << name << " worst daily return: " << metric.worstDay << "%\n";
}
} // namespace

int main(int argc, char* argv[]) {
    try {
        const auto options = optionsFromArgs(argc, argv);
        const auto holdings = risk::parseHoldings(options.holdingArgs);
        const auto loaded = risk::loadSeries(options.data, holdings);
        const auto portfolio = risk::portfolioReturns(loaded);
        const auto benchmark = risk::benchmarkReturns(loaded);
        const auto p = risk::calculateMetrics(portfolio, options.confidence);
        const auto b = risk::calculateMetrics(benchmark, options.confidence);
        const double matrixVolatility = risk::covariancePortfolioVolatility(loaded.assetReturns, holdings);
        const double gap = std::abs(p.annualizedVolatility - matrixVolatility);
        if (gap > 1e-8) throw std::runtime_error("Direct and covariance-based volatility disagree");

        std::cout << std::fixed << std::setprecision(4);
        std::cout << "PORTFOLIO RISK ENGINE\n=====================\nPortfolio: ";
        for (std::size_t i = 0; i < holdings.size(); ++i) {
            if (i) std::cout << ", ";
            std::cout << holdings[i].ticker << "=" << holdings[i].weight;
        }
        std::cout << "\nData: " << options.data << "\nAligned trading days: " << portfolio.size()
                  << "\nExcluded incomplete trading dates: " << loaded.incompleteDates << "\n"
                  << "Confidence: " << options.confidence * 100.0 << "%\n";
        std::cout << "\nRISK SUMMARY\n------------\n";
        report("Portfolio", p);
        report("SPY", b);
        std::cout << "\nCOVARIANCE VALIDATION\n---------------------\nMatrix-based volatility: "
                  << matrixVolatility << "%\nAbsolute difference: " << gap
                  << " percentage points\nStatus: PASSED\n";

        const std::vector<risk::StressPeriod> periods = {
            {"COVID selloff window", "2020-02-19", "2020-04-30"},
            {"2022 calendar year", "2022-01-01", "2022-12-31"}
        };
        const auto stresses = risk::calculateStress(loaded.observations, periods);
        std::cout << "\nHISTORICAL STRESS WINDOWS (COMPOUNDED)\n--------------------------------------\n";
        for (const auto& item : stresses) {
            std::cout << item.name << " [" << item.start << " to " << item.end << "] ("
                      << item.days << " aligned sessions): portfolio " << item.portfolioReturn
                      << "%, SPY " << item.benchmarkReturn << "%\n";
        }
        if (stresses.empty()) std::cout << "No selected stress windows overlap the data.\n";

        if (options.benchIterations) {
            // Timed repeated computation only; avoid mixing CSV disk I/O into timing.
            volatile double checksum = 0.0;
            const auto began = std::chrono::steady_clock::now();
            for (int i = 0; i < options.benchIterations; ++i) {
                checksum = checksum + risk::calculateMetrics(portfolio, options.confidence).annualizedVolatility;
                checksum = checksum + risk::covariancePortfolioVolatility(loaded.assetReturns, holdings);
            }
            const auto finished = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double, std::milli>(finished - began).count();
            std::cout << "\nCOMPUTATION BENCHMARK\n---------------------\nIterations: "
                      << options.benchIterations << "\nTotal duration: " << elapsed
                      << " ms\nMilliseconds / iteration: " << elapsed / options.benchIterations
                      << "\nChecksum: " << checksum << "\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
