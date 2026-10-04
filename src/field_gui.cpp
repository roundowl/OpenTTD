/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file field_gui.cpp GUI for building player-built farm fields. */

#include "stdafx.h"
#include "command_func.h"
#include "company_base.h"
#include "company_func.h"
#include "field_cmd.h"
#include "field_func.h"
#include "field_type.h"
#include "gui.h"
#include "hotkeys.h"
#include "road.h"
#include "road_cmd.h"
#include "road_func.h"
#include "sound_func.h"
#include "terraform_gui.h"
#include "tilehighlight_func.h"
#include "toolbar_gui.h"
#include "viewport_func.h"
#include "window_func.h"
#include "window_gui.h"

#include "widgets/field_widget.h"

#include "table/sprites.h"
#include "table/strings.h"

#include "safeguards.h"

/**
 * Road type to build the entry corner truck stop with.
 * @return The most recently built road type, or the default road.
 */
static RoadType GetFieldRoadType()
{
	if (_last_built_roadtype != INVALID_ROADTYPE && RoadTypeIsRoad(_last_built_roadtype) && HasRoadTypeAvail(_local_company, _last_built_roadtype)) {
		return _last_built_roadtype;
	}
	return ROADTYPE_ROAD;
}

/** Farm toolbar window. */
struct BuildFarmToolbarWindow : Window {
	WidgetID last_user_action = INVALID_WIDGET; ///< Last started user action.

	BuildFarmToolbarWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->InitNested(window_number);
		if (_settings_client.gui.link_terraform_toolbar) ShowTerraformToolbar(this);
	}

	void Close([[maybe_unused]] int data = 0) override
	{
		if (_settings_client.gui.link_terraform_toolbar) CloseWindowById(WindowClass::ScenarioGenerateLandscape, 0, false);
		this->Window::Close();
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_FT_FIELD:
				if (HandlePlacePushButton(this, WID_FT_FIELD, SPR_CURSOR_TRUCK_STATION, HT_RECT)) {
					this->last_user_action = widget;
				}
				break;

			case WID_FT_DEMOLISH:
				HandlePlacePushButton(this, WID_FT_DEMOLISH, ANIMCURSOR_DEMOLISH, HT_RECT | HT_DIAGONAL);
				this->last_user_action = widget;
				break;

			default: break;
		}
	}

	void OnPlaceObject([[maybe_unused]] Point pt, TileIndex tile) override
	{
		switch (this->last_user_action) {
			case WID_FT_FIELD:
				VpStartPlaceSizing(tile, VPM_X_AND_Y_LIMITED, DDSP_BUILD_FIELD);
				VpSetPlaceSizingLimit(FIELD_MAX_SIZE);
				break;

			case WID_FT_DEMOLISH:
				PlaceProc_DemolishArea(tile);
				break;

			default: NOT_REACHED();
		}
	}

	void OnPlaceDrag(ViewportPlaceMethod select_method, [[maybe_unused]] ViewportDragDropSelectionProcess select_proc, [[maybe_unused]] Point pt) override
	{
		VpSelectTilesWithMethod(pt.x, pt.y, select_method);
	}

	void OnPlaceMouseUp([[maybe_unused]] ViewportPlaceMethod select_method, ViewportDragDropSelectionProcess select_proc, Point pt, TileIndex start_tile, TileIndex end_tile) override
	{
		if (pt.x == -1) return;

		switch (select_proc) {
			case DDSP_BUILD_FIELD:
				Command<Commands::BuildField>::Post(STR_ERROR_CAN_T_BUILD_FIELD_HERE, CcPlaySound_CONSTRUCTION_OTHER, end_tile, start_tile, GetFieldRoadType());
				break;

			case DDSP_DEMOLISH_AREA:
				GUIPlaceProcDragXY(select_proc, start_tile, end_tile);
				break;

			default: break;
		}
	}

	Point OnInitialPosition(int16_t sm_width, [[maybe_unused]] int16_t sm_height, [[maybe_unused]] int window_number) override
	{
		return AlignInitialConstructionToolbar(sm_width);
	}

	void OnPlaceObjectAbort() override
	{
		this->RaiseButtons();
	}

	/**
	 * Handler for global hotkeys of the BuildFarmToolbarWindow.
	 * @param hotkey Hotkey
	 * @return EventState::Handled if hotkey was accepted.
	 */
	static EventState FarmToolbarGlobalHotkeys(int hotkey)
	{
		if (_game_mode != GameMode::Normal) return EventState::NotHandled;
		Window *w = ShowBuildFarmToolbar();
		if (w == nullptr) return EventState::NotHandled;
		return w->OnHotkey(hotkey);
	}

	static inline HotkeyList hotkeys{"farmtoolbar", {
		Hotkey('1', "field", WID_FT_FIELD),
		Hotkey('2', "demolish", WID_FT_DEMOLISH),
	}, FarmToolbarGlobalHotkeys};
};

static constexpr std::initializer_list<NWidgetPart> _nested_farm_toolbar_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::DarkGreen),
		NWidget(WWT_CAPTION, Colours::DarkGreen), SetStringTip(STR_TOOLBAR_FARM_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_STICKYBOX, Colours::DarkGreen),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_FT_FIELD), SetFill(0, 1), SetToolbarMinimalSize(2), SetSpriteTip(SPR_IMG_PLANTTREES, STR_TOOLBAR_FARM_BUILD_FIELD_TOOLTIP),
		NWidget(WWT_PANEL, Colours::DarkGreen), SetToolbarSpacerMinimalSize(), SetFill(1, 1), EndContainer(),
		NWidget(WWT_IMGBTN, Colours::DarkGreen, WID_FT_DEMOLISH), SetFill(0, 1), SetToolbarMinimalSize(1), SetSpriteTip(SPR_IMG_DYNAMITE, STR_TOOLTIP_DEMOLISH_BUILDINGS_ETC),
	EndContainer(),
};

static WindowDesc _farm_toolbar_desc(
	WindowPosition::Manual, "toolbar_farm", 0, 0,
	WindowClass::BuildToolbar, WindowClass::None,
	WindowDefaultFlag::Construction,
	_nested_farm_toolbar_widgets,
	&BuildFarmToolbarWindow::hotkeys
);

/**
 * Open the farm toolbar.
 * @return The toolbar window, or \c nullptr if it cannot be opened.
 */
Window *ShowBuildFarmToolbar()
{
	if (!Company::IsValidID(_local_company)) return nullptr;

	CloseWindowByClass(WindowClass::BuildToolbar);
	return AllocateWindowDescFront<BuildFarmToolbarWindow>(_farm_toolbar_desc, TransportType::Road);
}
