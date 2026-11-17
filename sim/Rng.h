#pragma once

#include <cmath>
#include <cstdint>
#include <random>

// Deterministic random numbers. We use the raw output of std::mt19937_64 (fully specified by the standard) 
// and do our own transforms: std::exponential_distribution & co. are implementation-defined, so the same seed 
// would give different simulations with GCC and MSVC.
class Rng{
public:
    explicit Rng(std::uint64_t seed) : engine_(seed) {}

    double Uniform(){ //in [0, 1)
        return static_cast<double>(engine_() >> 11) * (1.0 / 9007199254740992.0);
    }

    double Exponential(double mean){
        return -mean * std::log1p(-Uniform());
    }

    bool Bernoulli(double probability){
        return Uniform() < probability;
    }

private:
    std::mt19937_64 engine_;
};
