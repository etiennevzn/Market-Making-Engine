#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "Exchange.h"
#include "Simulator.h"

struct FillRecord{
    SimTime time{};
    Side side{};
    Price price{};
    Quantity quantity{};
};

struct PnlSample{
    SimTime time{};
    std::int64_t position{};
    double mid{};
    double totalPnl{};
};

struct PortfolioSummary{
    std::int64_t position{};
    double realizedPnl{};
    double unrealizedPnl{};
    double totalPnl{};               // = cash + position * mid = realized + unrealized
    std::uint64_t fills{};
    std::uint64_t volume{};          // total quantity traded by us
    std::int64_t maxAbsPosition{};
    double avgAbsPosition{};         // over the 1 s samples
    double maxDrawdown{};            // largest peak-to-trough fall of the total PnL (samples)
    double markout1s{};              // average, per unit, of (mid 1 s after the fill - fill price), sign = our side
    double markout5s{};
    std::uint64_t selfTrades{};      // trades between two of our own orders (must stay 0)
};

// Our own book-keeping: which orders are ours, how much of each is left, and what our fills did to
// position, cash and PnL. PnL is expressed in price units (ticks) x quantity.
//
// Fills are recognised from the Exchange trade feed, by order id. Orders must therefore be registered
// BEFORE they are submitted (a marketable order can trade during Submit itself).
class Portfolio{
public:
    static constexpr SimTime kShortHorizon = 1 * kSecond;
    static constexpr SimTime kLongHorizon = 5 * kSecond;

    Portfolio(Simulator& simulator, Exchange& exchange);

    void RegisterOrder(OrderId orderId, Side side, Quantity quantity);
    void ForgetOrder(OrderId orderId);
    std::optional<Quantity> Remaining(OrderId orderId) const;

    void StartSampling(SimTime interval = kSecond);

    // Average-cost accounting. Public so it can be unit-tested; normally driven by the trade feed.
    void ApplyFill(Side side, Price price, Quantity quantity);

    std::int64_t Position() const {return position_;}
    double RealizedPnl() const {return realized_;}
    double UnrealizedPnl(double mid) const {return static_cast<double>(position_) * (mid - averageCost_);}
    double TotalPnl(double mid) const {return cash_ + static_cast<double>(position_) * mid;}

    // Mid of the book now; if the book is one-sided, the last mid seen (NaN before the first one).
    double CurrentMid() const;

    PortfolioSummary Summary() const;
    const std::vector<FillRecord>& Fills() const {return fills_;}
    const std::vector<PnlSample>& Samples() const {return samples_;}

private:
    void OnTrade(const Trade& trade, SimTime time);
    void OnOwnFill(OrderId orderId, Side side, const Trade& trade, SimTime time);
    void ApplyFillImpl(Side side, Price price, Quantity quantity);
    void TakeSample();
    void ScheduleMarkout(std::size_t horizonIndex, SimTime horizon, Side side, Price price, Quantity quantity);

    Simulator& simulator_;
    Exchange& exchange_;
    std::unordered_map<OrderId, Quantity> remaining_; // own live orders -> quantity left

    std::int64_t position_{0};
    std::int64_t maxAbsPosition_{0};
    double cash_{0};
    double averageCost_{0};
    double realized_{0};

    std::vector<FillRecord> fills_;
    std::vector<PnlSample> samples_;
    SimTime sampleInterval_{kSecond};
    std::uint64_t volume_{0};
    std::uint64_t selfTrades_{0};
    double markoutSum_[2]{0, 0};
    double markoutQty_[2]{0, 0};
    mutable double lastMid_;
};
