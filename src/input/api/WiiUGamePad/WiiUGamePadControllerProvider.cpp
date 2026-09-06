#include "input/api/WiiUGamePad/WiiUGamePadControllerProvider.h"
#include "input/api/WiiUGamePad/WiiUGamePadController.h"
#include "Common/BaristaAppHook.h"
#include <algorithm>

namespace
{
constexpr std::array<uint32, 18> s_buttonMasks{0x8000, 0x4000, 0x2000, 0x1000,
	0x20, 0x10, 0x80, 0x40, 0x4, 0x8, 0x2, 0x200, 0x100, 0x800, 0x400,
	0x800000, 0x400000, 0x200000};

constexpr uint32 DecodeButtons(const std::array<uint8, 128>& report)
{
	// Unlike the little-endian stick fields, the main button mask is big-endian.
	return uint32(report[80]) << 16 | uint32(report[2]) << 8 | report[3];
}

constexpr bool ValidateButtonDecoder()
{
	// Wire bytes -> Cemu physical button index. Independent protocol fixtures
	// catch byte swapping (e.g. A becoming ZL) and the X/Y mask reversal.
	constexpr std::array<std::array<uint8, 4>, 18> cases{{
		{0x80,0,0,0}, {0x40,0,0,1}, {0x20,0,0,2}, {0x10,0,0,3},
		{0,0x20,0,4}, {0,0x10,0,5}, {0,0x80,0,6}, {0,0x40,0,7},
		{0,4,0,8}, {0,8,0,9}, {0,2,0,10}, {2,0,0,11},
		{1,0,0,12}, {8,0,0,13}, {4,0,0,14},
		{0,0,0x80,15}, {0,0,0x40,16}, {0,0,0x20,17}}};
	for (const auto& test : cases)
	{
		std::array<uint8, 128> report{};
		report[2] = test[0]; report[3] = test[1]; report[80] = test[2];
		for (size_t i = 0; i < s_buttonMasks.size(); ++i)
			if (((DecodeButtons(report) & s_buttonMasks[i]) != 0) != (i == test[3]))
				return false;
	}
	return true;
}
static_assert(ValidateButtonDecoder());
}

std::vector<std::shared_ptr<ControllerBase>> WiiUGamePadControllerProvider::get_controllers()
{
	return {std::make_shared<WiiUGamePadController>(0)};
}
bool WiiUGamePadControllerProvider::is_connected(size_t index) const
{
	std::array<uint8, 128> report{};
	return index == 0 && BaristaAppHook::ReadInput(report);
}
ControllerState WiiUGamePadControllerProvider::get_state(size_t index) const
{
	ControllerState state{};
	std::array<uint8, 128> report{};
	if (index != 0 || !BaristaAppHook::ReadInput(report)) return state;
	const uint32 buttons = DecodeButtons(report);
	for (size_t i = 0; i < s_buttonMasks.size(); ++i)
		state.buttons.SetButtonState(kButton0 + i, (buttons & s_buttonMasks[i]) != 0);
	auto stick = [&](size_t offset) {
		const sint32 value = report[offset] | uint32(report[offset + 1]) << 8;
		const float normalized = std::clamp((value - 2050) / 1150.0f, -1.0f, 1.0f);
		return std::abs(normalized) < 0.1f ? 0.0f : normalized;
	};
	state.axis = {stick(6), stick(8)};
	state.rotation = {stick(10), stick(12)};
	return state;
}
