#pragma once

#include <atomic>
#include <map>
#include <unordered_map>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <thread>

#include "Usings.h"
#include "Order.h"
#include "OrderModify.h"
#include "OrderbookLevelInfos.h"
#include "Trade.h"

class Orderbook{
private:
    struct OrderEntry{
        OrderPointer order_{nullptr};
        OrderPointers::iterator location_;
    };

    struct LevelData{
        Quantity quantity_{};
        Quantity count_{};

        enum class Action{
            Add,
            Remove,
            Match
        };
    };

    std::unordered_map<Price, LevelData> data_;
    std::map<Price, OrderPointers, std::greater<Price>> bids_; //storing the highest bid (best bid) first
    std::map<Price, OrderPointers, std::less<Price>> asks_; //storing the lowest ask (best ask) first
    std::unordered_map<OrderId, OrderEntry> orders_;
    mutable std::mutex ordersMutex_;
    std::thread ordersPruneThread_; //only started when autoPruneGoodForDay is true
    std::condition_variable shutdownConditionVariable_;
    std::atomic<bool> shutdown_{false};

    void PruneGoodForDayOrders(); //body of the prune thread (wall-clock based)

    //*Internal methods assume ordersMutex_ is ALREADY held by the caller
    Trades AddOrderInternal(OrderPointer order);
    void CancelOrderInternal(OrderId orderId);
    void CancelOrders(OrderIds orderIds);

    //Useful for FillOrKill
    void OnOrderCancelled(OrderPointer order);
    void OnOrderAdded(OrderPointer order);
    void OnOrderMatched(Price price, Quantity quantity, bool isFullyFilled);
    void UpdateLevelData(Price price, Quantity quantity, LevelData::Action action);

    bool CanFullyFill(Side side, Price price, Quantity quantity) const;
    bool CanMatch(Side side, Price price) const;
    Trades MatchOrders(const OrderPointer& incoming);

public:
    //autoPruneGoodForDay = true  : a background thread cancels GoodForDay orders at 16:00 local time (live use).
    //autoPruneGoodForDay = false : no thread, deterministic; call CancelGoodForDayOrders() yourself at your simulated end of day (backtests).
    explicit Orderbook(bool autoPruneGoodForDay = true);
    Orderbook(const Orderbook&) = delete;
    void operator=(const Orderbook&) = delete;
    Orderbook(Orderbook&&) = delete;
    void operator=(Orderbook&&) = delete;
    ~Orderbook();

    Trades AddOrder(OrderPointer order);
    void CancelOrder(OrderId orderId);
    Trades ModifyOrder(OrderModify order); //atomic cancel/replace (the replacement goes to the back of the queue)
    void CancelGoodForDayOrders();

    bool Contains(OrderId orderId) const;
    std::optional<Price> BestBid() const;
    std::optional<Price> BestAsk() const;
    std::size_t Size() const;
    OrderbookLevelInfos GetOrderInfos() const;
};
