#include "Portfolio.h"

#include <algorithm>
#include <cmath>
#include <limits>

Portfolio::Portfolio(Simulator& simulator, Exchange& exchange) :
    simulator_(simulator),
    exchange_(exchange),
    lastMid_(std::numeric_limits<double>::quiet_NaN())
{
    exchange_.AddTradeListener([this](const Trade& trade, SimTime time){ OnTrade(trade, time); });
}

void Portfolio::RegisterOrder(OrderId orderId, Side, Quantity quantity){
    remaining_[orderId] = quantity;
}

void Portfolio::ForgetOrder(OrderId orderId){
    remaining_.erase(orderId);
}

std::optional<Quantity> Portfolio::Remaining(OrderId orderId) const{
    const auto it = remaining_.find(orderId);
    if(it == remaining_.end()) return std::nullopt;
    return it->second;
}

void Portfolio::OnTrade(const Trade& trade, SimTime time){
    const OrderId bidId = trade.GetBidTrade().orderId_;
    const OrderId askId = trade.GetAskTrade().orderId_;
    const bool ownBid = remaining_.contains(bidId);
    const bool ownAsk = remaining_.contains(askId);

    if(ownBid && ownAsk){ ++selfTrades_; return; } // self-trade prevention should make this impossible
    if(ownBid) OnOwnFill(bidId, Side::Buy, trade, time);
    else if(ownAsk) OnOwnFill(askId, Side::Sell, trade, time);
}

void Portfolio::OnOwnFill(OrderId orderId, Side side, const Trade& trade, SimTime time){
    const Quantity quantity = trade.GetQuantity();
    const Price price = trade.GetPrice(); // price of the resting order

    ApplyFill(side, price, quantity);
    fills_.push_back(FillRecord{time, side, price, quantity});
    volume_ += quantity;

    auto it = remaining_.find(orderId);
    it->second -= std::min(quantity, it->second);
    if(it->second == 0) remaining_.erase(it);

    ScheduleMarkout(0, kShortHorizon, side, price, quantity);
    ScheduleMarkout(1, kLongHorizon, side, price, quantity);
}

void Portfolio::ApplyFill(Side side, Price price, Quantity quantity){
    ApplyFillImpl(side, price, quantity);
    maxAbsPosition_ = std::max(maxAbsPosition_, std::abs(position_));
}

void Portfolio::ApplyFillImpl(Side side, Price price, Quantity quantity){
    const std::int64_t signedQuantity = side == Side::Buy ? static_cast<std::int64_t>(quantity) : -static_cast<std::int64_t>(quantity);
    cash_ -= static_cast<double>(signedQuantity) * static_cast<double>(price);

    const bool increasing = position_ == 0 || ((position_ > 0) == (signedQuantity > 0));
    if(increasing){
        const double held = static_cast<double>(std::abs(position_));
        averageCost_ = (held * averageCost_ + static_cast<double>(quantity) * static_cast<double>(price)) / (held + static_cast<double>(quantity));
        position_ += signedQuantity;
        return;
    }

    // reducing, closing or flipping the position
    const std::int64_t closed = std::min<std::int64_t>(static_cast<std::int64_t>(quantity), std::abs(position_));
    const double perUnit = position_ > 0 ? static_cast<double>(price) - averageCost_ : averageCost_ - static_cast<double>(price);
    realized_ += static_cast<double>(closed) * perUnit;
    position_ += signedQuantity;

    if(position_ == 0) averageCost_ = 0;
    else if(static_cast<std::int64_t>(quantity) > closed) averageCost_ = static_cast<double>(price); // flipped: the new position opens at this price
}

double Portfolio::CurrentMid() const{
    const auto bid = exchange_.Book().BestBid();
    const auto ask = exchange_.Book().BestAsk();
    if(bid && ask) lastMid_ = 0.5 * (static_cast<double>(*bid) + static_cast<double>(*ask));
    return lastMid_;
}

void Portfolio::ScheduleMarkout(std::size_t horizonIndex, SimTime horizon, Side side, Price price, Quantity quantity){
    simulator_.ScheduleAfter(horizon, [this, horizonIndex, side, price, quantity]{
        const double mid = CurrentMid();
        if(std::isnan(mid)) return;
        const double move = side == Side::Buy ? mid - static_cast<double>(price) : static_cast<double>(price) - mid;
        markoutSum_[horizonIndex] += move * static_cast<double>(quantity);
        markoutQty_[horizonIndex] += static_cast<double>(quantity);
    });
}

void Portfolio::StartSampling(SimTime interval){
    sampleInterval_ = interval;
    simulator_.ScheduleAfter(0, [this]{ TakeSample(); });
}

void Portfolio::TakeSample(){
    const double mid = CurrentMid();
    if(!std::isnan(mid)) samples_.push_back(PnlSample{simulator_.Now(), position_, mid, TotalPnl(mid)});
    simulator_.ScheduleAfter(sampleInterval_, [this]{ TakeSample(); });
}

PortfolioSummary Portfolio::Summary() const{
    PortfolioSummary s;
    s.position = position_;
    s.realizedPnl = realized_;
    s.fills = fills_.size();
    s.volume = volume_;
    s.selfTrades = selfTrades_;
    s.markout1s = markoutQty_[0] > 0 ? markoutSum_[0] / markoutQty_[0] : 0.0;
    s.markout5s = markoutQty_[1] > 0 ? markoutSum_[1] / markoutQty_[1] : 0.0;

    const double mid = CurrentMid();
    if(!std::isnan(mid)){
        s.unrealizedPnl = UnrealizedPnl(mid);
        s.totalPnl = TotalPnl(mid);
    }

    double peak = 0, absSum = 0;
    for(const auto& sample : samples_){
        s.maxAbsPosition = std::max<std::int64_t>(s.maxAbsPosition, std::abs(sample.position));
        absSum += static_cast<double>(std::abs(sample.position));
        peak = std::max(peak, sample.totalPnl);
        s.maxDrawdown = std::max(s.maxDrawdown, peak - sample.totalPnl);
    }
    if(!samples_.empty()) s.avgAbsPosition = absSum / static_cast<double>(samples_.size());

    s.maxAbsPosition = std::max(s.maxAbsPosition, maxAbsPosition_); // exact: updated at every fill, not only at the samples
    return s;
}
