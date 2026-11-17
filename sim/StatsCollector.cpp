#include "StatsCollector.h"

#include <algorithm>
#include <cmath>

StatsCollector::StatsCollector(Simulator& simulator, Exchange& exchange, SimTime warmup, SimTime sampleInterval, std::function<Price()> reference) :
    simulator_(simulator),
    exchange_(exchange),
    warmup_(warmup),
    sampleInterval_(sampleInterval),
    reference_(std::move(reference))
{
    exchange_.AddTradeListener([this](const Trade& trade, SimTime time){
        if(time < warmup_) return;
        ++trades_;
        volume_ += trade.GetQuantity();
    });
}

void StatsCollector::Start(){
    simulator_.Schedule(warmup_, [this]{ TakeSample(); });
}

void StatsCollector::TakeSample(){
    Sample sample;
    sample.time = simulator_.Now();
    sample.bestBid = exchange_.Book().BestBid();
    sample.bestAsk = exchange_.Book().BestAsk();

    const auto infos = exchange_.Book().GetOrderInfos();
    const auto depth = [](const LevelInfos& levels){
        Quantity total = 0;
        for(std::size_t i = 0; i < levels.size() && i < kDepthLevels; ++i) total += levels[i].quantity_;
        return total;
    };
    sample.bidDepth = depth(infos.GetBids());
    sample.askDepth = depth(infos.GetAsks());
    if(reference_) sample.reference = reference_();

    samples_.push_back(sample);
    simulator_.ScheduleAfter(sampleInterval_, [this]{ TakeSample(); });
}

MarketSummary StatsCollector::Summary(SimTime end) const{
    MarketSummary summary;
    summary.trades = trades_;
    summary.volume = volume_;
    summary.samples = samples_.size();

    const double seconds = static_cast<double>(end - warmup_) / static_cast<double>(kSecond);
    if(seconds > 0) summary.tradesPerSecond = static_cast<double>(trades_) / seconds;

    std::size_t twoSided = 0;
    double spreadSum = 0, bidDepthSum = 0, askDepthSum = 0, errorSum = 0;
    std::size_t errorCount = 0;
    bool first = true;

    for(const auto& s : samples_){
        if(!s.bestBid || !s.bestAsk) continue;
        ++twoSided;

        const double mid = 0.5 * (static_cast<double>(*s.bestBid) + static_cast<double>(*s.bestAsk));
        spreadSum += static_cast<double>(*s.bestAsk - *s.bestBid);
        bidDepthSum += s.bidDepth;
        askDepthSum += s.askDepth;
        if(s.reference){ errorSum += std::abs(mid - static_cast<double>(*s.reference)); ++errorCount; }

        summary.minMid = first ? mid : std::min(summary.minMid, mid);
        summary.maxMid = first ? mid : std::max(summary.maxMid, mid);
        summary.finalMid = mid;
        first = false;
    }

    if(!samples_.empty()) summary.twoSidedFraction = static_cast<double>(twoSided) / static_cast<double>(samples_.size());
    if(twoSided > 0){
        summary.avgSpread = spreadSum / static_cast<double>(twoSided);
        summary.avgBidDepth = bidDepthSum / static_cast<double>(twoSided);
        summary.avgAskDepth = askDepthSum / static_cast<double>(twoSided);
    }
    if(errorCount > 0) summary.avgAbsMidError = errorSum / static_cast<double>(errorCount);

    return summary;
}
