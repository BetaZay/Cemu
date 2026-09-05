#pragma once

#include "input/api/Controller.h"
#include "input/api/WiiUGamePad/WiiUGamePadControllerProvider.h"

class WiiUGamePadController : public Controller<WiiUGamePadControllerProvider>
{
public:
	explicit WiiUGamePadController(size_t index);

	std::string_view api_name() const override
	{
		static_assert(to_string(InputAPI::WiiUGamePad) == "WiiUGamePad");
		return to_string(InputAPI::WiiUGamePad);
	}
	InputAPI::Type api() const override { return InputAPI::WiiUGamePad; }

	bool connect() override;
	bool is_connected() override;

	std::string get_button_name(uint64 button) const override;

protected:
	ControllerState raw_state() override;

private:
	size_t m_index;
};
