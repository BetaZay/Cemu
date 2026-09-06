#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

// Pure, time-indexed source pattern; no decoder or transport settings change.
namespace BaristaAppHookColorTest
{
// Match Cemu's base types without importing emulator state into this pure source.
using uint8 = std::uint8_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
constexpr uint32 Width = 864, Height = 480;
constexpr uint64 PhaseUs = 2000000;
constexpr std::array<std::string_view, 16> Phases{
	"black", "red cut", "green cut", "blue cut", "white cut", "black cut",
	"fade black to red", "fade red to black", "fade black to white", "fade white to black",
	"detail cut", "red cut from detail", "detail cut from red", "black cut from detail",
	"fade black to detail", "fade detail to black"};
constexpr uint64 CycleUs = PhaseUs * Phases.size();

std::vector<uint8> Render(uint64 elapsedUs);
}
