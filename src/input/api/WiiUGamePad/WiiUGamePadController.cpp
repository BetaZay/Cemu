#include "input/api/WiiUGamePad/WiiUGamePadController.h"

WiiUGamePadController::WiiUGamePadController(size_t index)
	: base_type(fmt::format("{}", index), fmt::format("Wii U GamePad {}", index + 1)), m_index(index)
{
}

bool WiiUGamePadController::connect()
{
	return is_connected();
}

bool WiiUGamePadController::is_connected()
{
	return m_provider->is_connected(m_index);
}

std::string WiiUGamePadController::get_button_name(uint64 button) const
{
	switch (button)
	{
	case kButton0: return "A";
	case kButton1: return "B";
	case kButton2: return "X";
	case kButton3: return "Y";
	case kButton4: return "L";
	case kButton5: return "R";
	case kButton6: return "ZL";
	case kButton7: return "ZR";
	case kButton8: return "-";
	case kButton9: return "+";
	case kButton10: return "HOME";
	case kButton11: return "UP";
	case kButton12: return "DOWN";
	case kButton13: return "LEFT";
	case kButton14: return "RIGHT";
	case kButton15: return "L3";
	case kButton16: return "R3";
	case kButton17: return "TV";
	}

	return base_type::get_button_name(button);
}

ControllerState WiiUGamePadController::raw_state()
{
	if (!is_connected())
		return {};

	return m_provider->get_state(m_index);
}
