#pragma once

#include <cstdint>

#include "Exchange.h"
#include "Rng.h"
#include "Simulator.h"

struct NoiseConfig{
    std::uint64_t seed = 42;
    Price initialFair = 10'000;       // reference price, in ticks
    double limitRatePerSide = 40.0;   // limit orders per second, per side
    double marketRatePerSide = 4.0;   // marketable orders per second, per side
    double jumpRate = 1.0;            // jumps of the reference price per second (+/- 1 tick)
    double meanLifetimeSec = 5.0;     // a limit order is cancelled after Exp(mean) seconds if still resting
    double meanDistanceTicks = 3.0;   // limit orders are placed at 1 + Exp(mean) ticks from the reference price
    double meanSize = 10.0;           // size = 1 + Exp(mean), capped at maxSize
    Quantity maxSize = 100;
    Price marketSlippageTicks = 50;   // marketable orders: FillAndKill limit this far beyond the touch
};

// Anonymous background flow (ParticipantId 0) that gives the market maker something to trade against:
//  - limit orders on both sides, around a reference price that does a random walk,
//  - marketable (FillAndKill) orders, 
//  - cancellations of limit orders after a random lifetime.
// Each process is an independent Poisson process (exponential inter-arrival times).
class NoiseTraders{
public:
    NoiseTraders(Simulator& simulator, Exchange& exchange, NoiseConfig config);

    void Start(); // schedules the first event of every process
    Price ReferencePrice() const {return reference_;}

private:
    SimTime DelayFor(double ratePerSecond);
    Quantity DrawQuantity();

    void ScheduleLimit(Side side);
    void ScheduleMarketable(Side side);
    void ScheduleJump();

    void OnLimitArrival(Side side);
    void OnMarketableArrival(Side side);
    void OnJump();

    Simulator& simulator_;
    Exchange& exchange_;
    NoiseConfig config_;
    Rng rng_;
    Price reference_;
};
