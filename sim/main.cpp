#include <fstream>
#include <iostream>
#include <string>

#include "Exchange.h"
#include "NoiseTraders.h"
#include "Simulator.h"
#include "StatsCollector.h"

// Usage: sim_run [seed=42] [duration_seconds=60] [samples.csv]
int main(int argc, char* argv[]){
    NoiseConfig config;
    double durationSeconds = 60.0;
    const double warmupSeconds = 10.0;

    if(argc > 1) config.seed = std::stoull(argv[1]);
    if(argc > 2) durationSeconds = std::stod(argv[2]);

    Simulator simulator;
    Exchange exchange{simulator};
    NoiseTraders noise{simulator, exchange, config};
    StatsCollector stats{simulator, exchange, static_cast<SimTime>(warmupSeconds * kSecond), 100 * kMillisecond, [&noise]{ return noise.ReferencePrice(); }};

    noise.Start();
    stats.Start();

    const SimTime end = static_cast<SimTime>((warmupSeconds + durationSeconds) * kSecond);
    simulator.RunUntil(end);

    const MarketSummary s = stats.Summary(end);
    std::cout << "seed " << config.seed << ", " << durationSeconds << " s simulated (after " << warmupSeconds << " s warm-up)\n"
              << "trades            : " << s.trades << "  (" << s.tradesPerSecond << " /s), volume " << s.volume << "\n"
              << "two-sided book    : " << 100.0 * s.twoSidedFraction << " % of samples\n"
              << "avg spread        : " << s.avgSpread << " ticks\n"
              << "avg depth (top " << StatsCollector::kDepthLevels << ")  : bid " << s.avgBidDepth << ", ask " << s.avgAskDepth << "\n"
              << "mid range         : [" << s.minMid << ", " << s.maxMid << "], final " << s.finalMid << "\n"
              << "avg |mid - ref|   : " << s.avgAbsMidError << " ticks\n"
              << "resting orders    : " << exchange.Book().Size() << "\n";

    if(argc > 3){
        std::ofstream csv{argv[3]};
        csv << "time_s,best_bid,best_ask,mid,spread,reference\n";
        for(const auto& sample : stats.Samples()){
            if(!sample.bestBid || !sample.bestAsk) continue;
            csv << static_cast<double>(sample.time) / static_cast<double>(kSecond) << ','
                << *sample.bestBid << ',' << *sample.bestAsk << ','
                << 0.5 * (static_cast<double>(*sample.bestBid) + static_cast<double>(*sample.bestAsk)) << ','
                << (*sample.bestAsk - *sample.bestBid) << ','
                << (sample.reference ? *sample.reference : 0) << '\n';
        }
        std::cout << "samples written to " << argv[3] << "\n";
    }
    return 0;
}
