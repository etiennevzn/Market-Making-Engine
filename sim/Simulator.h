#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>

using SimTime = std::int64_t; // microseconds since the start of the simulation
inline constexpr SimTime kMillisecond = 1'000;
inline constexpr SimTime kSecond = 1'000'000;

// Discrete-event simulator: a clock + a queue of timed actions. 
// Nothing here looks at the wall clock, so a run is fully deterministic.
class Simulator{
public:
    using Action = std::function<void()>;

    SimTime Now() const {return now_;}
    std::size_t PendingEvents() const {return events_.size();}

    void Schedule(SimTime at, Action action){
        if(at < now_) at = now_; //never schedule in the past
        events_.push_back(Event{at, nextSequence_++, std::move(action)});
        std::push_heap(events_.begin(), events_.end(), Later{});
    }

    void ScheduleAfter(SimTime delay, Action action){
        Schedule(now_ + std::max<SimTime>(delay, 0), std::move(action));
    }

    // Executes every event with time <= end, in time order (ties: order of scheduling), then sets the clock to `end`.
    void RunUntil(SimTime end){
        while(!events_.empty() && events_.front().time <= end){
            std::pop_heap(events_.begin(), events_.end(), Later{});
            Event event = std::move(events_.back());
            events_.pop_back();
            now_ = event.time;
            event.action(); //may schedule new events
        }
        now_ = std::max(now_, end);
    }

private:
    struct Event{
        SimTime time;
        std::uint64_t sequence;
        Action action;
    };

    struct Later{ //the earliest event must be at the front of the heap
        bool operator()(const Event& a, const Event& b) const {
            return a.time != b.time ? a.time > b.time : a.sequence > b.sequence;
        }
    };

    std::vector<Event> events_;
    std::uint64_t nextSequence_{0};
    SimTime now_{0};
};
