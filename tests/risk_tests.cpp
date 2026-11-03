#include "risk.hpp"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void near(double actual, double expected, double epsilon = 1e-10) {
    if (std::abs(actual - expected) > epsilon) {
        std::cerr << "Expected " << expected << " but found " << actual << '\n';
        std::abort();
    }
}
template <class F> void mustThrow(F f) {
    try { f(); } catch (const std::exception&) { return; }
    std::cerr << "Expected an exception\n";
    std::abort();
}
}

int main() {
    using namespace risk;
    const std::vector<double> a = {0.01, 0.03, -0.02, 0.04};
    const std::vector<double> b = {-0.01, 0.02, 0.01, 0.0};
    near(mean(a), 0.015);
    near(sampleCovariance(a,b), sampleCovariance(b,a));
    near(sampleCovariance(a,a), 0.0007); // verified hand calculation
    const auto holdings = parseHoldings({"XLK=0.4", "XLE=0.6"});
    const std::vector<std::vector<double>> assets = {a,b};
    std::vector<double> blended;
    for (std::size_t i = 0; i < a.size(); ++i) blended.push_back(0.4*a[i]+0.6*b[i]);
    near(covariancePortfolioVolatility(assets, holdings), annualizedVolatility(blended));

    const std::vector<double> returns = {-0.04,-0.02,-0.01,0.0,0.02};
    near(historicalVaR(returns,0.8),2.0);
    near(historicalES(returns,0.8),4.0);
    near(maxDrawdown({0.1,-0.1,-0.2,0.2}),28.0);
    near(maxDrawdown({0.1,0.2,0.3}),0.0);
    near(calculateMetrics(returns,0.8).worstDay,-4.0);
    const std::vector<Observation> observations = {{"2020-03-01",0.1,0.0},{"2020-03-02",-0.1,-0.05},{"2020-03-03",0.2,0.01}};
    const auto stress = calculateStress(observations,{{"example","2020-03-01","2020-03-02"}});
    if (stress.size()!=1 || stress[0].days!=2) std::abort();
    near(stress[0].portfolioReturn,-1.0);
    near(stress[0].benchmarkReturn,-5.0);
    mustThrow([&]{parseHoldings({"XLK=0.5","XLE=0.4"});});
    mustThrow([&]{parseHoldings({"XLK=0.5","XLK=0.5"});});
    mustThrow([&]{parseHoldings({"XLK=nan","XLE=1"});});
    mustThrow([&]{parseHoldings({"SPY=1"});});
    mustThrow([&]{historicalVaR(returns,1.0);});
    mustThrow([&]{historicalES(returns,0.0);});
    mustThrow([&]{annualizedVolatility({1.0});});
    mustThrow([&]{mean({std::numeric_limits<double>::quiet_NaN()});});
    mustThrow([&]{covariancePortfolioVolatility({a},{holdings[0],holdings[1]});});
    mustThrow([&]{maxDrawdown({-1.2});});

    const auto temp = std::filesystem::temp_directory_path()/"portfolio_risk_unit_test.csv";
    {
        std::ofstream out(temp);
        out << "trade_date,ticker,sector,sector_return,market_return\n";
        out << "2020-01-01,XLK,Technology,0.01,0.001\n";
        out << "2020-01-01,XLE,Energy,0.02,0.001\n";
        out << "2020-01-02,XLK,Technology,0.04,0.002\n";
        out << "2020-01-03,XLK,Technology,0.06,0.003\n";
        out << "2020-01-03,XLE,Energy,0.08,0.003\n";
    }
    auto loaded = loadSeries(temp.string(),holdings);
    if (loaded.totalDates!=3 || loaded.incompleteDates!=1 || loaded.observations.size()!=2) std::abort();
    near(loaded.observations[0].portfolio,0.016);
    near(loaded.observations[1].portfolio,0.072);
    {
        std::ofstream out(temp, std::ios::app);
        out << "2020-01-03,XLE,Energy,0.08,0.003\n";
    }
    mustThrow([&]{loadSeries(temp.string(),holdings);});
    std::filesystem::remove(temp);
    std::cout << "All numerical, parsing, alignment, and input validation tests passed.\n";
}
