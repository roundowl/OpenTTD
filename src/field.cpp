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
#include "engine_base.h"
#include "field_func.h"
#include "roadveh.h"
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
 * Is a vehicle farm machinery, i.e. able to work on fields at all?
 * @param v The vehicle.
 * @return True for road vehicles with field tasks.
 */
bool IsFieldMachine(const Vehicle *v)
{
	return v->type == VehicleType::Road && RoadVehicle::From(v)->GetEngine()->VehInfo<RoadVehicleInfo>().field_tasks != 0;
}

/**
 * Can a vehicle do a particular task on a field?
 * @param v The vehicle.
 * @param type The task.
 * @return True if the vehicle has the equipment for it.
 */
bool CanFieldMachineDo(const Vehicle *v, FieldTaskType type)
{
	return IsFieldMachine(v) && HasBit(RoadVehicle::From(v)->GetEngine()->VehInfo<RoadVehicleInfo>().field_tasks, to_underlying(type));
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
	CommandCost cost(ExpensesType::Other);
	if (type == FieldTaskType::Fertilise || type == FieldTaskType::Spray) {
		cost.AddCost(GetFieldTreatmentCost(type) * this->CountEligibleQuarters(type));
	}
	if (!flags.Test(DoCommandFlag::Execute)) return cost;

	if (active != this->cur_task) {
		this->cur_task = static_cast<uint8_t>(active);
		this->cur_task_started = false;
	}

	for (TileIndex tile : this->location) {
		if (!IsTileType(tile, TileType::Field)) continue;
		for (uint q = 0; q < 4; q++) this->WorkQuarter(tile, q, type);
	}

	this->UpdateCurrentTask();
	SetWindowDirty(WindowClass::FieldView, this->index);
	return cost;
}

/**
 * Do a task on one quarter, if it is eligible.
 * @param tile The field tile.
 * @param quarter The quarter of \a tile.
 * @param type The task.
 * @return Cargo units harvested, or -1 if the quarter was not eligible.
 */
int Field::WorkQuarter(TileIndex tile, uint quarter, FieldTaskType type)
{
	if (!IsTileType(tile, TileType::Field) || GetFieldIndex(tile) != this->index) return -1;
	if (!IsQuarterEligible(type, tile, quarter)) return -1;

	if (type == FieldTaskType::Harvest && !this->cur_task_started) this->last_harvest = 0;
	this->cur_task_started = true;

	int produced = 0;
	switch (type) {
		case FieldTaskType::Cultivate:
			SetFieldQuarterStage(tile, quarter, FieldStage::Cultivated);
			SetFieldQuarterTreated(tile, quarter, false, false);
			SetFieldQuarterTreated(tile, quarter, true, false);
			break;

		case FieldTaskType::Sow:
			SetFieldQuarterStage(tile, quarter, FieldStage::Sown);
			this->crop = GetDefaultFieldCrop();
			break;

		case FieldTaskType::Fertilise:
		case FieldTaskType::Spray:
			SetFieldQuarterTreated(tile, quarter, type == FieldTaskType::Spray, true);
			break;

		case FieldTaskType::Harvest: {
			if (GetFieldQuarterStage(tile, quarter) != FieldStage::Withered) {
				uint percent = 80 + (IsFieldQuarterTreated(tile, quarter, false) ? 20 : 0) + (IsFieldQuarterTreated(tile, quarter, true) ? 20 : 0);
				uint points = FIELD_QUARTER_YIELD * percent + this->harvest_remainder;
				produced = points / 100;
				this->harvest_remainder = points % 100;
				this->last_harvest += produced;
			}
			SetFieldQuarterStage(tile, quarter, FieldStage::Fallow);
			SetFieldQuarterTreated(tile, quarter, false, false);
			SetFieldQuarterTreated(tile, quarter, true, false);
			break;
		}

		default: NOT_REACHED();
	}

	MarkTileDirtyByTile(tile);
	SetWindowDirty(WindowClass::FieldView, this->index);
	return produced;
}

/** Monthly growth of all fields. */
static const IntervalTimer<TimerGameEconomy> _economy_fields_monthly({TimerGameEconomy::Trigger::Month, TimerGameEconomy::Priority::None}, [](auto)
{
	for (Field *f : Field::Iterate()) f->Grow();
});

/**
 * The geometry of a field as seen from its entry corner.
 * A field is a grid of half-tile cells (u, v): u runs along the long side, v along the short side,
 * both pointing away from the entry corner, whose tile holds the cells (0..1, 0..1).
 */
struct FieldFrame {
	int x0; ///< World X of the north corner of the entry corner tile.
	int y0; ///< World Y of the north corner of the entry corner tile.
	int sx; ///< +1 if the field extends towards higher X from the corner, else -1.
	int sy; ///< +1 if the field extends towards higher Y from the corner, else -1.
	bool u_is_x; ///< Whether u runs along world X.
	int lu; ///< Number of cells along u.
	int lv; ///< Number of cells along v.

	explicit FieldFrame(const Field &f)
	{
		this->x0 = TileX(f.corner) * TILE_SIZE;
		this->y0 = TileY(f.corner) * TILE_SIZE;
		this->sx = TileX(f.corner) == TileX(f.location.tile) ? 1 : -1;
		this->sy = TileY(f.corner) == TileY(f.location.tile) ? 1 : -1;
		this->u_is_x = f.location.w >= f.location.h;
		this->lu = 2 * (this->u_is_x ? f.location.w : f.location.h);
		this->lv = 2 * (this->u_is_x ? f.location.h : f.location.w);
	}

	/** World coordinates of the centre of cell (u, v). */
	FieldWaypoint Centre(int u, int v, bool work) const
	{
		int ax = this->u_is_x ? u : v;
		int ay = this->u_is_x ? v : u;
		return {this->x0 + 8 + this->sx * (ax * 8 - 4), this->y0 + 8 + this->sy * (ay * 8 - 4), work};
	}
};

/** Builder of a list of cells to drive through. */
struct FieldPathBuilder {
	struct Cell {
		int u;
		int v;
		bool work;
	};
	std::vector<Cell> cells;

	void Add(int u, int v, bool work)
	{
		if (!this->cells.empty() && this->cells.back().u == u && this->cells.back().v == v) {
			this->cells.back().work |= work;
			return;
		}
		this->cells.push_back({u, v, work});
	}

	const Cell &Last() const { return this->cells.back(); }

	/** Headland ring \a r (0 = outer, 1 = inner), running from next to the corner round to next to the corner. */
	static std::vector<Cell> Ring(const FieldFrame &fr, int r)
	{
		std::vector<Cell> ring;
		for (int u = 2; u <= fr.lu - 1 - r; u++) ring.push_back({u, r, true});
		for (int v = r + 1; v <= fr.lv - 1 - r; v++) ring.push_back({fr.lu - 1 - r, v, true});
		for (int u = fr.lu - 2 - r; u >= r; u--) ring.push_back({u, fr.lv - 1 - r, true});
		for (int v = fr.lv - 2 - r; v >= 2; v--) ring.push_back({r, v, true});
		return ring;
	}

	void AddCells(const std::vector<Cell> &list)
	{
		for (const Cell &c : list) this->Add(c.u, c.v, c.work);
	}

	/** From the corner cell (1, 1) to the start of a segment, inbound on the inner lane (u = 1). */
	void FromCorner(int u, int v)
	{
		if (v == 0) {
			this->Add(1, 0, false);
		} else {
			for (int i = 2; i <= v; i++) this->Add(1, i, false);
		}
		this->Add(u, v, false);
	}

	/** From the end of a segment back to the corner cell (1, 1), outbound on the outer lane (u = 0). */
	void ToCorner()
	{
		Cell from = this->Last();
		if (from.u >= 2 && from.v >= 2) {
			this->Add(1, from.v, false);
			this->Add(0, from.v, false);
			from = this->Last();
		}
		for (int v = from.v - 1; v >= 1; v--) this->Add(from.u, v, false);
		this->Add(1, 1, false);
	}

	/** Work one interior row pair: out on lane 2 + 2k, back on lane 3 + 2k. */
	void Row(const FieldFrame &fr, int k)
	{
		for (int u = 2; u <= fr.lu - 3; u++) this->Add(u, 2 + 2 * k, true);
		for (int u = fr.lu - 3; u >= 2; u--) this->Add(u, 3 + 2 * k, true);
	}

	/** From the end of one row pair to the start of another along the short headland. */
	void RowToRow(int k)
	{
		Cell from = this->Last();
		int to_v = 2 + 2 * k;
		if (to_v > from.v) {
			/* Away from the corner: inner lane. */
			for (int v = from.v; v <= to_v; v++) this->Add(1, v, false);
		} else {
			/* Towards the corner: outer lane. */
			this->Add(1, from.v, false);
			for (int v = from.v; v >= to_v; v--) this->Add(0, v, false);
			this->Add(1, to_v, false);
		}
		this->Add(2, to_v, false);
	}
};

/**
 * Build the route of a vehicle inside a field.
 * Harvesting does the two headland passes first, everything else does the inner rows first,
 * starting with the row furthest from the corner, so the vehicle ends near the corner.
 * @param f The field.
 * @param work The vehicle's field state.
 * @return Waypoints; the last one is the bay, or the service quarter for #FieldRouteKind::ToPark.
 */
std::vector<FieldWaypoint> BuildFieldRoute(const Field &f, const RoadVehFieldWork &work)
{
	FieldFrame fr(f);
	FieldPathBuilder pb;
	FieldWaypoint bay{work.bay_x, work.bay_y, false};

	/* Number of interior tile rows; rows need an interior both ways. */
	int rows = (fr.lu >= 6 && fr.lv >= 6) ? fr.lv / 2 - 2 : 0;

	auto work_route = [&]() {
		pb.Add(1, 1, false);
		if (work.task == FieldTaskType::Harvest) {
			pb.FromCorner(2, 0);
			pb.AddCells(FieldPathBuilder::Ring(fr, 0));
			auto inner = FieldPathBuilder::Ring(fr, 1);
			std::reverse(inner.begin(), inner.end());
			pb.AddCells(inner);
			if (rows > 0) {
				pb.Add(1, 1, false);
				pb.FromCorner(2, 2);
				pb.Row(fr, 0);
				for (int k = 1; k < rows; k++) {
					pb.RowToRow(k);
					pb.Row(fr, k);
				}
			}
			pb.ToCorner();
		} else {
			if (rows > 0) {
				pb.FromCorner(2, 2 + 2 * (rows - 1));
				pb.Row(fr, rows - 1);
				for (int k = rows - 2; k >= 0; k--) {
					pb.RowToRow(k);
					pb.Row(fr, k);
				}
				pb.ToCorner();
			}
			pb.FromCorner(2, 0);
			pb.AddCells(FieldPathBuilder::Ring(fr, 0));
			auto inner = FieldPathBuilder::Ring(fr, 1);
			std::reverse(inner.begin(), inner.end());
			pb.AddCells(inner);
			pb.ToCorner();
		}
	};

	std::vector<FieldWaypoint> route;
	switch (work.kind) {
		case FieldRouteKind::Work:
			work_route();
			for (const auto &c : pb.cells) route.push_back(fr.Centre(c.u, c.v, c.work));
			route.push_back(bay);
			break;

		case FieldRouteKind::Backtrack: {
			work_route();
			int from = std::min<int>(work.backtrack_from, static_cast<int>(pb.cells.size()) - 1);
			for (int i = from; i >= 0; i--) route.push_back(fr.Centre(pb.cells[i].u, pb.cells[i].v, false));
			route.push_back(bay);
			break;
		}

		case FieldRouteKind::ToPark:
		case FieldRouteKind::Parked:
			route.push_back(fr.Centre(1, 1, false));
			route.push_back(fr.Centre(0, 1, false));
			route.push_back(fr.Centre(0, 2, false));
			break;

		case FieldRouteKind::FromPark:
			route.push_back(fr.Centre(0, 1, false));
			route.push_back(fr.Centre(1, 1, false));
			route.push_back(bay);
			break;
	}
	return route;
}

/**
 * Is another vehicle working on a field?
 * @param f The field.
 * @param self The vehicle asking, which does not count.
 * @return True if a vehicle is driving a work route there.
 */
static bool IsFieldBusy(const Field *f, const RoadVehicle *self)
{
	for (const RoadVehicle *rv : RoadVehicle::Iterate()) {
		if (rv == self || rv->state != RVSB_IN_FIELD || rv->field_work.field != f->index) continue;
		if (rv->field_work.kind == FieldRouteKind::Work || rv->field_work.kind == FieldRouteKind::Backtrack) return true;
	}
	return false;
}

/**
 * Can a vehicle start a work run on a field right now?
 * @param v The vehicle.
 * @param f The field.
 * @param[out] task The task it would do.
 * @return True if it can.
 */
bool CanStartFieldWork(const RoadVehicle *v, Field *f, FieldTaskType *task)
{
	f->UpdateCurrentTask();
	if (f->tasks.empty() || f->GetTaskState(f->cur_task) != Field::TaskState::Available) return false;

	FieldTaskType type = f->tasks[f->cur_task].type;
	if (!CanFieldMachineDo(v, type)) return false;
	if (type == FieldTaskType::Harvest) {
		if (!IsValidCargoType(f->crop) || v->cargo_type != f->crop) return false;
		if (v->cargo.StoredCount() + FIELD_QUARTER_MAX_YIELD > v->cargo_cap) return false;
	}
	if (IsFieldBusy(f, v)) return false;

	if (task != nullptr) *task = type;
	return true;
}

/**
 * Decide what a farm vehicle does at the entry corner of a field.
 * Does not change any state.
 * @param v The vehicle.
 * @param f The field of the corner.
 * @return The action.
 */
FieldCornerAction EvaluateFieldCorner(const RoadVehicle *v, Field *f)
{
	if (!v->current_order.IsType(OT_WORK_FIELD) || v->current_order.GetDestination() != f->station) return FieldCornerAction::Leave;
	if (CanStartFieldWork(v, f, nullptr)) return FieldCornerAction::StartWork;

	/* Nothing to do here. With other kinds of orders, carry on with them. */
	bool only_fields = true;
	bool other_field_has_work = false;
	for (const Order &order : v->Orders()) {
		if (!order.IsType(OT_WORK_FIELD)) {
			only_fields = false;
			break;
		}
		Field *other = Field::GetByStation(order.GetDestination().ToStationID());
		if (other != nullptr && other != f && CanStartFieldWork(v, other, nullptr)) other_field_has_work = true;
	}
	if (only_fields && !other_field_has_work) return FieldCornerAction::Park;
	return FieldCornerAction::MoveOn;
}
