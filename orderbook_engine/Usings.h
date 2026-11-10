#pragma once

#include <cstdint>
#include <vector>

using Price = std::int32_t;          // price in integer ticks
using Quantity = std::uint32_t;
using OrderId = std::uint64_t;
using ParticipantId = std::uint32_t; // who owns the order (0 = anonymous, no self-trade prevention)
using OrderIds = std::vector<OrderId>;
