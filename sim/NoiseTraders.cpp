#include "NoiseTraders.h"

#include <algorithm>
#include <cmath>

NoiseTraders::NoiseTraders(Simulator& simulator, Exchange& exchange, NoiseConfig config) :
    simulator_(simulator),
    exchange_(exchange),
    config_(config),
    rng_(config.seed),
    reference_(config.initialFair)
{}

void NoiseTraders::Start(){
    ScheduleLimit(Side::Buy);
    ScheduleLimit(Side::Sell);
    ScheduleMarketable(Side::Buy);
    ScheduleMarketable(Side::Sell);
    ScheduleJump();
}

SimTime NoiseTraders::DelayFor(double ratePerSecond){
    const double seconds = rng_.Exponential(1.0 / ratePerSecond);
    return std::max<SimTime>(1, static_cast<SimTime>(std::llround(seconds * static_cast<double>(kSecond))));
}

Quantity NoiseTraders::DrawQuantity(){
    const double draw = rng_.Exponential(config_.meanSize);
    const double size = 1.0 + std::min(draw, static_cast<double>(config_.maxSize));
    return std::min<Quantity>(static_cast<Quantity>(size), config_.maxSize);
}

void NoiseTraders::ScheduleLimit(Side side){
    if(config_.limitRatePerSide <= 0) return;
    simulator_.ScheduleAfter(DelayFor(config_.limitRatePerSide), [this, side]{
        OnLimitArrival(side);
        ScheduleLimit(side);
    });
}

void NoiseTraders::ScheduleMarketable(Side side){
    if(config_.marketRatePerSide <= 0) return;
    simulator_.ScheduleAfter(DelayFor(config_.marketRatePerSide), [this, side]{
        OnMarketableArrival(side);
        ScheduleMarketable(side);
    });
}

void NoiseTraders::ScheduleJump(){
    if(config_.jumpRate <= 0) return;
    simulator_.ScheduleAfter(DelayFor(config_.jumpRate), [this]{
        OnJump();
        ScheduleJump();
    });
}

//Random draws are kept in separate statements: the evaluation order of function arguments is unspecified in C++,
//and the simulation must give the same result with every compiler.
void NoiseTraders::OnLimitArrival(Side side){
    const double draw = std::min(rng_.Exponential(config_.meanDistanceTicks), 1000.0);
    const Price distance = 1 + static_cast<Price>(draw);
    const Price price = side == Side::Buy ? reference_ - distance : reference_ + distance;
    const Quantity quantity = DrawQuantity();
    const SimTime lifetime = DelayFor(1.0 / config_.meanLifetimeSec);
    const OrderId id = exchange_.NextOrderId();

    exchange_.Submit(std::make_shared<Order>(OrderType::GoodTillCancel, id, side, price, quantity, Constants::NoParticipant));

    //no-op if the order was filled in the meantime
    simulator_.ScheduleAfter(lifetime, [this, id]{ exchange_.Cancel(id); });
}

//Marketable order = FillAndKill limit order priced well beyond the touch: it sweeps the book up to its size, 
//and whatever cannot be filled is cancelled (the engine's own Market type would leave the remainder resting at the worst price).
void NoiseTraders::OnMarketableArrival(Side side){
    const auto touch = side == Side::Buy ? exchange_.Book().BestAsk() : exchange_.Book().BestBid();
    if(!touch) return;

    const Price limit = side == Side::Buy ? *touch + config_.marketSlippageTicks : *touch - config_.marketSlippageTicks;
    const Quantity quantity = DrawQuantity();
    const OrderId id = exchange_.NextOrderId();

    exchange_.Submit(std::make_shared<Order>(OrderType::FillAndKill, id, side, limit, quantity, Constants::NoParticipant));
}

void NoiseTraders::OnJump(){
    reference_ += rng_.Bernoulli(0.5) ? 1 : -1;
}
