#include <gtest/gtest.h>

#include <cmath>

#include "Exchange.h"
#include "NaiveMarketMaker.h"
#include "NoiseTraders.h"
#include "Portfolio.h"
#include "Simulator.h"

namespace {

OrderPointer Limit(OrderId id, Side side, Price price, Quantity quantity, ParticipantId participant = 0){
    return std::make_shared<Order>(OrderType::GoodTillCancel, id, side, price, quantity, participant);
}

Quantity QuantityAt(const LevelInfos& levels, Price price){
    for(const auto& level : levels) if(level.price_ == price) return level.quantity_;
    return 0;
}

} // namespace

// ---------------------------------------------------------------- accounting (average cost)

TEST(Portfolio, RoundTripRealizesTheDifference){
    Simulator sim; Exchange ex{sim}; Portfolio pf{sim, ex};
    pf.ApplyFill(Side::Buy, 100, 10);
    pf.ApplyFill(Side::Sell, 103, 10);

    EXPECT_EQ(pf.Position(), 0);
    EXPECT_DOUBLE_EQ(pf.RealizedPnl(), 30.0);
    EXPECT_DOUBLE_EQ(pf.UnrealizedPnl(500.0), 0.0);
    EXPECT_DOUBLE_EQ(pf.TotalPnl(500.0), 30.0); // flat: the mid no longer matters
}

TEST(Portfolio, AverageCostAndPartialClose){
    Simulator sim; Exchange ex{sim}; Portfolio pf{sim, ex};
    pf.ApplyFill(Side::Buy, 100, 10);
    pf.ApplyFill(Side::Buy, 102, 10);  // average cost 101
    pf.ApplyFill(Side::Sell, 104, 5);  // closes 5 units: (104 - 101) * 5

    EXPECT_EQ(pf.Position(), 15);
    EXPECT_DOUBLE_EQ(pf.RealizedPnl(), 15.0);
    EXPECT_DOUBLE_EQ(pf.UnrealizedPnl(105.0), 15 * (105.0 - 101.0));
    EXPECT_DOUBLE_EQ(pf.TotalPnl(105.0), 75.0);
}

TEST(Portfolio, ShortPosition){
    Simulator sim; Exchange ex{sim}; Portfolio pf{sim, ex};
    pf.ApplyFill(Side::Sell, 100, 10);
    pf.ApplyFill(Side::Buy, 97, 4);    // closes 4 short units at a gain of 3 each

    EXPECT_EQ(pf.Position(), -6);
    EXPECT_DOUBLE_EQ(pf.RealizedPnl(), 12.0);
    EXPECT_DOUBLE_EQ(pf.UnrealizedPnl(98.0), 12.0); // -6 * (98 - 100)
    EXPECT_DOUBLE_EQ(pf.TotalPnl(98.0), 24.0);
}

TEST(Portfolio, FlippingThePositionReopensAtTheFillPrice){
    Simulator sim; Exchange ex{sim}; Portfolio pf{sim, ex};
    pf.ApplyFill(Side::Buy, 100, 10);
    pf.ApplyFill(Side::Sell, 105, 25); // closes 10 long (+50), opens 15 short at 105

    EXPECT_EQ(pf.Position(), -15);
    EXPECT_DOUBLE_EQ(pf.RealizedPnl(), 50.0);
    EXPECT_DOUBLE_EQ(pf.UnrealizedPnl(104.0), 15.0); // -15 * (104 - 105)
}

// ---------------------------------------------------------------- fill recognition

TEST(Portfolio, RecognisesFillsOnRestingOrdersAtThePassivePrice){
    Simulator sim; Exchange ex{sim}; Portfolio pf{sim, ex};
    pf.RegisterOrder(1, Side::Buy, 10);
    ex.Submit(Limit(1, Side::Buy, 100, 10, 1));

    ex.Submit(Limit(2, Side::Sell, 95, 4));      // aggressive seller: trade at OUR price, 100
    EXPECT_EQ(pf.Position(), 4);
    EXPECT_DOUBLE_EQ(pf.TotalPnl(100.0), 0.0);   // bought 4 at 100, marked at 100
    EXPECT_EQ(pf.Remaining(1), 6u);

    ex.Submit(Limit(3, Side::Sell, 95, 6));
    EXPECT_EQ(pf.Position(), 10);
    EXPECT_FALSE(pf.Remaining(1).has_value());   // fully filled: no longer tracked
    ASSERT_EQ(pf.Fills().size(), 2u);
    EXPECT_EQ(pf.Fills()[0].price, 100);
    EXPECT_EQ(pf.Fills()[0].quantity, 4u);
}

TEST(Portfolio, RecognisesFillsOnOwnAsks){
    Simulator sim; Exchange ex{sim}; Portfolio pf{sim, ex};
    pf.RegisterOrder(1, Side::Sell, 5);
    ex.Submit(Limit(1, Side::Sell, 101, 5, 1));
    ex.Submit(Limit(2, Side::Buy, 105, 3));

    EXPECT_EQ(pf.Position(), -3);
    EXPECT_DOUBLE_EQ(pf.TotalPnl(101.0), 0.0);
    EXPECT_EQ(pf.Fills().front().side, Side::Sell);
    EXPECT_EQ(pf.Fills().front().price, 101);
}

TEST(Portfolio, IgnoresOrdersThatWereNotRegistered){
    Simulator sim; Exchange ex{sim}; Portfolio pf{sim, ex};
    ex.Submit(Limit(1, Side::Buy, 100, 5));
    ex.Submit(Limit(2, Side::Sell, 100, 5));
    EXPECT_EQ(pf.Position(), 0);
    EXPECT_TRUE(pf.Fills().empty());
}

TEST(Portfolio, MarkoutMeasuresTheMidAfterTheFill){
    Simulator sim; Exchange ex{sim}; Portfolio pf{sim, ex};
    ex.Submit(Limit(10, Side::Buy, 99, 5));      // other bid stays after our bid is consumed
    ex.Submit(Limit(11, Side::Sell, 103, 5));    // other ask
    pf.RegisterOrder(1, Side::Buy, 5);
    ex.Submit(Limit(1, Side::Buy, 100, 5, 1));   // our bid, best bid
    ex.Submit(Limit(2, Side::Sell, 100, 5));     // someone sells into it: we buy 5 at 100

    sim.RunUntil(2 * kSecond);
    EXPECT_NEAR(pf.Summary().markout1s, 1.0, 1e-9); // mid is now (99 + 103) / 2 = 101: +1 per unit
    sim.RunUntil(6 * kSecond);
    EXPECT_NEAR(pf.Summary().markout5s, 1.0, 1e-9);
}

// ---------------------------------------------------------------- market maker

TEST(NaiveMarketMaker, QuotesAroundTheMidAndRespectsTheOffset){
    for(Price offset : {0, 1}){
        Simulator sim; Exchange ex{sim}; Portfolio pf{sim, ex};
        ex.Submit(Limit(1000, Side::Buy, 9997, 100));
        ex.Submit(Limit(1001, Side::Sell, 9998, 100));

        MarketMakerConfig config;
        config.offsetTicks = offset;
        NaiveMarketMaker maker{sim, ex, pf, config};
        maker.Start(0);
        sim.RunUntil(50 * kMillisecond);

        const auto infos = ex.Book().GetOrderInfos();
        // mid = 9997.5: bid = 9997 - offset, ask = 9998 + offset
        EXPECT_EQ(QuantityAt(infos.GetBids(), 9997 - offset), offset == 0 ? 110u : 10u) << "offset " << offset;
        EXPECT_EQ(QuantityAt(infos.GetAsks(), 9998 + offset), offset == 0 ? 110u : 10u) << "offset " << offset;
        EXPECT_EQ(ex.Book().Size(), 4u);
    }
}

TEST(NaiveMarketMaker, StandsAsideWhenTheBookIsOneSided){
    Simulator sim; Exchange ex{sim}; Portfolio pf{sim, ex};
    ex.Submit(Limit(1000, Side::Buy, 9997, 100));

    NaiveMarketMaker maker{sim, ex, pf, MarketMakerConfig{}};
    maker.Start(0);
    sim.RunUntil(kSecond);
    EXPECT_EQ(ex.Book().Size(), 1u);
}

TEST(NaiveMarketMaker, DropsTheQuoteThatWouldBreakTheInventoryLimit){
    Simulator sim; Exchange ex{sim}; Portfolio pf{sim, ex};
    ex.Submit(Limit(1000, Side::Buy, 9997, 100));
    ex.Submit(Limit(1001, Side::Sell, 9998, 100));
    pf.ApplyFill(Side::Buy, 9990, 10); // already long 10 = the limit

    MarketMakerConfig config;
    config.maxPosition = 10;
    config.quoteSize = 10;
    NaiveMarketMaker maker{sim, ex, pf, config};
    maker.Start(0);
    sim.RunUntil(50 * kMillisecond);

    const auto infos = ex.Book().GetOrderInfos();
    EXPECT_EQ(QuantityAt(infos.GetBids(), 9997), 100u); // no bid from us
    EXPECT_EQ(QuantityAt(infos.GetAsks(), 9998), 110u); // the ask that reduces the position is there
}

namespace {

struct MarketMakingRun{
    explicit MarketMakingRun(std::uint64_t seed, MarketMakerConfig mmConfig = {}, double durationSeconds = 60.0) :
        exchange(simulator),
        noise(simulator, exchange, MakeNoise(seed)),
        portfolio(simulator, exchange),
        maker(simulator, exchange, portfolio, mmConfig)
    {
        noise.Start();
        maker.Start(5 * kSecond);
        simulator.Schedule(5 * kSecond, [this]{ portfolio.StartSampling(kSecond); });
        simulator.RunUntil(static_cast<SimTime>((5.0 + durationSeconds) * static_cast<double>(kSecond)));
    }

    static NoiseConfig MakeNoise(std::uint64_t seed){ NoiseConfig c; c.seed = seed; return c; }

    Simulator simulator;
    Exchange exchange;
    NoiseTraders noise;
    Portfolio portfolio;
    NaiveMarketMaker maker;
};

} // namespace

TEST(NaiveMarketMaker, StaysWithinTheInventoryLimitAndNeverTradesWithItself){
    MarketMakerConfig config;
    config.maxPosition = 25; // tight on purpose: the limit is hit often
    config.quoteSize = 10;
    MarketMakingRun run{42, config, 120.0};

    const PortfolioSummary s = run.portfolio.Summary();
    EXPECT_GT(s.fills, 50u);
    EXPECT_LE(s.maxAbsPosition, 25);
    EXPECT_EQ(s.selfTrades, 0u);
}

TEST(NaiveMarketMaker, PnlIsConsistentWithPositionAndCash){
    MarketMakingRun run{7};
    const double mid = run.portfolio.CurrentMid();
    ASSERT_FALSE(std::isnan(mid));

    // total (cash + position * mid) must equal realized + unrealized
    EXPECT_NEAR(run.portfolio.TotalPnl(mid), run.portfolio.RealizedPnl() + run.portfolio.UnrealizedPnl(mid), 1e-6);
    EXPECT_GT(run.portfolio.Summary().volume, 0u);
}

TEST(NaiveMarketMaker, IsReproducible){
    MarketMakingRun a{123}, b{123};
    const PortfolioSummary sa = a.portfolio.Summary(), sb = b.portfolio.Summary();
    EXPECT_EQ(sa.fills, sb.fills);
    EXPECT_EQ(sa.volume, sb.volume);
    EXPECT_EQ(sa.position, sb.position);
    EXPECT_DOUBLE_EQ(sa.totalPnl, sb.totalPnl);
}
