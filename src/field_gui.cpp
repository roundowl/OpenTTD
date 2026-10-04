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
#include "cargotype.h"
#include "dropdown_func.h"
#include "field_base.h"
#include "field_cmd.h"
#include "field_map.h"
#include "strings_func.h"
#include "timer/timer.h"
#include "timer/timer_window.h"
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
#include "zoom_func.h"

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

/** Names of the task types, indexed by #FieldTaskType. */
static const StringID _field_task_names[] = {
	STR_FIELD_TASK_CULTIVATE,
	STR_FIELD_TASK_SOW,
	STR_FIELD_TASK_FERTILISE,
	STR_FIELD_TASK_SPRAY,
	STR_FIELD_TASK_HARVEST,
};
static_assert(std::size(_field_task_names) == to_underlying(FieldTaskType::End));

/** Names of the growth stages, indexed by #FieldStage. */
static const StringID _field_stage_names[] = {
	STR_FIELD_STAGE_FALLOW,
	STR_FIELD_STAGE_CULTIVATED,
	STR_FIELD_STAGE_SOWN,
	STR_FIELD_STAGE_SPROUTED,
	STR_FIELD_STAGE_GROWING,
	STR_FIELD_STAGE_MATURING,
	STR_FIELD_STAGE_RIPE,
	STR_FIELD_STAGE_OVERRIPE,
	STR_FIELD_STAGE_WITHERED,
};
static_assert(std::size(_field_stage_names) == to_underlying(FieldStage::End));

/** Entries of the start month dropdown: any time, then the twelve months. */
static const StringID _field_month_names[] = {
	STR_FIELD_VIEW_MONTH_ANY,
	STR_MONTH_JAN, STR_MONTH_FEB, STR_MONTH_MAR, STR_MONTH_APR, STR_MONTH_MAY, STR_MONTH_JUN,
	STR_MONTH_JUL, STR_MONTH_AUG, STR_MONTH_SEP, STR_MONTH_OCT, STR_MONTH_NOV, STR_MONTH_DEC,
};

/** Window showing the state and the work plan of a farm field. */
struct FieldViewWindow : Window {
	static const uint INFO_LINES = 6; ///< Number of text lines in the info panel.

	Scrollbar *vscroll = nullptr; ///< Scrollbar of the task list.
	int selected = -1; ///< Selected task, or -1.

	FieldViewWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_FV_SCROLLBAR);
		this->FinishInitNested(window_number);
		this->owner = this->GetField()->owner;
		this->OnInvalidateData();
	}

	const Field *GetField() const
	{
		return Field::Get(static_cast<FieldID>(this->window_number));
	}

	/** Send a plan change to the server. */
	void Modify(FieldTaskAction action, uint8_t pos, uint8_t value = 0) const
	{
		StringID err = action == FieldTaskAction::PerformNow ? STR_ERROR_CAN_T_DO_FIELD_TASK : STR_ERROR_CAN_T_MODIFY_FIELD_PLAN;
		Command<Commands::ModifyFieldTasks>::Post(err, static_cast<FieldID>(this->window_number), action, pos, value);
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_FV_CAPTION) return GetString(STR_FIELD_VIEW_CAPTION, this->GetField()->station);
		return this->Window::GetWidgetString(widget, stringid);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, const Dimension &padding, Dimension &fill, Dimension &resize) override
	{
		switch (widget) {
			case WID_FV_INFO:
				size.height = INFO_LINES * GetCharacterHeight(FontSize::Normal) + padding.height;
				size.width = std::max<uint>(size.width, ScaleGUITrad(300));
				break;

			case WID_FV_TASKS:
				fill.height = resize.height = GetCharacterHeight(FontSize::Normal);
				size.height = 6 * resize.height + padding.height;
				break;
		}
	}

	/** Build the "Ground:" summary of how many quarters are in which stage. */
	std::string GetGroundSummary(const Field *f) const
	{
		std::array<uint, to_underlying(FieldStage::End)> counts{};
		for (TileIndex tile : f->location) {
			if (!IsTileType(tile, TileType::Field)) continue;
			for (uint q = 0; q < 4; q++) counts[to_underlying(GetFieldQuarterStage(tile, q))]++;
		}

		std::string summary;
		for (uint i = 0; i < counts.size(); i++) {
			if (counts[i] == 0) continue;
			if (!summary.empty()) summary += ", ";
			summary += GetString(STR_FIELD_VIEW_STAGE_COUNT, _field_stage_names[i], counts[i]);
		}
		return summary;
	}

	void DrawInfo(const Rect &r) const
	{
		const Field *f = this->GetField();
		Rect tr = r.Shrink(WidgetDimensions::scaled.framerect);
		int line = GetCharacterHeight(FontSize::Normal);

		uint quarters = 0, fertilised = 0, sprayed = 0;
		for (TileIndex tile : f->location) {
			if (!IsTileType(tile, TileType::Field)) continue;
			for (uint q = 0; q < 4; q++) {
				quarters++;
				if (IsFieldQuarterTreated(tile, q, false)) fertilised++;
				if (IsFieldQuarterTreated(tile, q, true)) sprayed++;
			}
		}

		DrawString(tr, GetString(STR_FIELD_VIEW_SIZE, f->location.w, f->location.h, quarters));
		tr.top += line;
		StringID crop = IsValidCargoType(f->crop) ? CargoSpec::Get(f->crop)->name : STR_FIELD_VIEW_CROP_NONE;
		DrawString(tr, GetString(STR_FIELD_VIEW_CROP, crop));
		tr.top += line;
		DrawString(tr, GetString(STR_FIELD_VIEW_GROUND, this->GetGroundSummary(f)));
		tr.top += line;
		DrawString(tr, GetString(STR_FIELD_VIEW_TREATED, quarters == 0 ? 0 : fertilised * 100 / quarters, quarters == 0 ? 0 : sprayed * 100 / quarters));
		tr.top += line;
		DrawString(tr, f->IsGrowthPaused() ? STR_FIELD_VIEW_PAUSED : STR_FIELD_VIEW_GROWING);
		tr.top += line;
		if (f->last_harvest > 0 && IsValidCargoType(f->crop)) {
			DrawString(tr, GetString(STR_FIELD_VIEW_LAST_HARVEST, f->crop, f->last_harvest));
		} else {
			DrawString(tr, STR_FIELD_VIEW_LAST_HARVEST_NONE);
		}
	}

	void DrawTasks(const Rect &r) const
	{
		const Field *f = this->GetField();
		Rect tr = r.Shrink(WidgetDimensions::scaled.framerect);
		int line = this->resize.step_height;

		if (f->tasks.empty()) {
			DrawString(tr, STR_FIELD_VIEW_PLAN_EMPTY);
			return;
		}

		auto [first, last] = this->vscroll->GetVisibleRangeIterators(f->tasks);
		for (auto it = first; it != last; ++it) {
			uint index = static_cast<uint>(std::distance(f->tasks.begin(), it));
			bool current = index == f->cur_task;

			if (static_cast<int>(index) == this->selected) {
				GfxFillRect(tr.left, tr.top, tr.right, tr.top + line - 1, PC_DARK_GREY);
			}

			std::string text = it->start_month == 0
					? GetString(STR_FIELD_VIEW_TASK_ROW, index + 1, _field_task_names[to_underlying(it->type)])
					: GetString(STR_FIELD_VIEW_TASK_ROW_MONTH, index + 1, _field_task_names[to_underlying(it->type)], _field_month_names[it->start_month]);
			if (current) {
				switch (f->GetTaskState(index)) {
					case Field::TaskState::Available: text += " " + GetString(STR_FIELD_VIEW_TASK_AVAILABLE); break;
					case Field::TaskState::Waiting: text += " " + GetString(STR_FIELD_VIEW_TASK_WAITING); break;
					case Field::TaskState::Done: text += " " + GetString(STR_FIELD_VIEW_TASK_DONE); break;
				}
			}
			DrawString(tr.left, tr.right, tr.top, text, current ? TextColour::White : TextColour::Black);
			tr.top += line;
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		switch (widget) {
			case WID_FV_INFO: this->DrawInfo(r); break;
			case WID_FV_TASKS: this->DrawTasks(r); break;
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		const Field *f = this->GetField();
		switch (widget) {
			case WID_FV_LOCATION:
				if (_ctrl_pressed) {
					ShowExtraViewportWindow(f->location.GetCentreTile());
				} else {
					ScrollMainWindowToTile(f->location.GetCentreTile());
				}
				break;

			case WID_FV_TASKS: {
				int row = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_FV_TASKS, WidgetDimensions::scaled.framerect.top);
				this->selected = (row < static_cast<int>(f->tasks.size()) && row != this->selected) ? row : -1;
				this->OnInvalidateData();
				this->SetDirty();
				break;
			}

			case WID_FV_ADD:
				ShowDropDownMenu(this, _field_task_names, -1, WID_FV_ADD, 0, 0);
				break;

			case WID_FV_DELETE:
				if (this->selected >= 0) this->Modify(FieldTaskAction::Delete, this->selected);
				break;

			case WID_FV_MONTH:
				if (this->selected >= 0) ShowDropDownMenu(this, _field_month_names, f->tasks[this->selected].start_month, WID_FV_MONTH, 0, 0);
				break;

			case WID_FV_GOTO:
				if (this->selected >= 0) this->Modify(FieldTaskAction::SkipTo, this->selected);
				break;

			case WID_FV_DO_NOW:
				this->Modify(FieldTaskAction::PerformNow, 0);
				break;
		}
	}

	void OnDropdownSelect(WidgetID widget, int index, int) override
	{
		const Field *f = this->GetField();
		switch (widget) {
			case WID_FV_ADD: {
				uint8_t pos = this->selected >= 0 ? this->selected : static_cast<uint8_t>(f->tasks.size());
				this->Modify(FieldTaskAction::Insert, pos, index);
				break;
			}

			case WID_FV_MONTH:
				if (this->selected >= 0) this->Modify(FieldTaskAction::SetMonth, this->selected, index);
				break;
		}
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_FV_TASKS, WidgetDimensions::scaled.framerect.Vertical());
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		const Field *f = this->GetField();
		if (this->selected >= static_cast<int>(f->tasks.size())) this->selected = -1;
		this->vscroll->SetCount(f->tasks.size());

		bool mine = f->owner == _local_company;
		bool has_sel = this->selected >= 0;
		this->SetWidgetDisabledState(WID_FV_ADD, !mine || f->tasks.size() >= FIELD_MAX_TASKS);
		this->SetWidgetsDisabledState(!mine || !has_sel, WID_FV_DELETE, WID_FV_MONTH, WID_FV_GOTO);
		this->SetWidgetDisabledState(WID_FV_DO_NOW, !mine);
	}

	void OnPaint() override
	{
		this->OnInvalidateData();
		this->DrawWidgets();
	}

	/** Refresh periodically; growth and task states change without plan edits. */
	const IntervalTimer<TimerWindow> refresh_interval = {std::chrono::seconds(1), [this](auto) {
		this->SetDirty();
	}};
};

static constexpr std::initializer_list<NWidgetPart> _nested_field_view_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::DarkGreen),
		NWidget(WWT_CAPTION, Colours::DarkGreen, WID_FV_CAPTION),
		NWidget(WWT_PUSHIMGBTN, Colours::DarkGreen, WID_FV_LOCATION), SetAspect(WidgetDimensions::ASPECT_LOCATION), SetSpriteTip(SPR_GOTO_LOCATION, STR_STATION_VIEW_CENTER_TOOLTIP),
		NWidget(WWT_SHADEBOX, Colours::DarkGreen),
		NWidget(WWT_DEFSIZEBOX, Colours::DarkGreen),
		NWidget(WWT_STICKYBOX, Colours::DarkGreen),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::DarkGreen, WID_FV_INFO), SetResize(1, 0), SetFill(1, 0), EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::DarkGreen, WID_FV_TASKS), SetResize(1, 1), SetFill(1, 1), SetScrollbar(WID_FV_SCROLLBAR), SetToolTip(STR_FIELD_VIEW_TASKS_TOOLTIP), EndContainer(),
		NWidget(NWID_VSCROLLBAR, Colours::DarkGreen, WID_FV_SCROLLBAR),
	EndContainer(),
	NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
		NWidget(WWT_DROPDOWN, Colours::DarkGreen, WID_FV_ADD), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_FIELD_VIEW_ADD_TASK, STR_FIELD_VIEW_ADD_TASK_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, Colours::DarkGreen, WID_FV_DELETE), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_FIELD_VIEW_DELETE_TASK, STR_FIELD_VIEW_DELETE_TASK_TOOLTIP),
		NWidget(WWT_DROPDOWN, Colours::DarkGreen, WID_FV_MONTH), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_FIELD_VIEW_START_MONTH, STR_FIELD_VIEW_START_MONTH_TOOLTIP),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
			NWidget(WWT_PUSHTXTBTN, Colours::DarkGreen, WID_FV_GOTO), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_FIELD_VIEW_GO_TO, STR_FIELD_VIEW_GO_TO_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, Colours::DarkGreen, WID_FV_DO_NOW), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_FIELD_VIEW_DO_NOW, STR_FIELD_VIEW_DO_NOW_TOOLTIP),
		EndContainer(),
		NWidget(WWT_RESIZEBOX, Colours::DarkGreen),
	EndContainer(),
};

static WindowDesc _field_view_desc(
	WindowPosition::Automatic, "view_field", 320, 260,
	WindowClass::FieldView, WindowClass::None,
	{},
	_nested_field_view_widgets
);

/**
 * Open the window of a farm field.
 * @param field The field.
 */
void ShowFieldWindow(FieldID field)
{
	AllocateWindowDescFront<FieldViewWindow>(_field_view_desc, field);
}
