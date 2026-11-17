#pragma once

#include <cstdint>
#include <optional>

#include "Exchange.h"
#include "Portfolio.h"
#include "Simulator.h"

struct MarketMakerConfig{
    ParticipantId participantId = 1;
    SimTime quoteInterval = 100 * kMillisecond; // how often the quotes are reviewed
    Price offsetTicks = 0;                      // 0 = first tick on each side of the mid, 1 = one tick further away, ...
    Quantity quoteSize = 10;
    std::int64_t maxPosition = 200;             // hard inventory limit (units, long or short)
};

// Deliberately simple: every quoteInterval it looks at the mid of the book and keeps ONE bid and ONE ask around it.
//  - bid = first tick below the mid - offset, ask = first tick above the mid + offset (symmetric, never crossed)
//  - a quote whose price is still right is left alone, which keeps its place in the queue
//  - near the inventory limit the quote that would increase the position gets smaller, then disappears
//  - no view on the price and no inventory skew: this is the baseline the Avellaneda-Stoikov version will be compared to.
// Known weakness: the mid is computed from the whole book, including our own quotes.
class NaiveMarketMaker{
public:
    NaiveMarketMaker(Simulator& simulator, Exchange& exchange, Portfolio& portfolio, MarketMakerConfig config);

    void Start(SimTime firstRefresh); // schedules the first review at this simulated time

private:
    struct Quote{
        OrderId id{0};   // 0 = no live quote
        Price price{0};
    };

    void Refresh();
    bool StillValid(const Quote& quote, std::optional<Price> target, Quantity desired) const;
    void Drop(Quote& quote);
    void Place(Side side, Price price, Quantity quantity, Quote& quote);

    Simulator& simulator_;
    Exchange& exchange_;
    Portfolio& portfolio_;
    MarketMakerConfig config_;
    Quote bid_;
    Quote ask_;
};
