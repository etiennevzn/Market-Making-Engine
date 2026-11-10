#pragma once

#include <limits>

#include "Usings.h"

struct Constants
{
    // Sentinel for "no price" (market orders). Note: quiet_NaN() is 0 for an integer type, which is a valid price.
    static constexpr Price InvalidPrice = std::numeric_limits<Price>::min();
    static constexpr ParticipantId NoParticipant = 0;
};
