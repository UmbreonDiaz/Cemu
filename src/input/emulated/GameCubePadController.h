#pragma once

#include "input/emulated/EmulatedController.h"

// emulated GameCube controller, plugged into an emulated Wii U GameCube controller adapter.
// Games read it through nn_hpad (e.g. Super Smash Bros. for Wii U).
class GameCubePadController : public EmulatedController
{
public:
	enum ButtonId
	{
		kButtonId_None,

		kButtonId_A,
		kButtonId_B,
		kButtonId_X,
		kButtonId_Y,

		kButtonId_Z,
		kButtonId_Start,

		kButtonId_L, // digital L (full press "click")
		kButtonId_R, // digital R
		kButtonId_LAnalog,
		kButtonId_RAnalog,

		kButtonId_Up,
		kButtonId_Down,
		kButtonId_Left,
		kButtonId_Right,

		kButtonId_StickL_Up,
		kButtonId_StickL_Down,
		kButtonId_StickL_Left,
		kButtonId_StickL_Right,

		kButtonId_StickC_Up,
		kButtonId_StickC_Down,
		kButtonId_StickC_Left,
		kButtonId_StickC_Right,

		kButtonId_Max,
	};

	// button bits as reported by HPADRead
	enum HPADButton : uint32
	{
		kHPADButton_A = 0x0001,
		kHPADButton_B = 0x0002,
		kHPADButton_X = 0x0004,
		kHPADButton_Y = 0x0008,
		kHPADButton_Left = 0x0010,
		kHPADButton_Right = 0x0020,
		kHPADButton_Down = 0x0040,
		kHPADButton_Up = 0x0080,
		kHPADButton_Start = 0x0100,
		kHPADButton_Z = 0x0200,
		kHPADButton_R = 0x0400,
		kHPADButton_L = 0x0800,
	};

	GameCubePadController(size_t player_index);

	Type type() const override { return Type::GameCube; }

	uint32 get_emulated_button_flag(uint32 id) const override;
	size_t get_highest_mapping_id() const override { return kButtonId_Max; }
	bool is_axis_mapping(uint64 mapping) const override
	{
		return (mapping >= kButtonId_StickL_Up && mapping <= kButtonId_StickC_Right) || mapping == kButtonId_LAnalog || mapping == kButtonId_RAnalog;
	}

	// main stick, C-stick, analog L/R in [-1, 1] / [0, 1]
	glm::vec2 get_axis() const override;
	glm::vec2 get_rotation() const override;
	glm::vec2 get_trigger() const override;

	// HPAD button bits for the current state (digital L/R included when a trigger is fully pressed)
	uint32 get_hpad_buttons() const;

	static uint32 s_get_emulated_button_flag(uint32 id);

	static std::string_view get_button_name(ButtonId id);

	bool is_start_down() const override { return is_mapping_down(kButtonId_Start); }
	bool is_left_down() const override { return is_mapping_down(kButtonId_Left); }
	bool is_right_down() const override { return is_mapping_down(kButtonId_Right); }
	bool is_up_down() const override { return is_mapping_down(kButtonId_Up); }
	bool is_down_down() const override { return is_mapping_down(kButtonId_Down); }
	bool is_a_down() const override { return is_mapping_down(kButtonId_A); }
	bool is_b_down() const override { return is_mapping_down(kButtonId_B); }
	bool is_home_down() const override { return false; }

	bool set_default_mapping(const std::shared_ptr<ControllerBase>& controller) override;
};
