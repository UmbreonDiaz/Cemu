#include "input/emulated/GameCubePadController.h"

#include "input/api/Controller.h"

GameCubePadController::GameCubePadController(size_t player_index)
	: EmulatedController(player_index)
{
}

uint32 GameCubePadController::get_emulated_button_flag(uint32 id) const
{
	return s_get_emulated_button_flag(id);
}

uint32 GameCubePadController::s_get_emulated_button_flag(uint32 id)
{
	switch (id)
	{
	case kButtonId_A: return kHPADButton_A;
	case kButtonId_B: return kHPADButton_B;
	case kButtonId_X: return kHPADButton_X;
	case kButtonId_Y: return kHPADButton_Y;
	case kButtonId_Z: return kHPADButton_Z;
	case kButtonId_Start: return kHPADButton_Start;
	case kButtonId_L: return kHPADButton_L;
	case kButtonId_R: return kHPADButton_R;
	case kButtonId_Up: return kHPADButton_Up;
	case kButtonId_Down: return kHPADButton_Down;
	case kButtonId_Left: return kHPADButton_Left;
	case kButtonId_Right: return kHPADButton_Right;
	default: return 0;
	}
}

std::string_view GameCubePadController::get_button_name(ButtonId id)
{
	switch (id)
	{
	case kButtonId_A: return "A";
	case kButtonId_B: return "B";
	case kButtonId_X: return "X";
	case kButtonId_Y: return "Y";
	case kButtonId_Z: return "Z";
	case kButtonId_Start: return TR_NOOP("start");
	case kButtonId_L: return "L";
	case kButtonId_R: return "R";
	case kButtonId_LAnalog: return TR_NOOP("L analog");
	case kButtonId_RAnalog: return TR_NOOP("R analog");
	case kButtonId_Up: return TR_NOOP("up");
	case kButtonId_Down: return TR_NOOP("down");
	case kButtonId_Left: return TR_NOOP("left");
	case kButtonId_Right: return TR_NOOP("right");
	case kButtonId_StickL_Up: return TR_NOOP("up");
	case kButtonId_StickL_Down: return TR_NOOP("down");
	case kButtonId_StickL_Left: return TR_NOOP("left");
	case kButtonId_StickL_Right: return TR_NOOP("right");
	case kButtonId_StickC_Up: return TR_NOOP("up");
	case kButtonId_StickC_Down: return TR_NOOP("down");
	case kButtonId_StickC_Left: return TR_NOOP("left");
	case kButtonId_StickC_Right: return TR_NOOP("right");
	default:
		cemu_assert_debug(false);
		return "";
	}
}

glm::vec2 GameCubePadController::get_axis() const
{
	const auto left = get_axis_value(kButtonId_StickL_Left);
	const auto right = get_axis_value(kButtonId_StickL_Right);

	const auto up = get_axis_value(kButtonId_StickL_Up);
	const auto down = get_axis_value(kButtonId_StickL_Down);

	glm::vec2 result;
	result.x = (left > right) ? -left : right;
	result.y = (up > down) ? up : -down;
	return result;
}

glm::vec2 GameCubePadController::get_rotation() const
{
	const auto left = get_axis_value(kButtonId_StickC_Left);
	const auto right = get_axis_value(kButtonId_StickC_Right);

	const auto up = get_axis_value(kButtonId_StickC_Up);
	const auto down = get_axis_value(kButtonId_StickC_Down);

	glm::vec2 result;
	result.x = (left > right) ? -left : right;
	result.y = (up > down) ? up : -down;
	return result;
}

glm::vec2 GameCubePadController::get_trigger() const
{
	// a digital L/R mapped to a key or button presses the analog trigger all the way
	float left = get_axis_value(kButtonId_LAnalog);
	float right = get_axis_value(kButtonId_RAnalog);
	if (is_mapping_down(kButtonId_L))
		left = 1.0f;
	if (is_mapping_down(kButtonId_R))
		right = 1.0f;
	return {left, right};
}

uint32 GameCubePadController::get_hpad_buttons() const
{
	uint32 result = 0;
	for (uint32 id = kButtonId_A; id <= kButtonId_Right; ++id)
	{
		if (id == kButtonId_LAnalog || id == kButtonId_RAnalog)
			continue;
		if (is_mapping_down(id))
			result |= s_get_emulated_button_flag(id);
	}

	// when only an analog trigger is mapped, a full press also "clicks" the digital L/R, like on the real controller
	constexpr float kClickThreshold = 0.95f;
	if (!get_mapping_controller(kButtonId_L) && get_axis_value(kButtonId_LAnalog) >= kClickThreshold)
		result |= kHPADButton_L;
	if (!get_mapping_controller(kButtonId_R) && get_axis_value(kButtonId_RAnalog) >= kClickThreshold)
		result |= kHPADButton_R;

	return result;
}

bool GameCubePadController::set_default_mapping(const std::shared_ptr<ControllerBase>& controller)
{
	std::vector<std::pair<uint64, uint64>> mapping;
	switch (controller->api())
	{
	case InputAPI::GameCube:
	{
		// a real GameCube controller: everything maps 1:1
		mapping =
		{
			{kButtonId_A, kButton0},
			{kButtonId_B, kButton1},
			{kButtonId_X, kButton2},
			{kButtonId_Y, kButton3},

			{kButtonId_Left, kButton4},
			{kButtonId_Right, kButton5},
			{kButtonId_Down, kButton6},
			{kButtonId_Up, kButton7},

			{kButtonId_Start, kButton8},
			{kButtonId_Z, kButton9},
			{kButtonId_R, kButton10},
			{kButtonId_L, kButton11},

			{kButtonId_LAnalog, kTriggerXP},
			{kButtonId_RAnalog, kTriggerYP},

			{kButtonId_StickL_Up, kAxisYP},
			{kButtonId_StickL_Down, kAxisYN},
			{kButtonId_StickL_Left, kAxisXN},
			{kButtonId_StickL_Right, kAxisXP},

			{kButtonId_StickC_Up, kRotationYP},
			{kButtonId_StickC_Down, kRotationYN},
			{kButtonId_StickC_Left, kRotationXN},
			{kButtonId_StickC_Right, kRotationXP},
		};
		break;
	}
#ifdef HAS_SDL
	case InputAPI::SDLController:
	{
		// positional: A = bottom face button, B = left, X = right, Y = top
		mapping =
		{
			{kButtonId_A, kButton0},
			{kButtonId_B, kButton2},
			{kButtonId_X, kButton1},
			{kButtonId_Y, kButton3},

			{kButtonId_Z, kButton10},
			{kButtonId_Start, kButton6},

			{kButtonId_LAnalog, kTriggerXP},
			{kButtonId_RAnalog, kTriggerYP},

			{kButtonId_Up, kButton11},
			{kButtonId_Down, kButton12},
			{kButtonId_Left, kButton13},
			{kButtonId_Right, kButton14},

			{kButtonId_StickL_Up, kAxisYN},
			{kButtonId_StickL_Down, kAxisYP},
			{kButtonId_StickL_Left, kAxisXN},
			{kButtonId_StickL_Right, kAxisXP},

			{kButtonId_StickC_Up, kRotationYN},
			{kButtonId_StickC_Down, kRotationYP},
			{kButtonId_StickC_Left, kRotationXN},
			{kButtonId_StickC_Right, kRotationXP},
		};
		break;
	}
#endif
	case InputAPI::XInput:
	{
		mapping =
		{
			{kButtonId_A, kButton12},
			{kButtonId_B, kButton14},
			{kButtonId_X, kButton13},
			{kButtonId_Y, kButton15},

			{kButtonId_Z, kButton9},
			{kButtonId_Start, kButton4},

			{kButtonId_LAnalog, kTriggerXP},
			{kButtonId_RAnalog, kTriggerYP},

			{kButtonId_Up, kButton0},
			{kButtonId_Down, kButton1},
			{kButtonId_Left, kButton2},
			{kButtonId_Right, kButton3},

			{kButtonId_StickL_Up, kAxisYP},
			{kButtonId_StickL_Down, kAxisYN},
			{kButtonId_StickL_Left, kAxisXN},
			{kButtonId_StickL_Right, kAxisXP},

			{kButtonId_StickC_Up, kRotationYP},
			{kButtonId_StickC_Down, kRotationYN},
			{kButtonId_StickC_Left, kRotationXN},
			{kButtonId_StickC_Right, kRotationXP},
		};
		break;
	}
	default:
		break;
	}

	bool mapping_updated = false;
	std::for_each(mapping.cbegin(), mapping.cend(), [this, &controller, &mapping_updated](const auto& m)
	{
		if (m_mappings.find(m.first) == m_mappings.cend())
		{
			set_mapping(m.first, controller, m.second);
			mapping_updated = true;
		}
	});

	return mapping_updated;
}
