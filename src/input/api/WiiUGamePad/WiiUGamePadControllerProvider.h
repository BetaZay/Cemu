#pragma once
#include "input/api/ControllerProvider.h"
#include "input/api/ControllerState.h"

// Input-only adapter to the local daemon. No pairing, credentials or UDP ports.
class WiiUGamePadControllerProvider : public ControllerProviderBase
{
public:
	inline static InputAPI::Type kAPIType = InputAPI::WiiUGamePad;
	InputAPI::Type api() const override { return kAPIType; }
	std::vector<std::shared_ptr<ControllerBase>> get_controllers() override;
	bool is_connected(size_t index) const;
	ControllerState get_state(size_t index) const;
};
