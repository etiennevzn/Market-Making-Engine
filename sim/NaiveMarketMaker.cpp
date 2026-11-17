#include "NaiveMarketMaker.h"

#include <algorithm>
#include <cmath>

NaiveMarketMaker::NaiveMarketMaker(Simulator& simulator, Exchange& exchange, Portfolio& portfolio, MarketMakerConfig config) :
    simulator_(simulator),
    exchange_(exchange),
    portfolio_(portfolio),
    config_(config)
{}

void NaiveMarketMaker::Start(SimTime firstRefresh){
    simulator_.Schedule(firstRefresh, [this]{ Refresh(); });
}

void NaiveMarketMaker::Refresh(){
    const auto bestBid = exchange_.Book().BestBid();
    const auto bestAsk = exchange_.Book().BestAsk();

    std::optional<Price> bidTarget, askTarget;
    Quantity bidSize = 0, askSize = 0;

    if(bestBid && bestAsk){ // without both sides there is no reference price: we stand aside
        const double mid = 0.5 * (static_cast<double>(*bestBid) + static_cast<double>(*bestAsk));
        // Symmetric around the mid: the first tick strictly below / above it. (floor/ceil would be asymmetric when the
        // mid is a whole number: forcing the ask above the bid made us improve the bid but only join the ask, i.e. a buying bias.)
        const Price bidPrice = static_cast<Price>(std::ceil(mid)) - 1 - config_.offsetTicks;
        const Price askPrice = static_cast<Price>(std::floor(mid)) + 1 + config_.offsetTicks;

        // inventory limit: the side that would push the position beyond +/- maxPosition shrinks, then disappears
        const std::int64_t position = portfolio_.Position();
        const std::int64_t buyRoom = std::max<std::int64_t>(0, config_.maxPosition - position);
        const std::int64_t sellRoom = std::max<std::int64_t>(0, config_.maxPosition + position);
        bidSize = static_cast<Quantity>(std::min<std::int64_t>(config_.quoteSize, buyRoom));
        askSize = static_cast<Quantity>(std::min<std::int64_t>(config_.quoteSize, sellRoom));

        if(bidSize > 0) bidTarget = bidPrice;
        if(askSize > 0) askTarget = askPrice;
    }

    const bool keepBid = StillValid(bid_, bidTarget, bidSize);
    const bool keepAsk = StillValid(ask_, askTarget, askSize);

    // cancel everything that is stale BEFORE placing anything: a new bid placed above our old ask
    // would otherwise be stopped by self-trade prevention
    if(!keepBid) Drop(bid_);
    if(!keepAsk) Drop(ask_);
    if(!keepBid && bidTarget) Place(Side::Buy, *bidTarget, bidSize, bid_);
    if(!keepAsk && askTarget) Place(Side::Sell, *askTarget, askSize, ask_);

    simulator_.ScheduleAfter(config_.quoteInterval, [this]{ Refresh(); });
}

bool NaiveMarketMaker::StillValid(const Quote& quote, std::optional<Price> target, Quantity desired) const{
    if(quote.id == 0 || !target) return false;
    if(!exchange_.Book().Contains(quote.id)) return false;
    const auto remaining = portfolio_.Remaining(quote.id);
    // keep a partially filled quote as long as it is not larger than what the inventory limit allows now
    return remaining && quote.price == *target && *remaining <= desired;
}

void NaiveMarketMaker::Drop(Quote& quote){
    if(quote.id == 0) return;
    if(exchange_.Book().Contains(quote.id)) exchange_.Cancel(quote.id);
    portfolio_.ForgetOrder(quote.id);
    quote.id = 0;
}

void NaiveMarketMaker::Place(Side side, Price price, Quantity quantity, Quote& quote){
    const OrderId id = exchange_.NextOrderId();
    portfolio_.RegisterOrder(id, side, quantity); // before Submit: the order may trade immediately
    exchange_.Submit(std::make_shared<Order>(OrderType::GoodTillCancel, id, side, price, quantity, config_.participantId));

    if(exchange_.Book().Contains(id)){
        quote.id = id;
        quote.price = price;
    }else{ // fully filled on arrival, or stopped by self-trade prevention
        portfolio_.ForgetOrder(id);
        quote.id = 0;
    }
}
