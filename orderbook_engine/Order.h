#pragma once

#include <list>
#include <memory>
#include <stdexcept>
#include <format>

#include "OrderType.h"
#include "Side.h"
#include "Usings.h"
#include "Constants.h"

class Order{
public:
    Order(OrderType orderType, OrderId orderId, Side side, Price price, Quantity quantity, ParticipantId participantId = Constants::NoParticipant): 
        orderType_(orderType), 
        orderId_(orderId),
        side_(side),
        price_(price),
        initialQuantity_(quantity),
        remainingQuantity_(quantity),
        participantId_(participantId)
    {}

    //constructor used for the market orders : no need to specify the price, we just want the order to be filled
    Order(OrderId orderId, Side side, Quantity quantity, ParticipantId participantId = Constants::NoParticipant) : Order(OrderType::Market, orderId, side, Constants::InvalidPrice, quantity, participantId){}

    OrderId GetOrderId() const {return orderId_;}
    Side GetSide() const {return side_;}
    Price GetPrice() const {return price_;}
    OrderType GetOrderType() const {return orderType_;}
    ParticipantId GetParticipantId() const {return participantId_;}
    Quantity GetInitialQuantity() const {return initialQuantity_;}
    Quantity GetRemainingQuantity() const {return remainingQuantity_;}
    Quantity GetFilledQuantity() const {return GetInitialQuantity() - GetRemainingQuantity();}
    bool isFilled() const {return GetRemainingQuantity() == 0;}

    void Fill(Quantity quantity){
        if(quantity > GetRemainingQuantity()) throw std::logic_error(std::format("Order ({}) cannot be filled for more than its remaining quantity.", GetOrderId()));
        remainingQuantity_ -= quantity;
    }

    void ToGoodTillCancel(Price price){ 
        if (GetOrderType() != OrderType::Market) throw std::logic_error(std::format("Order ({}) cannot have its price adjusted, only market orders can.", GetOrderId()));
        price_ = price;
        orderType_ = OrderType::GoodTillCancel;
    }

private:
    OrderType orderType_;
    OrderId orderId_;
    Side side_;
    Price price_;
    Quantity initialQuantity_;
    Quantity remainingQuantity_;
    ParticipantId participantId_;
};

using OrderPointer = std::shared_ptr<Order>;
using OrderPointers = std::list<OrderPointer>;
