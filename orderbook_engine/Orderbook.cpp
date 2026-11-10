#include "Orderbook.h"
#include <ctime>
#include <chrono>

namespace {
    //localtime_s exists only on Windows (MSVC); POSIX has localtime_r
    std::tm ToLocalTime(std::time_t t){
        std::tm parts{};
#ifdef _WIN32
        localtime_s(&parts, &t);
#else
        localtime_r(&t, &parts);
#endif
        return parts;
    }
}

void Orderbook::PruneGoodForDayOrders(){    
    using namespace std::chrono;
    const auto end = hours(16);

	while (true){
		const auto now = system_clock::now();
		std::tm now_parts = ToLocalTime(system_clock::to_time_t(now));

		if (now_parts.tm_hour >= end.count()) now_parts.tm_mday += 1;

		now_parts.tm_hour = end.count();
		now_parts.tm_min = 0;
		now_parts.tm_sec = 0;
		now_parts.tm_isdst = -1;

		auto next = system_clock::from_time_t(std::mktime(&now_parts));
		auto till = next - now + milliseconds(100);

		{
			std::unique_lock ordersLock{ordersMutex_};

			//wait with a predicate: a spurious wake-up no longer kills the thread
			if (shutdownConditionVariable_.wait_for(ordersLock, till, [this]{ return shutdown_.load(std::memory_order_acquire); })) 
                return;
		}

		CancelGoodForDayOrders();
	}
}

void Orderbook::CancelGoodForDayOrders(){
    std::scoped_lock ordersLock{ordersMutex_};

    OrderIds orderIds;
    for (const auto& [id, entry] : orders_){
        if (entry.order_->GetOrderType() == OrderType::GoodForDay) orderIds.push_back(id);
    }

    for (const auto& orderId : orderIds) CancelOrderInternal(orderId);
}

bool Orderbook::CanMatch(Side side, Price price) const{ //checks if an order can be matched immediately
    if(side == Side::Buy){
        if(asks_.empty()) return false;
        return price >= asks_.begin()->first; //best ask
    }else{
        if(bids_.empty()) return false;
        return price <= bids_.begin()->first; //best bid
    }
}

//Called right after `incoming` was inserted in the book. Since the book was not crossed before that insertion, 
//`incoming` is the aggressor of every trade generated here, and the other order is the resting (maker) one.
Trades Orderbook::MatchOrders(const OrderPointer& incoming){
    Trades trades;
    bool selfTrade = false;

    while(!selfTrade && !bids_.empty() && !asks_.empty()){
        const auto bidLevel = bids_.begin();
        const auto askLevel = asks_.begin();
        const Price bidPrice = bidLevel->first;
        const Price askPrice = askLevel->first;

        if(bidPrice < askPrice) break;

        auto& bids = bidLevel->second;
        auto& asks = askLevel->second;

        while(!bids.empty() && !asks.empty()){
            //COPIES of the shared_ptr (not references to the list elements): they must stay valid after pop_front()
            OrderPointer bid = bids.front();
            OrderPointer ask = asks.front();

            //self-trade prevention (cancel-newest): never match two orders of the same participant
            if(bid->GetParticipantId() != Constants::NoParticipant && bid->GetParticipantId() == ask->GetParticipantId()){
                selfTrade = true;
                break;
            }

            //a trade happens at the price of the resting order, not of the aggressor
            const Price executionPrice = (bid == incoming) ? ask->GetPrice() : bid->GetPrice();

            const Quantity quantity = std::min(bid->GetRemainingQuantity(), ask->GetRemainingQuantity());
            bid->Fill(quantity);
            ask->Fill(quantity);

            if(bid->isFilled()){
                bids.pop_front();
                orders_.erase(bid->GetOrderId());
            }

            if(ask->isFilled()){
                asks.pop_front();
                orders_.erase(ask->GetOrderId());
            }

            trades.push_back(Trade{
                TradeInfo{bid->GetOrderId(), bid->GetPrice(), quantity}, 
                TradeInfo{ask->GetOrderId(), ask->GetPrice(), quantity},
                executionPrice
            });

            OnOrderMatched(bid->GetPrice(), quantity, bid->isFilled());
            OnOrderMatched(ask->GetPrice(), quantity, ask->isFilled());
        }

        //levels are erased through their iterator (the old code erased by a key reference that dangled right after the erase).
        //data_ is NOT touched here: UpdateLevelData already drops a price once its order count reaches 0 
        //(erasing it here would wipe the quantity of the other side when a bid and an ask share the same price)
        if(bids.empty()) bids_.erase(bidLevel);
        if(asks.empty()) asks_.erase(askLevel);
    }

    if(selfTrade && orders_.contains(incoming->GetOrderId())) CancelOrderInternal(incoming->GetOrderId());

    return trades;
}

void Orderbook::OnOrderMatched(Price price, Quantity quantity, bool isFullyFilled)
{
	UpdateLevelData(price, quantity, isFullyFilled ? LevelData::Action::Remove : LevelData::Action::Match);
}

Orderbook::Orderbook(bool autoPruneGoodForDay){
    if(autoPruneGoodForDay) ordersPruneThread_ = std::thread{[this] {PruneGoodForDayOrders();}};
}

Orderbook::~Orderbook(){
    {
        //set the flag under the mutex, otherwise the notification can be lost between the predicate check and the wait
        std::scoped_lock ordersLock{ordersMutex_};
        shutdown_.store(true, std::memory_order_release);
    }
	shutdownConditionVariable_.notify_all();
	if(ordersPruneThread_.joinable()) ordersPruneThread_.join();
}

Trades Orderbook::AddOrder(OrderPointer order){
    std::scoped_lock ordersLock{ordersMutex_};
    return AddOrderInternal(std::move(order));
}

Trades Orderbook::AddOrderInternal(OrderPointer order){
    if(order->GetInitialQuantity() == 0) return {};
    if(orders_.contains(order->GetOrderId())) return {};

    //if we can match the market order side, we transform it into a GoodTillCancelOrder that will be 
    //filled, in the worst case, at the worst price currently present in the orderbook. 
    if(order->GetOrderType() == OrderType::Market){
        if(order->GetSide() == Side::Buy && !asks_.empty()){
            const auto& [worstAsk, _] = *asks_.rbegin();
            order->ToGoodTillCancel(worstAsk); 
        }else if(order->GetSide() == Side::Sell && !bids_.empty()){
            const auto& [worstBid, _] = *bids_.rbegin();
            order->ToGoodTillCancel(worstBid);
        }else return {};
    }

    if(order->GetOrderType() == OrderType::FillAndKill && !CanMatch(order->GetSide(), order->GetPrice())) return {};
    //known limitation: CanFullyFill counts the participant's own resting liquidity, so a FillOrKill can still be partially executed if self-trade prevention stops it
    if(order->GetOrderType() == OrderType::FillOrKill && !CanFullyFill(order->GetSide(), order->GetPrice(), order->GetInitialQuantity())) return {};

    OrderPointers::iterator iterator;
    if(order->GetSide() == Side::Buy){
        auto& orders = bids_[order->GetPrice()];
        orders.push_back(order);
        iterator = std::prev(orders.end()); //iterator on the order we just added, stored into the OrderEntry
    }else{
        auto& orders = asks_[order->GetPrice()];
        orders.push_back(order);
        iterator = std::prev(orders.end()); 
    }

    orders_.insert({order->GetOrderId(), OrderEntry{order, iterator}});
    OnOrderAdded(order);

    Trades trades = MatchOrders(order);

    //FillAndKill: whatever could not be filled immediately is cancelled
    //(CancelOrderInternal, not CancelOrder: the mutex is already held, relocking it would deadlock)
    if(order->GetOrderType() == OrderType::FillAndKill && orders_.contains(order->GetOrderId())) CancelOrderInternal(order->GetOrderId());

    return trades;
}

void Orderbook::OnOrderAdded(OrderPointer order)
{
	UpdateLevelData(order->GetPrice(), order->GetInitialQuantity(), LevelData::Action::Add);
}

//Cancelling one or several orders holds the mutex only once (CancelOrderInternal expects the mutex to be held)

void Orderbook::CancelOrderInternal(OrderId orderId){
    const auto it = orders_.find(orderId);
    if(it == orders_.end()) return;

    const OrderPointer order = it->second.order_;
    const auto orderIterator = it->second.location_;
    orders_.erase(it);

    const auto price = order->GetPrice();
    if(order->GetSide() == Side::Sell){
        auto& orders = asks_.at(price);
        orders.erase(orderIterator);
        if(orders.empty()) asks_.erase(price);
    }else{
        auto& orders = bids_.at(price);
        orders.erase(orderIterator);
        if(orders.empty()) bids_.erase(price);
    }

    OnOrderCancelled(order);
}

void Orderbook::CancelOrder(OrderId orderId)
{
	std::scoped_lock ordersLock{ordersMutex_};

	CancelOrderInternal(orderId);
}

void Orderbook::CancelOrders(OrderIds orderIds)
{
	std::scoped_lock ordersLock{ordersMutex_};

	for (const auto& orderId : orderIds) CancelOrderInternal(orderId);
}

void Orderbook::OnOrderCancelled(OrderPointer order)
{
	UpdateLevelData(order->GetPrice(), order->GetRemainingQuantity(), LevelData::Action::Remove);
}

void Orderbook::UpdateLevelData(Price price, Quantity quantity, LevelData::Action action)
{
	auto& data = data_[price];

	data.count_ += action == LevelData::Action::Remove ? -1 : action == LevelData::Action::Add ? 1 : 0;
	if (action == LevelData::Action::Remove || action == LevelData::Action::Match)
	{
		data.quantity_ -= quantity;
	}
	else
	{
		data.quantity_ += quantity;
	}

	if (data.count_ == 0) data_.erase(price);
}

//Can the order be filled entirely, only with levels at prices at least as good as its limit price?
//We walk the opposite side from the best level and add up the quantity available (data_ holds the quantity per price).
bool Orderbook::CanFullyFill(Side side, Price price, Quantity quantity) const
{
	if (!CanMatch(side, price)) return false;

	std::uint64_t available = 0;

	if (side == Side::Buy){
		for (const auto& [levelPrice, _] : asks_){
			if (levelPrice > price) break;
			available += data_.at(levelPrice).quantity_;
			if (available >= quantity) return true;
		}
	}else{
		for (const auto& [levelPrice, _] : bids_){
			if (levelPrice < price) break;
			available += data_.at(levelPrice).quantity_;
			if (available >= quantity) return true;
		}
	}

	return false;
}

Trades Orderbook::ModifyOrder(OrderModify order){
	//one single critical section: nobody can see (or trade against) the book between the cancel and the re-add
	std::scoped_lock ordersLock{ordersMutex_};

	const auto it = orders_.find(order.GetOrderId());
	if (it == orders_.end()) return {};

	const OrderType orderType = it->second.order_->GetOrderType();
	const ParticipantId participantId = it->second.order_->GetParticipantId();

	CancelOrderInternal(order.GetOrderId());
	return AddOrderInternal(order.ToOrderPointer(orderType, participantId));
}

bool Orderbook::Contains(OrderId orderId) const {
	std::scoped_lock ordersLock{ordersMutex_};
	return orders_.contains(orderId);
}

std::optional<Price> Orderbook::BestBid() const {
	std::scoped_lock ordersLock{ordersMutex_};
	if (bids_.empty()) return std::nullopt;
	return bids_.begin()->first;
}

std::optional<Price> Orderbook::BestAsk() const {
	std::scoped_lock ordersLock{ordersMutex_};
	if (asks_.empty()) return std::nullopt;
	return asks_.begin()->first;
}

std::size_t Orderbook::Size() const {
	std::scoped_lock ordersLock{ordersMutex_};
	return orders_.size(); 
}

OrderbookLevelInfos Orderbook::GetOrderInfos() const{
    std::scoped_lock ordersLock{ordersMutex_};

    LevelInfos bidInfos, askInfos;
    bidInfos.reserve(bids_.size());
    askInfos.reserve(asks_.size());

    auto CreateLevelInfos = [](Price price, const OrderPointers& orders){
        return LevelInfo{price, std::accumulate(orders.begin(), orders.end(), (Quantity)0, 
            [](Quantity runningSum, const OrderPointer& order){
                return runningSum + order->GetRemainingQuantity();
            })};
    };

    for(const auto& [price, orders] : bids_) bidInfos.push_back(CreateLevelInfos(price,orders));
    for(const auto& [price, orders] : asks_) askInfos.push_back(CreateLevelInfos(price,orders));

    return OrderbookLevelInfos{bidInfos, askInfos};
}
