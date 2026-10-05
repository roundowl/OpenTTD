/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file field_base.h Base for player-built farm fields. */

#ifndef FIELD_BASE_H
#define FIELD_BASE_H

#include "core/pool_type.hpp"
#include "field_type.h"
#include "cargo_type.h"
#include "company_type.h"
#include "command_type.h"
#include "station_type.h"
#include "tilearea_orthogonal.h"
#include "timer/timer_game_calendar.h"

using FieldPool = Pool<Field, FieldID, 64>;
extern FieldPool _field_pool;

/**
 * A player-built farm field.
 * The field is a rectangle; one of its corner tiles is a truck stop (the entry corner),
 * all other tiles are #TileType::Field tiles.
 */
struct Field : FieldPool::PoolItem<&_field_pool> {
	Owner owner = INVALID_OWNER; ///< Owner of the field.
	TileArea location{INVALID_TILE, 0, 0}; ///< Whole field rectangle, including the entry corner.
	TileIndex corner = INVALID_TILE; ///< The entry corner tile; a truck stop of #station.
	StationID station = StationID::Invalid(); ///< Station the entry corner belongs to.
	TimerGameCalendar::Date build_date{}; ///< Date of construction.

	/** One step of the field's work plan. */
	struct Task {
		FieldTaskType type = FieldTaskType::Cultivate; ///< What to do.
		uint8_t start_month = 0; ///< Earliest calendar month to start, 1..12, or 0 for any time.
	};

	/** Whether a task can be worked on. */
	enum class TaskState : uint8_t {
		Available, ///< Some quarters can be worked on right now.
		Waiting, ///< Nothing to do yet, but there will be: crops still growing, or before the start month.
		Done, ///< Nothing left to do; the plan moves on.
	};

	std::vector<Task> tasks; ///< The work plan, worked through cyclically.
	uint8_t cur_task = 0; ///< Index into #tasks of the current task.
	bool cur_task_started = false; ///< Whether any quarter has been worked for the current task.
	CargoType crop = INVALID_CARGO; ///< Cargo produced by the current or last sown crop.
	CargoType planned_crop = INVALID_CARGO; ///< Crop to sow next, or #INVALID_CARGO for the climate default.
	uint8_t growth_counter = 0; ///< Months since the growing quarters last advanced a stage.
	uint8_t ripe_age = 0; ///< Months the crop has been ripe.
	uint16_t harvest_remainder = 0; ///< Fraction of a cargo unit carried over between harvested quarters, in 1/100.
	uint32_t last_harvest = 0; ///< Cargo units produced by the most recent harvest run.
	Money pending_credit = 0; ///< Work credits of field passes since the last harvest, still to be charged to the harvested cargo.

	Field(FieldID index) : FieldPool::PoolItem<&_field_pool>(index) {}
	~Field() {}

	TaskState GetTaskState(uint index) const;
	uint FindActiveTask() const;
	void UpdateCurrentTask();
	bool IsGrowthPaused() const;
	void Grow();
	uint CountEligibleQuarters(FieldTaskType type) const;
	CommandCost PerformCurrentTask(DoCommandFlags flags);
	int WorkQuarter(TileIndex tile, uint quarter, FieldTaskType type);
	CargoType GetCreditCrop() const;

	/**
	 * Get the current task, if the plan has any.
	 * @return The current task or \c nullptr.
	 */
	const Task *GetCurrentTask() const
	{
		return this->cur_task < this->tasks.size() ? &this->tasks[this->cur_task] : nullptr;
	}

	static Field *GetByTile(TileIndex tile);
	static Field *GetByCornerTile(TileIndex tile);
	static Field *GetByStation(StationID station);
};

CargoType GetDefaultFieldCrop();
uint GetFieldQuarterMaxYield(CargoType crop);
Money GetFieldWorkCredit(CargoType crop, uint units);
Money GetFieldTreatmentCost(FieldTaskType type);

#endif /* FIELD_BASE_H */
