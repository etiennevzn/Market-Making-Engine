#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "Exchange.h"
#include "Simulator.h"

struct Sample{
    SimTime time{};
    std::optional<Price> bestBid;
    std::optional<Price> bestAsk;
    Quantity bidDepth{};   // total quantity in the top levels of each side
    Quantity askDepth{};
    std::optional<Price> reference; // reference price of the noise flow, if provided
};

struct MarketSummary{
    std::uint64_t trades{};
    std::uint64_t volume{};
    double tradesPerSecond{};
    std::size_t samples{};
    double twoSidedFraction{};     // share of samples where both a bid and an ask exist
    double avgSpread{};            // in ticks, over two-sided samples
    double avgBidDepth{};
    double avgAskDepth{};
    double avgAbsMidError{};       // mean |mid - reference| in ticks (0 if no reference)
    double minMid{};
    double maxMid{};
    double finalMid{};
};

// Watches the market after a warm-up period: counts trades/volume (as an Exchange trade listener) 
// and samples the top of the book at a fixed interval.
class StatsCollector{
public:
    static constexpr std::size_t kDepthLevels = 5;

    StatsCollector(Simulator& simulator, Exchange& exchange, SimTime warmup, 
                   SimTime sampleInterval = 100 * kMillisecond, std::function<Price()> reference = {});

    void Start(); // schedules the first sample at the end of the warm-up
    MarketSummary Summary(SimTime end) const;
    const std::vector<Sample>& Samples() const {return samples_;}

private:
    void TakeSample();

    Simulator& simulator_;
    Exchange& exchange_;
    SimTime warmup_;
    SimTime sampleInterval_;
    std::function<Price()> reference_;
    std::uint64_t trades_{0};
    std::uint64_t volume_{0};
    std::vector<Sample> samples_;
};
