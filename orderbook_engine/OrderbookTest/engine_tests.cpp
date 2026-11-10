#include "pch.h"
#include "../Orderbook.h"

// Direct C++ tests for the behaviours a market-making layer relies on.
// Each one targets a bug that existed in the first version of the engine (noted in the comments).

namespace {

OrderPointer MakeOrder(OrderType type, OrderId id, Side side, Price price, Quantity qty, ParticipantId participant = 0){
    return std::make_shared<Order>(type, id, side, price, qty, participant);
}

OrderPointer Gtc(OrderId id, Side side, Price price, Quantity qty, ParticipantId participant = 0){
    return MakeOrder(OrderType::GoodTillCancel, id, side, price, qty, participant);
}

} // namespace

// Bug: trades used to be built from `bid`/`ask` references that dangled after pop_front() (use-after-free).
// Run with -DENABLE_SANITIZERS=ON to catch a regression with AddressSanitizer.
TEST(Matching, TradeInfoIsValidWhenBothOrdersAreFullyFilled){
    Orderbook book{false};
    book.AddOrder(Gtc(1, Side::Buy, 100, 10));
    const Trades trades = book.AddOrder(Gtc(2, Side::Sell, 100, 10));

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].GetBidTrade().orderId_, 1u);
    EXPECT_EQ(trades[0].GetAskTrade().orderId_, 2u);
    EXPECT_EQ(trades[0].GetQuantity(), 10u);
    EXPECT_EQ(book.Size(), 0u);
}

// Bug: the trade carried each order's own limit price; the execution price must be the resting order's price.
TEST(Matching, ExecutionPriceIsTheRestingOrderPrice){
    Orderbook book{false};

    book.AddOrder(Gtc(1, Side::Buy, 100, 10));                       // resting bid @100
    Trades trades = book.AddOrder(Gtc(2, Side::Sell, 95, 10));       // aggressive ask @95
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].GetPrice(), 100);

    book.AddOrder(Gtc(3, Side::Sell, 100, 10));                      // resting ask @100
    trades = book.AddOrder(Gtc(4, Side::Buy, 105, 10));              // aggressive bid @105
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].GetPrice(), 100);
}

TEST(Matching, SweepsSeveralLevelsAtEachRestingPrice){
    Orderbook book{false};
    book.AddOrder(Gtc(1, Side::Sell, 100, 5));
    book.AddOrder(Gtc(2, Side::Sell, 101, 5));

    const Trades trades = book.AddOrder(Gtc(3, Side::Buy, 101, 10));
    ASSERT_EQ(trades.size(), 2u);
    EXPECT_EQ(trades[0].GetPrice(), 100);
    EXPECT_EQ(trades[1].GetPrice(), 101);
    EXPECT_EQ(book.Size(), 0u);
}

TEST(Matching, TimePriorityWithinALevel){
    Orderbook book{false};
    book.AddOrder(Gtc(1, Side::Buy, 100, 5));
    book.AddOrder(Gtc(2, Side::Buy, 100, 5));

    const Trades trades = book.AddOrder(Gtc(3, Side::Sell, 100, 5));
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].GetBidTrade().orderId_, 1u); // first in, first out
    EXPECT_TRUE(book.Contains(2));
}

// Bug: MatchOrders called CancelOrder() while the (non-recursive) mutex was already held -> deadlock.
// (ctest kills the test after 30 s if it hangs.)
TEST(FillAndKill, PartiallyFilledRemainderIsCancelled){
    Orderbook book{false};
    book.AddOrder(Gtc(1, Side::Buy, 100, 5));

    const Trades trades = book.AddOrder(MakeOrder(OrderType::FillAndKill, 2, Side::Sell, 100, 10));
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].GetQuantity(), 5u);
    EXPECT_EQ(book.Size(), 0u);
}

TEST(FillAndKill, RejectedWhenNothingToMatch){
    Orderbook book{false};
    book.AddOrder(Gtc(1, Side::Buy, 100, 5));

    EXPECT_TRUE(book.AddOrder(MakeOrder(OrderType::FillAndKill, 2, Side::Sell, 101, 5)).empty());
    EXPECT_EQ(book.Size(), 1u);
}

// Bug: CanFullyFill mixed && and || without parentheses.
TEST(FillOrKill, FillsAcrossLevelsWithinItsLimitPrice){
    Orderbook book{false};
    book.AddOrder(Gtc(1, Side::Sell, 100, 5));
    book.AddOrder(Gtc(2, Side::Sell, 101, 5));

    const Trades trades = book.AddOrder(MakeOrder(OrderType::FillOrKill, 3, Side::Buy, 101, 10));
    EXPECT_EQ(trades.size(), 2u);
    EXPECT_EQ(book.Size(), 0u);
}

TEST(FillOrKill, RejectedIfNotEnoughQuantityWithinLimitPrice){
    Orderbook book{false};
    book.AddOrder(Gtc(1, Side::Sell, 100, 5));
    book.AddOrder(Gtc(2, Side::Sell, 102, 5)); // beyond the limit price of 101

    EXPECT_TRUE(book.AddOrder(MakeOrder(OrderType::FillOrKill, 3, Side::Buy, 101, 10)).empty());
    EXPECT_EQ(book.Size(), 2u);

    EXPECT_TRUE(book.AddOrder(MakeOrder(OrderType::FillOrKill, 4, Side::Buy, 100, 6)).empty());
    EXPECT_EQ(book.Size(), 2u);
}

// Bug: MatchOrders erased data_[price] when a level emptied, even if the OTHER side kept a remainder at that same price.
TEST(FillOrKill, SeesTheRemainderLeftAfterAPartialMatchAtTheSamePrice){
    Orderbook book{false};
    book.AddOrder(Gtc(1, Side::Buy, 100, 5));
    book.AddOrder(Gtc(2, Side::Sell, 100, 10)); // 5 traded, 5 rest as an ask @100

    const Trades trades = book.AddOrder(MakeOrder(OrderType::FillOrKill, 3, Side::Buy, 100, 5));
    EXPECT_EQ(trades.size(), 1u);
    EXPECT_EQ(book.Size(), 0u);
}

TEST(SelfTradePrevention, IncomingOrderIsCancelledInsteadOfTradingWithItself){
    Orderbook book{false};
    book.AddOrder(Gtc(1, Side::Buy, 100, 10, /*participant*/ 7));

    const Trades trades = book.AddOrder(Gtc(2, Side::Sell, 100, 10, 7));
    EXPECT_TRUE(trades.empty());
    EXPECT_TRUE(book.Contains(1));
    EXPECT_FALSE(book.Contains(2));
    EXPECT_FALSE(book.BestAsk().has_value());
}

TEST(SelfTradePrevention, OthersAheadInTheQueueAreStillTradedBeforeStopping){
    Orderbook book{false};
    book.AddOrder(Gtc(1, Side::Buy, 100, 5, 8)); // someone else, first in the queue
    book.AddOrder(Gtc(2, Side::Buy, 100, 5, 7)); // our own order, behind

    const Trades trades = book.AddOrder(Gtc(3, Side::Sell, 100, 10, 7));
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].GetBidTrade().orderId_, 1u);
    EXPECT_TRUE(book.Contains(2));   // our resting bid is untouched
    EXPECT_FALSE(book.Contains(3));  // the remainder of the incoming ask is cancelled
}

TEST(SelfTradePrevention, AnonymousOrdersCanTradeTogether){
    Orderbook book{false};
    book.AddOrder(Gtc(1, Side::Buy, 100, 10)); // participant 0 = no STP
    EXPECT_EQ(book.AddOrder(Gtc(2, Side::Sell, 100, 10)).size(), 1u);
}

TEST(Modify, ReplacesAtomicallyAndCanCross){
    Orderbook book{false};
    book.AddOrder(Gtc(1, Side::Buy, 100, 10));
    book.AddOrder(Gtc(2, Side::Sell, 105, 10));

    const Trades trades = book.ModifyOrder(OrderModify{2, Side::Sell, 100, 10});
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].GetPrice(), 100);
    EXPECT_EQ(book.Size(), 0u);
}

TEST(Modify, UnknownOrderIsIgnored){
    Orderbook book{false};
    EXPECT_TRUE(book.ModifyOrder(OrderModify{42, Side::Buy, 100, 10}).empty());
    EXPECT_EQ(book.Size(), 0u);
}

TEST(Modify, KeepsTheParticipantOfTheReplacedOrder){
    Orderbook book{false};
    book.AddOrder(Gtc(1, Side::Buy, 99, 10, 7));
    book.AddOrder(Gtc(2, Side::Sell, 105, 10, 7));

    // moving our ask down to 99 would trade against our own bid: STP must still apply after the modify
    EXPECT_TRUE(book.ModifyOrder(OrderModify{2, Side::Sell, 99, 10}).empty());
    EXPECT_TRUE(book.Contains(1));
    EXPECT_FALSE(book.Contains(2));
}

TEST(Market, MarketOrderOnAnEmptySideIsDropped){
    Orderbook book{false};
    EXPECT_TRUE(book.AddOrder(std::make_shared<Order>(1, Side::Sell, 10)).empty());
    EXPECT_EQ(book.Size(), 0u);
}

TEST(Book, BestBidAndAsk){
    Orderbook book{false};
    EXPECT_FALSE(book.BestBid().has_value());
    EXPECT_FALSE(book.BestAsk().has_value());

    book.AddOrder(Gtc(1, Side::Buy, 99, 1));
    book.AddOrder(Gtc(2, Side::Buy, 98, 1));
    book.AddOrder(Gtc(3, Side::Sell, 101, 1));
    book.AddOrder(Gtc(4, Side::Sell, 102, 1));

    EXPECT_EQ(book.BestBid(), 99);
    EXPECT_EQ(book.BestAsk(), 101);
}

TEST(Book, ZeroQuantityAndDuplicateIdsAreIgnored){
    Orderbook book{false};
    EXPECT_TRUE(book.AddOrder(Gtc(1, Side::Buy, 100, 0)).empty());
    EXPECT_EQ(book.Size(), 0u);

    book.AddOrder(Gtc(2, Side::Buy, 100, 5));
    book.AddOrder(Gtc(2, Side::Buy, 101, 5)); // same id
    EXPECT_EQ(book.Size(), 1u);
    EXPECT_EQ(book.BestBid(), 100);
}

TEST(GoodForDay, CancelledOnDemandWithoutThread){
    Orderbook book{false}; // no wall-clock thread: the simulator decides when the day ends
    book.AddOrder(MakeOrder(OrderType::GoodForDay, 1, Side::Buy, 100, 5));
    book.AddOrder(Gtc(2, Side::Buy, 99, 5));

    book.CancelGoodForDayOrders();
    EXPECT_FALSE(book.Contains(1));
    EXPECT_TRUE(book.Contains(2));
}

TEST(GoodForDay, ThreadModeStartsAndStopsCleanly){
    Orderbook book{true}; // constructing and destroying must not hang
    book.AddOrder(Gtc(1, Side::Buy, 100, 5));
    EXPECT_EQ(book.Size(), 1u);
}
