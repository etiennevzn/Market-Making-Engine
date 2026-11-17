#pragma once

#include <functional>
#include <vector>

#include "Orderbook.h"
#include "Simulator.h"

// The "venue" every participant (noise traders, later the market maker) talks to.
// It wraps the Orderbook and broadcasts every trade, with its timestamp, to the listeners (stats, PnL tracking...).
// Trades that happen on a RESTING order are only visible here: AddOrder() returns them to the aggressor only.
class Exchange{
public:
    using TradeListener = std::function<void(const Trade&, SimTime)>;

    explicit Exchange(Simulator& simulator) : simulator_(simulator) {}

    Orderbook& Book() {return book_;}
    const Orderbook& Book() const {return book_;}

    OrderId NextOrderId() {return nextOrderId_++;}
    void AddTradeListener(TradeListener listener) {listeners_.push_back(std::move(listener));}

    Trades Submit(OrderPointer order){
        return Publish(book_.AddOrder(std::move(order)));
    }

    Trades Modify(OrderModify modify){
        return Publish(book_.ModifyOrder(std::move(modify)));
    }

    void Cancel(OrderId orderId){
        book_.CancelOrder(orderId);
    }

private:
    Trades Publish(Trades trades){
        for(const auto& trade : trades)
            for(const auto& listener : listeners_) listener(trade, simulator_.Now());
        return trades;
    }

    Simulator& simulator_;
    Orderbook book_{false}; //no wall-clock thread: simulated time only
    OrderId nextOrderId_{1};
    std::vector<TradeListener> listeners_;
};
