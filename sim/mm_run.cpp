#include <fstream>
#include <iostream>
#include <string>

#include "Exchange.h"
#include "NaiveMarketMaker.h"
#include "NoiseTraders.h"
#include "Portfolio.h"
#include "Simulator.h"
#include "StatsCollector.h"

// Usage: mm_run [seed=42] [duration_seconds=300] [offset_ticks=0] [pnl.csv]
int main(int argc, char* argv[]){
    NoiseConfig noiseConfig;
    MarketMakerConfig mmConfig;
    double durationSeconds = 300.0;
    const double warmupSeconds = 10.0;

    if(argc > 1) noiseConfig.seed = std::stoull(argv[1]);
    if(argc > 2) durationSeconds = std::stod(argv[2]);
    if(argc > 3) mmConfig.offsetTicks = std::stoi(argv[3]);

    Simulator simulator;
    Exchange exchange{simulator};
    NoiseTraders noise{simulator, exchange, noiseConfig};
    StatsCollector stats{simulator, exchange, static_cast<SimTime>(warmupSeconds * kSecond)};
    Portfolio portfolio{simulator, exchange};
    NaiveMarketMaker maker{simulator, exchange, portfolio, mmConfig};

    const SimTime start = static_cast<SimTime>(warmupSeconds * kSecond);
    noise.Start();
    stats.Start();
    maker.Start(start);
    simulator.Schedule(start, [&]{ portfolio.StartSampling(kSecond); });

    const SimTime end = static_cast<SimTime>((warmupSeconds + durationSeconds) * kSecond);
    simulator.RunUntil(end);

    const MarketSummary market = stats.Summary(end);
    const PortfolioSummary p = portfolio.Summary();

    std::cout << "seed " << noiseConfig.seed << ", " << durationSeconds << " s, quotes " << mmConfig.quoteSize << " lots at mid -/+ offset " << mmConfig.offsetTicks
              << " tick(s), inventory limit +/-" << mmConfig.maxPosition << "\n\n"
              << "market   : avg spread " << market.avgSpread << " ticks, " << market.trades << " trades, volume " << market.volume << "\n"
              << "us       : " << p.fills << " fills, volume " << p.volume << " (" << (market.volume ? 100.0 * static_cast<double>(p.volume) / static_cast<double>(market.volume) : 0.0) << " % of the market)\n"
              << "position : final " << p.position << ", max |pos| " << p.maxAbsPosition << ", avg |pos| " << p.avgAbsPosition << "\n"
              << "PnL      : realized " << p.realizedPnl << ", unrealized " << p.unrealizedPnl << ", total " << p.totalPnl << " (ticks x units)\n"
              << "           per unit traded " << (p.volume ? p.totalPnl / static_cast<double>(p.volume) : 0.0) << " ticks, max drawdown " << p.maxDrawdown << "\n"
              << "markout  : " << p.markout1s << " ticks/unit after 1 s, " << p.markout5s << " after 5 s  (positive = the price moved our way)\n"
              << "self-trades: " << p.selfTrades << "\n";

    if(argc > 4){
        std::ofstream csv{argv[4]};
        csv << "time_s,position,mid,total_pnl\n";
        for(const auto& s : portfolio.Samples())
            csv << static_cast<double>(s.time) / static_cast<double>(kSecond) << ',' << s.position << ',' << s.mid << ',' << s.totalPnl << '\n';
        std::cout << "PnL samples written to " << argv[4] << "\n";
    }
    return 0;
}
