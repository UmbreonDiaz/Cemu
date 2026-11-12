#include "input/api/GameCube/GameCubeController.h"

#ifdef HAS_GAMECUBE

namespace
{
	// how far the sticks and triggers of an original controller travel from their rest position
	constexpr float kStickRange = 100.0f;
	constexpr float kCStickRange = 100.0f;
	constexpr float kTriggerRange = 200.0f;
}

GameCubeController::GameCubeController(uint32 adapter, uint32 index)
	: base_type(fmt::format("{}_{}", adapter, index), fmt::format("Port {}", index + 1)), m_adapter(adapter),
	  m_index(index)
{
	// update names if multiple adapters are connected
	if (adapter > 0)
		m_display_name = fmt::format("Port {} (adapter {})", index + 1, adapter + 1);

	// GameCube sticks are precise, a small deadzone is enough
	m_settings.axis.deadzone = 0.1f;
	m_settings.rotation.deadzone = 0.1f;
	m_settings.trigger.deadzone = 0.05f;

	m_provider->start();
}

bool GameCubeController::is_connected()
{
	return m_provider->is_port_connected(m_adapter, m_index);
}

bool GameCubeController::has_rumble()
{
	return m_provider->has_rumble_connected(m_adapter);
}

void GameCubeController::start_rumble()
{
	if (m_settings.rumble <= 0)
		return;

	m_provider->set_rumble_state(m_adapter, m_index, true);
}

void GameCubeController::stop_rumble()
{
	m_provider->set_rumble_state(m_adapter, m_index, false);
}

std::string GameCubeController::get_button_name(uint64 button) const
{
	switch (button)
	{
	case kButton0: return "A";
	case kButton1: return "B";
	case kButton2: return "X";
	case kButton3: return "Y";

	case kButton4: return "Left";
	case kButton5: return "Right";
	case kButton6: return "Down";
	case kButton7: return "Up";

	case kButton8: return "Start";
	case kButton9: return "Z";

	case kButton10: return "R";
	case kButton11: return "L";

	case kTriggerXP: return "L analog";
	case kTriggerYP: return "R analog";
	}

	return base_type::get_button_name(button);
}

ControllerState GameCubeController::raw_state()
{
	ControllerState result{};
	const auto state = m_provider->get_state(m_adapter, m_index);
	if (!state.valid || state.type == GameCubeControllerProvider::kPortNone)
		return result;

	for (uint32 i = 0; i <= kButton11; ++i)
	{
		if (HAS_BIT(state.button, i))
			result.buttons.SetButtonState(i, true);
	}

	auto norm = [](sint16 value, float range) { return std::clamp((float)value / range, -1.0f, 1.0f); };
	result.axis.x = norm(state.lstick_x, kStickRange);
	result.axis.y = norm(state.lstick_y, kStickRange);
	result.rotation.x = norm(state.rstick_x, kCStickRange);
	result.rotation.y = norm(state.rstick_y, kCStickRange);
	result.trigger.x = norm(state.ltrigger, kTriggerRange);
	result.trigger.y = norm(state.rtrigger, kTriggerRange);

	return result;
}

#endif
