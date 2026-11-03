#define PORTFOLIO_RISK_ENGINE_TEST
#include "../main.cpp"

#include <cassert>
#include <cstdio>

bool approximately(double a, double b, double tolerance = 1e-9) {
    return std::abs(a-b) < tolerance;
}

template <typename Function>
bool fails(Function function) {
    try { function(); } catch (const std::exception&) { return true; }
    return false;
}

int main() {
    const std::vector<double> x = {0.01, 0.03, -0.02, 0.04};
    const std::vector<double> y = {-0.01, 0.02, 0.01, 0.00};
    assert(approximately(calculateMean(x), 0.015));
    assert(approximately(sampleCovariance(x, y), sampleCovariance(y, x)));

    const std::vector<double> weights = {0.4, 0.6};
    const std::vector<std::vector<double>> assets = {x, y};
    std::vector<double> portfolio;
    for (std::size_t i = 0; i < x.size(); ++i)
        portfolio.push_back(0.4 * x[i] + 0.6 * y[i]);
    assert(approximately(matrixPortfolioVolatility(assets, weights),
                         annualizedVolatility(portfolio)));

    const std::vector<double> returns = {-0.04, -0.02, -0.01, 0.00, 0.02};
    assert(approximately(historicalVaR(returns, 0.80), 2.0));
    assert(approximately(historicalES(returns, 0.80), 4.0));
    assert(fails([&]{ historicalVaR(returns, 1.0); }));
    assert(fails([&]{ historicalES(returns, 0.0); }));
    assert(fails([&]{ sampleVariance({1.0}); }));
    assert(fails([&]{ matrixPortfolioVolatility(assets, {0.1, 0.2}); }));

    char a0[] = "risk-engine";
    char a1[] = "XLK=0.40";
    char a2[] = "XLE=0.60";
    char* args[] = {a0, a1, a2};
    const auto chosen = parsePortfolio(3, args);
    assert(chosen.tickers.size() == 2);
    assert(approximately(chosen.weights[0], 0.40));
    char duplicate[] = "XLK=0.60";
    char* bad[] = {a0, a1, duplicate};
    assert(fails([&]{ parsePortfolio(3, bad); }));

    const char* testFile = "portfolio_risk_test_data.csv";
    {
        std::ofstream stream(testFile);
        stream << "trade_date,ticker,sector,sector_return,market_return\n";
        stream << "2025-01-02,XLK,Technology,0.01,0.005\n";
        stream << "2025-01-02,XLE,Energy,0.02,0.005\n";
        stream << "2025-01-03,XLK,Technology,-0.01,-0.004\n";
        stream << "2025-01-03,XLE,Energy,-0.02,-0.004\n";
        stream << "2025-01-04,XLK,Technology,0.03,0.01\n";
    }
    const auto data = loadMarketData(testFile, chosen);
    std::remove(testFile);
    assert(data.days.size() == 3);
    assert(data.days.at("2025-01-04").returns.size() == 1);
    std::cout << "All risk engine unit tests passed.\n";
    return 0;
}
