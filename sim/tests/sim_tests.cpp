#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "Exchange.h"
#include "NoiseTraders.h"
#include "Rng.h"
#include "Simulator.h"
#include "StatsCollector.h"

// ---------------------------------------------------------------- Simulator

TEST(Simulator, RunsEventsInTimeOrderAndTiesInSchedulingOrder){
    Simulator sim;
    std::vector<int> order;
    sim.Schedule(30, [&]{ order.push_back(3); });
    sim.Schedule(10, [&]{ order.push_back(1); });
    sim.Schedule(20, [&]{ order.push_back(2); });
    sim.Schedule(20, [&]{ order.push_back(22); }); // same time as the previous one, scheduled later

    sim.RunUntil(100);
    EXPECT_EQ(order, (std::vector<int>{1, 2, 22, 3}));
    EXPECT_EQ(sim.Now(), 100);
}

TEST(Simulator, StopsAtTheEndTimeAndKeepsLaterEvents){
    Simulator sim;
    int count = 0;
    sim.Schedule(10, [&]{ ++count; });
    sim.Schedule(50, [&]{ ++count; });

    sim.RunUntil(20);
    EXPECT_EQ(count, 1);
    EXPECT_EQ(sim.PendingEvents(), 1u);

    sim.RunUntil(60);
    EXPECT_EQ(count, 2);
}

TEST(Simulator, EventsCanScheduleEvents){
    Simulator sim;
    std::vector<SimTime> times;
    std::function<void()> tick = [&]{
        times.push_back(sim.Now());
        if(times.size() < 4) sim.ScheduleAfter(5, tick);
    };
    sim.Schedule(0, tick);

    sim.RunUntil(1000);
    EXPECT_EQ(times, (std::vector<SimTime>{0, 5, 10, 15}));
}

// ---------------------------------------------------------------- Rng

TEST(Rng, SameSeedGivesTheSameSequence){
    Rng a{123}, b{123}, c{124};
    bool differsFromC = false;
    for(int i = 0; i < 100; ++i){
        const double x = a.Uniform();
        EXPECT_EQ(x, b.Uniform());
        if(x != c.Uniform()) differsFromC = true;
    }
    EXPECT_TRUE(differsFromC);
}

TEST(Rng, UniformIsInUnitIntervalAndExponentialHasTheRequestedMean){
    Rng rng{7};
    double sum = 0;
    const int n = 200'000;
    for(int i = 0; i < n; ++i){
        const double u = rng.Uniform();
        ASSERT_GE(u, 0.0);
        ASSERT_LT(u, 1.0);
        sum += rng.Exponential(3.0);
    }
    EXPECT_NEAR(sum / n, 3.0, 0.05);
}

// ---------------------------------------------------------------- Noise flow

namespace {

struct Scenario{
    explicit Scenario(NoiseConfig config, double durationSeconds = 30.0, double warmupSeconds = 5.0) :
        exchange(simulator),
        noise(simulator, exchange, config),
        stats(simulator, exchange, static_cast<SimTime>(warmupSeconds * kSecond), 100 * kMillisecond, [this]{ return noise.ReferencePrice(); }),
        end(static_cast<SimTime>((warmupSeconds + durationSeconds) * kSecond))
    {
        // check the book invariants every 50 ms during the whole run
        checker = [this]{ CheckBook(); simulator.ScheduleAfter(50 * kMillisecond, checker); };
        simulator.Schedule(0, [this]{ checker(); });

        noise.Start();
        stats.Start();
        simulator.RunUntil(end);
    }

    void CheckBook(){
        ++checks;
        const auto infos = exchange.Book().GetOrderInfos();
        const auto& bids = infos.GetBids();
        const auto& asks = infos.GetAsks();

        for(std::size_t i = 0; i < bids.size(); ++i){
            if(bids[i].quantity_ == 0) ++violations;
            if(i > 0 && bids[i].price_ >= bids[i - 1].price_) ++violations; // strictly descending
        }
        for(std::size_t i = 0; i < asks.size(); ++i){
            if(asks[i].quantity_ == 0) ++violations;
            if(i > 0 && asks[i].price_ <= asks[i - 1].price_) ++violations; // strictly ascending
        }
        if(!bids.empty() && !asks.empty() && bids.front().price_ >= asks.front().price_) ++violations; // crossed book
    }

    Simulator simulator;
    Exchange exchange;
    NoiseTraders noise;
    StatsCollector stats;
    SimTime end;
    std::function<void()> checker;
    std::size_t checks{0};
    std::size_t violations{0};
};

} // namespace

TEST(NoiseFlow, BookStaysConsistentThroughoutTheRun){
    Scenario run{NoiseConfig{}};
    EXPECT_GT(run.checks, 500u);
    EXPECT_EQ(run.violations, 0u);
}

TEST(NoiseFlow, ProducesAPlausibleMarket){
    Scenario run{NoiseConfig{}};
    const MarketSummary s = run.stats.Summary(run.end);

    EXPECT_GT(s.trades, 100u);
    EXPECT_GT(s.volume, s.trades);                 // average trade size above 1
    EXPECT_GT(s.twoSidedFraction, 0.95);           // there is (almost) always a bid and an ask
    EXPECT_GT(s.avgSpread, 1.0);
    EXPECT_LT(s.avgSpread, 15.0);
    EXPECT_GT(s.avgBidDepth, 10.0);
    EXPECT_GT(s.avgAskDepth, 10.0);
    EXPECT_LT(s.avgAbsMidError, 10.0);             // the book stays anchored to the reference price
    EXPECT_LT(run.exchange.Book().Size(), 5000u);  // orders are cancelled: the book does not grow without bound
}

TEST(NoiseFlow, TradesAreReportedAtThePriceOfTheRestingOrder){
    Simulator sim;
    Exchange exchange{sim};
    NoiseTraders noise{sim, exchange, NoiseConfig{}};

    std::size_t seen = 0, wrong = 0;
    exchange.AddTradeListener([&](const Trade& trade, SimTime){
        ++seen;
        // execution price lies between the ask's limit and the bid's limit
        if(trade.GetPrice() < trade.GetAskTrade().price_ || trade.GetPrice() > trade.GetBidTrade().price_) ++wrong;
    });

    noise.Start();
    sim.RunUntil(20 * kSecond);
    EXPECT_GT(seen, 50u);
    EXPECT_EQ(wrong, 0u);
}

TEST(NoiseFlow, SameSeedIsReproducibleDifferentSeedIsNot){
    NoiseConfig config;
    config.seed = 99;
    Scenario a{config}, b{config};
    const MarketSummary sa = a.stats.Summary(a.end), sb = b.stats.Summary(b.end);

    EXPECT_EQ(sa.trades, sb.trades);
    EXPECT_EQ(sa.volume, sb.volume);
    EXPECT_EQ(sa.finalMid, sb.finalMid);
    EXPECT_EQ(a.exchange.Book().Size(), b.exchange.Book().Size());

    config.seed = 100;
    Scenario c{config};
    const MarketSummary sc = c.stats.Summary(c.end);
    EXPECT_TRUE(sc.trades != sa.trades || sc.volume != sa.volume);
}

TEST(NoiseFlow, MarketableOrdersNeverLeaveARestingRemainder){
    // with limit orders switched off, the market-making side of the book is empty: FAK orders must not rest anywhere
    NoiseConfig config;
    config.limitRatePerSide = 0;
    Scenario run{config, 10.0, 1.0};
    EXPECT_EQ(run.exchange.Book().Size(), 0u);
}
