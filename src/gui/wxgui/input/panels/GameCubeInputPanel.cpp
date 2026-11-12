#include "wxgui/input/panels/GameCubeInputPanel.h"

#include <wx/gbsizer.h>
#include <wx/stattext.h>
#include <wx/statline.h>
#include <wx/textctrl.h>

#include "input/emulated/GameCubePadController.h"
#include "wxgui/helpers/wxHelpers.h"
#include "wxgui/components/wxInputDraw.h"

namespace
{
	using GC = GameCubePadController;
	const GC::ButtonId kButtonItems[] = { GC::kButtonId_A, GC::kButtonId_B, GC::kButtonId_X, GC::kButtonId_Y, GC::kButtonId_Z, GC::kButtonId_Start, GC::kButtonId_L, GC::kButtonId_R, GC::kButtonId_LAnalog, GC::kButtonId_RAnalog };
	const GC::ButtonId kStickItems[] = { GC::kButtonId_StickL_Up, GC::kButtonId_StickL_Down, GC::kButtonId_StickL_Left, GC::kButtonId_StickL_Right };
	const GC::ButtonId kCStickItems[] = { GC::kButtonId_StickC_Up, GC::kButtonId_StickC_Down, GC::kButtonId_StickC_Left, GC::kButtonId_StickC_Right };
	const GC::ButtonId kDPadItems[] = { GC::kButtonId_Up, GC::kButtonId_Down, GC::kButtonId_Left, GC::kButtonId_Right };
}

GameCubeInputPanel::GameCubeInputPanel(wxWindow* parent)
	: InputPanel(parent)
{
	auto bold_font = GetFont();
	bold_font.MakeBold();

	auto main_sizer = new wxGridBagSizer();

	sint32 row = 0;
	sint32 column = 0;
	for (auto id : kButtonItems)
	{
		row++;
		add_button_row(main_sizer, row, column, id);
	}

	//////////////////////////////////////////////////////////////////

	main_sizer->Add(new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVERTICAL), wxGBPosition(0, column + 2), wxGBSpan(11, 1), wxALL | wxEXPAND, 5);

	row = 0;
	column += 3;

	auto text = new wxStaticText(this, wxID_ANY, _("Control Stick"));
	text->SetFont(bold_font);
	main_sizer->Add(text, wxGBPosition(row, column), wxGBSpan(1, 3), wxALL | wxEXPAND, 5);

	for (auto id : kStickItems)
	{
		row++;
		add_button_row(main_sizer, row, column, id);
	}

	row++;

	m_stick_draw = new wxInputDraw(this, wxID_ANY, wxDefaultPosition, { 60, 60 });
	main_sizer->Add(m_stick_draw, wxGBPosition(row, column + 1), wxGBSpan(2, 1), wxTOP | wxBOTTOM | wxEXPAND | wxALIGN_CENTER, 5);

	main_sizer->Add(new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVERTICAL), wxGBPosition(0, column + 3), wxGBSpan(11, 1), wxALL | wxEXPAND, 5);

	//////////////////////////////////////////////////////////////////

	row = 0;
	column += 4;

	text = new wxStaticText(this, wxID_ANY, _("C-Stick"));
	text->SetFont(bold_font);
	main_sizer->Add(text, wxGBPosition(row, column), wxGBSpan(1, 3), wxALL | wxEXPAND, 5);

	for (auto id : kCStickItems)
	{
		row++;
		add_button_row(main_sizer, row, column, id);
	}

	row++;

	m_cstick_draw = new wxInputDraw(this, wxID_ANY, wxDefaultPosition, { 60, 60 });
	main_sizer->Add(m_cstick_draw, wxGBPosition(row, column + 1), wxGBSpan(2, 1), wxTOP | wxBOTTOM | wxEXPAND | wxALIGN_CENTER, 5);

	main_sizer->Add(new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVERTICAL), wxGBPosition(0, column + 3), wxGBSpan(11, 1), wxALL | wxEXPAND, 5);

	//////////////////////////////////////////////////////////////////

	row = 0;
	column += 4;

	text = new wxStaticText(this, wxID_ANY, _("D-pad"));
	text->SetFont(bold_font);
	main_sizer->Add(text, wxGBPosition(row, column), wxGBSpan(1, 3), wxALL | wxEXPAND, 5);

	for (auto id : kDPadItems)
	{
		row++;
		add_button_row(main_sizer, row, column, id);
	}

	//////////////////////////////////////////////////////////////////

	SetSizerAndFit(main_sizer);
}

void GameCubeInputPanel::add_button_row(wxGridBagSizer* sizer, sint32 row, sint32 column, const GameCubePadController::ButtonId& button_id)
{
	sizer->Add(
		new wxStaticText(this, wxID_ANY, wxGetTranslation(wxString::FromUTF8(GameCubePadController::get_button_name(button_id)))),
		wxGBPosition(row, column),
		wxDefaultSpan,
		wxALL | wxALIGN_CENTER_VERTICAL, 5);

	auto text_ctrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxTE_PROCESS_ENTER | wxTE_PROCESS_TAB);
	text_ctrl->SetClientData((void*)button_id);
	text_ctrl->SetMinSize(wxSize(150, -1));
	text_ctrl->SetEditable(false);
	text_ctrl->SetBackgroundColour(kKeyColourNormalMode);
	bind_hotkey_events(text_ctrl);
	sizer->Add(text_ctrl, wxGBPosition(row, column + 1), wxDefaultSpan, wxALL | wxEXPAND, 5);
}

void GameCubeInputPanel::on_timer(const EmulatedControllerPtr& emulated_controller, const ControllerPtr& controller_base)
{
	InputPanel::on_timer(emulated_controller, controller_base);

	if (emulated_controller)
	{
		m_stick_draw->SetAxisValue(emulated_controller->get_axis());
		m_cstick_draw->SetAxisValue(emulated_controller->get_rotation());
	}
}
