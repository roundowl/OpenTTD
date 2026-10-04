/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file field.cpp Growth and work planning of player-built farm fields. */

#include "stdafx.h"
#include "field_base.h"
#include "field_map.h"
#include "cargotype.h"
#include "economy_func.h"
#include "landscape.h"
#include "settings_type.h"
#include "viewport_func.h"
#include "window_func.h"
#include "timer/timer.h"
#include "timer/timer_game_calendar.h"
#include "timer/timer_game_economy.h"

#include "table/strings.h"

#include "safeguards.h"

/**
 * Get the cargo a field crop produces when no other crop is chosen:
 * the climate's grain, wheat or maize.
 * @return The cargo type, or #INVALID_CARGO if the climate has none of them.
 */
CargoType GetDefaultFieldCrop()
{
	for (CargoLabel label : {CT_GRAIN, CT_WHEAT, CT_MAIZE}) {
		CargoType cargo = GetCargoTypeByLabel(label);
		if (IsValidCargoType(cargo)) return cargo;
	}
	return INVALID_CARGO;
}

/**
 * Get the cost of treating one quarter.
 * @param type #FieldTaskType::Fertilise or #FieldTaskType::Spray.
 * @return Cost per quarter.
 */
Money GetFieldTreatmentCost([[maybe_unused]] FieldTaskType type)
{
	return _price[Price::ClearGrass];
}

/**
 * Can a task be done on a quarter right now?
 * @param type The task.
 * @param tile The field tile.
 * @param quarter The quarter of \a tile.
 * @return True if the quarter can be worked on.
 */
static bool IsQuarterEligible(FieldTaskType type, TileIndex tile, uint quarter)
{
	FieldStage stage = GetFieldQuarterStage(tile, quarter);
	switch (type) {
		case FieldTaskType::Cultivate: return stage == FieldStage::Fallow || stage == FieldStage::Withered;
		case FieldTaskType::Sow: return stage == FieldStage::Cultivated;
		case FieldTaskType::Fertilise:
		case FieldTaskType::Spray:
			return stage >= FieldStage::Cultivated && stage <= FieldStage::Maturing && !IsFieldQuarterTreated(tile, quarter, type == FieldTaskType::Spray);
		case FieldTaskType::Harvest: return stage >= FieldStage::Ripe;
		default: NOT_REACHED();
	}
}

/**
 * Will a task become possible on a quarter by itself, i.e. through growth?
 * @param type The task.
 * @param stage Stage of the quarter.
 * @return True if waiting makes sense.
 */
static bool IsQuarterPending(FieldTaskType type, FieldStage stage)
{
	return type == FieldTaskType::Harvest && stage >= FieldStage::Sown && stage <= FieldStage::Maturing;
}

/**
 * Count the quarters a task can be done on right now.
 * @param type The task.
 * @return Number of eligible quarters.
 */
uint Field::CountEligibleQuarters(FieldTaskType type) const
{
	uint count = 0;
	for (TileIndex tile : this->location) {
		if (!IsTileType(tile, TileType::Field)) continue;
		for (uint q = 0; q < 4; q++) {
			if (IsQuarterEligible(type, tile, q)) count++;
		}
	}
	return count;
}

/**
 * Get whether a task of the plan can be worked on.
 * @param index Index into #tasks.
 * @return The state of the task.
 */
Field::TaskState Field::GetTaskState(uint index) const
{
	assert(index < this->tasks.size());
	const Task &task = this->tasks[index];

	bool eligible = false;
	bool pending = false;
	for (TileIndex tile : this->location) {
		if (!IsTileType(tile, TileType::Field)) continue;
		for (uint q = 0; q < 4; q++) {
			if (IsQuarterEligible(task.type, tile, q)) eligible = true;
			if (IsQuarterPending(task.type, GetFieldQuarterStage(tile, q))) pending = true;
		}
	}

	if (eligible) {
		/* A timetabled task starts only in its month; once started it is finished regardless. */
		bool started = index == this->cur_task && this->cur_task_started;
		if (task.start_month != 0 && !started && TimerGameCalendar::month + 1 != task.start_month) return TaskState::Waiting;
		return TaskState::Available;
	}
	return pending ? TaskState::Waiting : TaskState::Done;
}

/**
 * Find the task the plan is really on: the current one, or the first one after it that is not done.
 * @return Index into #tasks; equals #cur_task when nothing needs skipping. Undefined for an empty plan.
 */
uint Field::FindActiveTask() const
{
	uint index = this->cur_task < this->tasks.size() ? this->cur_task : 0;
	for (size_t i = 0; i < this->tasks.size(); i++) {
		/* The started-flag only belongs to #cur_task; GetTaskState handles that. */
		if (this->GetTaskState(index) != TaskState::Done) return index;
		index = (index + 1) % this->tasks.size();
	}
	return index;
}

/** Move the current task past tasks that have nothing left to do. */
void Field::UpdateCurrentTask()
{
	if (this->tasks.empty()) {
		this->cur_task = 0;
		this->cur_task_started = false;
		return;
	}

	uint active = this->FindActiveTask();
	if (active != this->cur_task) {
		this->cur_task = static_cast<uint8_t>(active);
		this->cur_task_started = false;
	}
}

/**
 * Is growth paused because snow covers the field?
 * Snow only exists in sub-arctic, or wherever a NewGRF provides a snow line.
 * @return True if any field tile is above the snow line.
 */
bool Field::IsGrowthPaused() const
{
	if (_settings_game.game_creation.landscape != LandscapeType::Arctic && !IsSnowLineSet()) return false;

	uint8_t snow_line = GetSnowLine();
	for (TileIndex tile : this->location) {
		if (GetTileMaxZ(tile) > snow_line) return true;
	}
	return false;
}

/** Advance every growing quarter by one stage, unless snow covers the field. */
void Field::Grow()
{
	if (this->IsGrowthPaused()) {
		SetWindowDirty(WindowClass::FieldView, this->index);
		return;
	}

	for (TileIndex tile : this->location) {
		if (!IsTileType(tile, TileType::Field)) continue;
		bool changed = false;
		for (uint q = 0; q < 4; q++) {
			FieldStage stage = GetFieldQuarterStage(tile, q);
			if (stage < FieldStage::Sown || stage > FieldStage::Overripe) continue;
			SetFieldQuarterStage(tile, q, static_cast<FieldStage>(to_underlying(stage) + 1));
			changed = true;
		}
		if (changed) MarkTileDirtyByTile(tile);
	}

	this->UpdateCurrentTask();
	SetWindowDirty(WindowClass::FieldView, this->index);
}

/**
 * Do the current task instantly on every eligible quarter.
 * Stand-in for farm machinery until vehicles can work fields.
 * @param flags Operation to perform.
 * @return The cost of the treatments or an error.
 */
CommandCost Field::PerformCurrentTask(DoCommandFlags flags)
{
	if (this->tasks.empty()) return CommandCost(STR_ERROR_FIELD_NOTHING_TO_DO);
	uint active = this->FindActiveTask();
	if (this->GetTaskState(active) != TaskState::Available) return CommandCost(STR_ERROR_FIELD_NOTHING_TO_DO);

	FieldTaskType type = this->tasks[active].type;
	CommandCost cost(ExpensesType::Property);
	if (type == FieldTaskType::Fertilise || type == FieldTaskType::Spray) {
		cost.AddCost(GetFieldTreatmentCost(type) * this->CountEligibleQuarters(type));
	}
	if (!flags.Test(DoCommandFlag::Execute)) return cost;

	if (active != this->cur_task) {
		this->cur_task = static_cast<uint8_t>(active);
		this->cur_task_started = false;
	}

	uint32_t produced = 0;
	for (TileIndex tile : this->location) {
		if (!IsTileType(tile, TileType::Field)) continue;
		for (uint q = 0; q < 4; q++) {
			if (!IsQuarterEligible(type, tile, q)) continue;

			switch (type) {
				case FieldTaskType::Cultivate:
					SetFieldQuarterStage(tile, q, FieldStage::Cultivated);
					SetFieldQuarterTreated(tile, q, false, false);
					SetFieldQuarterTreated(tile, q, true, false);
					break;

				case FieldTaskType::Sow:
					SetFieldQuarterStage(tile, q, FieldStage::Sown);
					this->crop = GetDefaultFieldCrop();
					break;

				case FieldTaskType::Fertilise:
				case FieldTaskType::Spray:
					SetFieldQuarterTreated(tile, q, type == FieldTaskType::Spray, true);
					break;

				case FieldTaskType::Harvest: {
					if (GetFieldQuarterStage(tile, q) != FieldStage::Withered) {
						uint percent = 80 + (IsFieldQuarterTreated(tile, q, false) ? 20 : 0) + (IsFieldQuarterTreated(tile, q, true) ? 20 : 0);
						uint points = FIELD_QUARTER_YIELD * percent + this->harvest_remainder;
						produced += points / 100;
						this->harvest_remainder = points % 100;
					}
					SetFieldQuarterStage(tile, q, FieldStage::Fallow);
					SetFieldQuarterTreated(tile, q, false, false);
					SetFieldQuarterTreated(tile, q, true, false);
					break;
				}

				default: NOT_REACHED();
			}
		}
		MarkTileDirtyByTile(tile);
	}

	if (type == FieldTaskType::Harvest) this->last_harvest = produced;
	this->cur_task_started = true;
	this->UpdateCurrentTask();
	SetWindowDirty(WindowClass::FieldView, this->index);
	return cost;
}

/** Monthly growth of all fields. */
static const IntervalTimer<TimerGameEconomy> _economy_fields_monthly({TimerGameEconomy::Trigger::Month, TimerGameEconomy::Priority::None}, [](auto)
{
	for (Field *f : Field::Iterate()) f->Grow();
});
