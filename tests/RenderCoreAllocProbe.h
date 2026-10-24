// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include <cstdint>
namespace RenderCoreAllocProbe {
void Begin() noexcept;
[[nodiscard]] std::uint64_t End() noexcept;
[[nodiscard]] std::uint64_t LargestAllocationBytes() noexcept;
}
