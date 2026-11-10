#include <iostream>
#include "Orderbook.h"

int main() {
    Orderbook orderbook;
    
    // Add a buy order
    auto buyOrder = std::make_shared<Order>(
        OrderType::GoodTillCancel,
        1001,                    // Order ID
        Side::Buy,
        Price{100},              // Price: $100
        Quantity{50}             // Quantity: 50 shares
    );
    
    auto trades = orderbook.AddOrder(buyOrder);
    
    // Add a matching sell order
    auto sellOrder = std::make_shared<Order>(
        OrderType::GoodTillCancel,
        1002,
        Side::Sell,
        Price{100},
        Quantity{30}
    );
    
    trades = orderbook.AddOrder(sellOrder);
    
    // Expected : 
    // Orders in book: 1
    // Trades executed: 1 (30 @ 100)
    std::cout << "Orders in book: " << orderbook.Size() << std::endl;
    std::cout << "Trades executed: " << trades.size() << std::endl;
    if(!trades.empty()) std::cout << "First trade: " << trades[0].GetQuantity() << " @ " << trades[0].GetPrice() << std::endl;
    
    return 0;
}
