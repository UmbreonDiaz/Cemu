#pragma once

#include <wx/gbsizer.h>
#include "input/emulated/GameCubePadController.h"
#include "wxgui/input/panels/InputPanel.h"
#include "wxgui/components/wxInputDraw.h"

class GameCubeInputPanel : public InputPanel
{
public:
	GameCubeInputPanel(wxWindow* parent);

	void on_timer(const EmulatedControllerPtr& emulated_controller, const ControllerPtr& controller) override;

private:
	wxInputDraw* m_stick_draw, * m_cstick_draw;

	void add_button_row(wxGridBagSizer* sizer, sint32 row, sint32 column, const GameCubePadController::ButtonId& button_id);
};
