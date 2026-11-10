#pragma once

#include <vector>

#include "TradeInfo.h"

class Trade{
public:
    // bidTrade / askTrade carry each order's own limit price.
    // executionPrice is the price the trade actually happened at: the price of the RESTING (maker) order.
    Trade(const TradeInfo& bidTrade, const TradeInfo& askTrade, Price executionPrice): bidTrade_(bidTrade), askTrade_(askTrade), executionPrice_(executionPrice){}
    const TradeInfo& GetBidTrade() const {return bidTrade_;}
    const TradeInfo& GetAskTrade() const {return askTrade_;}
    Price GetPrice() const {return executionPrice_;}
    Quantity GetQuantity() const {return bidTrade_.quantity_;}
    
private:
    TradeInfo bidTrade_;
    TradeInfo askTrade_;
    Price executionPrice_;
};

using Trades = std::vector<Trade>;
