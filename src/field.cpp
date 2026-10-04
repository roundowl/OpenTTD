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
#include "newgrf_crop.h"
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
 * Most cargo units one quarter of a crop can yield, with every bonus.
 * @param crop The crop's cargo.
 * @return Rounded up yield at 140%.
 */
uint GetFieldQuarterMaxYield(CargoType crop)
{
	return (GetCropYield(crop) * 140 + 99) / 100;
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

	/* Crops may take several months per growth stage; ripe crops always wither on the same schedule. */
	bool grow_stage = ++this->growth_counter >= GetCropMonthsPerStage(this->crop);
	if (grow_stage) this->growth_counter = 0;

	/* The whole crop grows as one: every growing quarter takes the stage of the most advanced one,
	 * however recently it was sown, so a field ripens all at once. */
	FieldStage top = FieldStage::End;
	for (TileIndex tile : this->location) {
		if (!IsTileType(tile, TileType::Field)) continue;
		for (uint q = 0; q < 4; q++) {
			FieldStage stage = GetFieldQuarterStage(tile, q);
			if (stage < FieldStage::Sown || stage > FieldStage::Overripe) continue;
			if (top == FieldStage::End || stage > top) top = stage;
		}
	}
	if (top == FieldStage::End) {
		SetWindowDirty(WindowClass::FieldView, this->index);
		return;
	}
	FieldStage target;
	if (top >= FieldStage::Ripe) {
		/* Ripe for two months, overripe for two more, then the crop withers. */
		this->ripe_age++;
		target = this->ripe_age >= 4 ? FieldStage::Withered : (this->ripe_age >= 2 ? FieldStage::Overripe : FieldStage::Ripe);
	} else {
		this->ripe_age = 0;
		target = grow_stage ? static_cast<FieldStage>(to_underlying(top) + 1) : top;
	}

	for (TileIndex tile : this->location) {
		if (!IsTileType(tile, TileType::Field)) continue;
		bool changed = false;
		for (uint q = 0; q < 4; q++) {
			FieldStage stage = GetFieldQuarterStage(tile, q);
			if (stage < FieldStage::Sown || stage > FieldStage::Overripe || stage == target) continue;
			SetFieldQuarterStage(tile, q, target);
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

	bool first_quarter = !this->cur_task_started;
	if (type == FieldTaskType::Harvest && first_quarter) this->last_harvest = 0;
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
			if (first_quarter || !IsValidCargoType(this->crop)) {
				/* The planned crop, if it can still be sown here; else the climate default. */
				auto crops = GetAvailableCrops();
				bool planned_ok = std::ranges::find(crops, this->planned_crop) != crops.end();
				this->crop = planned_ok ? this->planned_crop : GetDefaultFieldCrop();
			}
			break;

		case FieldTaskType::Fertilise:
		case FieldTaskType::Spray:
			SetFieldQuarterTreated(tile, quarter, type == FieldTaskType::Spray, true);
			break;

		case FieldTaskType::Harvest: {
			if (GetFieldQuarterStage(tile, quarter) != FieldStage::Withered) {
				uint percent = 80 + (IsFieldQuarterTreated(tile, quarter, false) ? 20 : 0) + (IsFieldQuarterTreated(tile, quarter, true) ? 20 : 0);
				uint points = GetCropYield(this->crop) * percent + this->harvest_remainder;
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
 * A field is a grid of half-tile cells (u, v): u runs along the short side, v along the long side,
 * both pointing away from the entry corner, whose tile holds the cells (0..1, 0..1).
 * Rows run along u, so they are short and there are many of them for machines to share.
 */
struct FieldFrame {
	int x0; ///< World X of the north corner of the entry corner tile.
	int y0; ///< World Y of the north corner of the entry corner tile.
	int sx; ///< +1 if the field extends towards higher X from the corner, else -1.
	int sy; ///< +1 if the field extends towards higher Y from the corner, else -1.
	bool u_is_x; ///< Whether u (the short side) runs along world X.
	int lu; ///< Number of cells along u, the short side.
	int lv; ///< Number of cells along v, the long side.

	explicit FieldFrame(const Field &f)
	{
		this->x0 = TileX(f.corner) * TILE_SIZE;
		this->y0 = TileY(f.corner) * TILE_SIZE;
		this->sx = TileX(f.corner) == TileX(f.location.tile) ? 1 : -1;
		this->sy = TileY(f.corner) == TileY(f.location.tile) ? 1 : -1;
		this->u_is_x = f.location.w < f.location.h;
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

/** A half-tile cell of a field, in the corner-relative frame of #FieldFrame. */
struct FieldCell {
	int u;
	int v;
	bool work;

	bool Is(int u, int v) const { return this->u == u && this->v == v; }
};

/** The corner cell all routes start from and end at. */
static constexpr FieldCell FIELD_CORNER_CELL{1, 1, false};

/** Builder of a list of cells to drive through. */
struct FieldPathBuilder {
	std::vector<FieldCell> cells;

	void Add(int u, int v, bool work)
	{
		if (!this->cells.empty() && this->cells.back().Is(u, v)) {
			this->cells.back().work |= work;
			return;
		}
		this->cells.push_back({u, v, work});
	}

	const FieldCell &Last() const { return this->cells.back(); }

	void AddCells(const std::vector<FieldCell> &list)
	{
		for (const FieldCell &c : list) this->Add(c.u, c.v, c.work);
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

	/** From the last cell back to the corner cell (1, 1), outbound on the outer lane (u = 0). */
	void ToCorner()
	{
		FieldCell from = this->Last();
		if (from.Is(1, 1)) return;
		if (from.v == 0) {
			this->Add(1, 0, false);
		} else if (from.v == 1) {
			/* End of the inner headland, (2, 1): next to the corner. */
		} else {
			if (from.u >= 2) this->Add(1, from.v, false);
			if (from.u >= 1 && from.v >= 3) this->Add(0, from.v, false);
			from = this->Last();
			if (from.u == 0) {
				for (int v = from.v - 1; v >= 1; v--) this->Add(0, v, false);
			}
		}
		this->Add(1, 1, false);
	}

	/** From the end of one row pair to the start of another along the short headland. */
	void RowToRow(int to_v)
	{
		FieldCell from = this->Last();
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

	/** Drive from the last cell to \a to, staying on the lanes. */
	void Transit(const FieldCell &to)
	{
		FieldCell from = this->Last();
		if (from.Is(to.u, to.v)) return;
		if (from.Is(0, 2) && to.Is(1, 2)) {
			/* End of the outer headland straight onto the inner one. */
			this->Add(1, 2, false);
			return;
		}
		if (from.u == 2 && from.v >= 3 && to.u == 2 && to.v >= 2) {
			this->RowToRow(to.v);
			return;
		}
		this->ToCorner();
		if (!to.Is(1, 1)) this->FromCorner(to.u, to.v);
	}
};

/**
 * Number of work segments of a field.
 * Segment 0 is the entry strip: the tile column alongside the entry corner, along the long side,
 * which also carries the lanes to every row. Segment 1 + j is row pair j, running along the short
 * side right up to the far fence; row pair 0 starts straight off the corner tile.
 */
static int GetFieldSegmentCount(const FieldFrame &fr)
{
	return 1 + fr.lv / 2;
}

/**
 * The cells of a work segment, in driving order.
 * @param fr The field frame.
 * @param segment The segment, see #GetFieldSegmentCount.
 * @return The cells, all marked as work.
 */
static std::vector<FieldCell> GetFieldSegmentCells(const FieldFrame &fr, int segment)
{
	std::vector<FieldCell> cells;
	if (segment == 0) {
		/* Out on the inner lane, back on the outer one, ending next to the corner. */
		for (int v = 2; v <= fr.lv - 1; v++) cells.push_back({1, v, true});
		for (int v = fr.lv - 1; v >= 2; v--) cells.push_back({0, v, true});
		return cells;
	}
	int j = segment - 1;
	for (int u = 2; u <= fr.lu - 1; u++) cells.push_back({u, 2 * j, true});
	for (int u = fr.lu - 1; u >= 2; u--) cells.push_back({u, 2 * j + 1, true});
	return cells;
}

/** Cells of a work route: from the vehicle's start cell to the segment, then the segment. */
static std::vector<FieldCell> GetFieldWorkCells(const FieldFrame &fr, const RoadVehFieldWork &work)
{
	FieldPathBuilder pb;
	pb.Add(work.from_u, work.from_v, false);
	auto segment = GetFieldSegmentCells(fr, work.segment);
	pb.Transit(segment.front());
	pb.AddCells(segment);
	return pb.cells;
}

/**
 * Build the route of a vehicle inside a field.
 * @param f The field.
 * @param work The vehicle's field state.
 * @return Waypoints; the last one is the bay, the end of the segment for #FieldRouteKind::Work,
 *         or the service quarter for #FieldRouteKind::ToPark.
 */
std::vector<FieldWaypoint> BuildFieldRoute(const Field &f, const RoadVehFieldWork &work)
{
	FieldFrame fr(f);
	FieldWaypoint bay{work.bay_x, work.bay_y, false};
	std::vector<FieldWaypoint> route;

	switch (work.kind) {
		case FieldRouteKind::Work:
			for (const auto &c : GetFieldWorkCells(fr, work)) route.push_back(fr.Centre(c.u, c.v, c.work));
			break;

		case FieldRouteKind::Backtrack: {
			auto cells = GetFieldWorkCells(fr, work);
			int from = std::min<int>(work.backtrack_from, static_cast<int>(cells.size()) - 1);
			FieldPathBuilder pb;
			for (int i = from; i >= 0; i--) pb.Add(cells[i].u, cells[i].v, false);
			pb.ToCorner();
			for (const auto &c : pb.cells) route.push_back(fr.Centre(c.u, c.v, false));
			route.push_back(bay);
			break;
		}

		case FieldRouteKind::ToCorner: {
			FieldPathBuilder pb;
			pb.Add(work.from_u, work.from_v, false);
			pb.ToCorner();
			for (const auto &c : pb.cells) route.push_back(fr.Centre(c.u, c.v, false));
			route.push_back(bay);
			break;
		}

		case FieldRouteKind::ToPark:
		case FieldRouteKind::Parked:
		case FieldRouteKind::ToService:
		case FieldRouteKind::Servicing:
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
 * Number of work segments of a field.
 * @param f The field.
 * @return Two headland passes plus one per interior row pair.
 */
int GetFieldSegmentCount(const Field &f)
{
	return GetFieldSegmentCount(FieldFrame(f));
}

/**
 * Get the last cell of a work segment, where a vehicle is when it has finished it.
 * @param f The field.
 * @param segment The segment.
 * @return Its (u, v) cell.
 */
std::pair<int, int> GetFieldSegmentEnd(const Field &f, int segment)
{
	FieldFrame fr(f);
	FieldCell end = GetFieldSegmentCells(fr, segment).back();
	return {end.u, end.v};
}

/**
 * Count the quarters of a segment a task can be done on.
 * @param f The field.
 * @param fr Its frame.
 * @param segment The segment.
 * @param type The task.
 * @return Number of eligible quarters.
 */
static uint CountSegmentEligibleQuarters(const Field &f, const FieldFrame &fr, int segment, FieldTaskType type)
{
	uint count = 0;
	for (const FieldCell &c : GetFieldSegmentCells(fr, segment)) {
		FieldWaypoint wp = fr.Centre(c.u, c.v, false);
		TileIndex tile = TileVirtXY(wp.x, wp.y);
		if (!IsTileType(tile, TileType::Field) || GetFieldIndex(tile) != f.index) continue;
		uint quarter = ((wp.y & TILE_UNIT_MASK) >= TILE_SIZE / 2 ? 2 : 0) | ((wp.x & TILE_UNIT_MASK) >= TILE_SIZE / 2 ? 1 : 0);
		if (IsQuarterEligible(type, tile, quarter)) count++;
	}
	return count;
}

/**
 * Count the quarters of a segment the vehicle's task can be done on.
 * @param f The field.
 * @param segment The segment.
 * @param type The task.
 * @return Number of eligible quarters.
 */
uint CountFieldSegmentEligibleQuarters(const Field &f, int segment, FieldTaskType type)
{
	return CountSegmentEligibleQuarters(f, FieldFrame(f), segment, type);
}

/**
 * Is a segment held by a vehicle working or backing out of it?
 * @param f The field.
 * @param segment The segment.
 * @param self The vehicle asking, which does not count.
 * @return True if reserved.
 */
static bool IsFieldSegmentReserved(const Field &f, int segment, const RoadVehicle *self)
{
	for (const RoadVehicle *rv : RoadVehicle::Iterate()) {
		if (rv == self || rv->state != RVSB_IN_FIELD || rv->field_work.field != f.index) continue;
		if (rv->field_work.segment != segment) continue;
		if (rv->field_work.kind == FieldRouteKind::Work || rv->field_work.kind == FieldRouteKind::Backtrack) return true;
	}
	return false;
}

/**
 * Find a segment a vehicle can work on.
 * The entry strip and the row pair beside the corner come first; every other row is handed out
 * once the strip's inner lane leading to it has been worked.
 * @param v The vehicle.
 * @param f The field.
 * @param type The task.
 * @param[out] busy Set when there is work, but every segment with work is taken.
 * @return The segment, or -1.
 */
static int FindFieldSegment(const RoadVehicle *v, const Field &f, FieldTaskType type, bool &busy)
{
	FieldFrame fr(f);
	int count = GetFieldSegmentCount(fr);

	/* The row pair next to the corner needs no strip lanes and the strip needs none either: both
	 * are open at once. The other rows are reached over the strip's inner lane, so a row opens as
	 * soon as the inner lane up to it has been worked: machines follow the strip machine up the
	 * field. Nearest first. */
	auto inner_lane_clear_to = [&](int to_v) {
		for (int lane_v = 2; lane_v <= to_v; lane_v++) {
			FieldWaypoint wp = fr.Centre(1, lane_v, false);
			TileIndex tile = TileVirtXY(wp.x, wp.y);
			if (!IsTileType(tile, TileType::Field) || GetFieldIndex(tile) != f.index) continue;
			uint quarter = ((wp.y & TILE_UNIT_MASK) >= TILE_SIZE / 2 ? 2 : 0) | ((wp.x & TILE_UNIT_MASK) >= TILE_SIZE / 2 ? 1 : 0);
			if (IsQuarterEligible(type, tile, quarter)) return false;
		}
		return true;
	};
	std::vector<int> order{1, 0};
	for (int s = 2; s < count; s++) order.push_back(s);

	busy = false;
	for (int s : order) {
		if (CountSegmentEligibleQuarters(f, fr, s, type) == 0) continue;
		if (s >= 2 && !inner_lane_clear_to(2 * (s - 1))) {
			busy = true; // Waiting for the strip machine to clear the lane.
			continue;
		}
		if (IsFieldSegmentReserved(f, s, v)) {
			busy = true;
			continue;
		}
		return s;
	}
	return -1;
}

/**
 * Can a vehicle start working on a field right now?
 * @param v The vehicle.
 * @param f The field.
 * @param[out] task The task it would do, may be \c nullptr.
 * @param[out] segment The segment it would work, may be \c nullptr.
 * @return Whether it can start, or has to wait for other machines.
 */
FieldWorkAvailability CanStartFieldWork(const RoadVehicle *v, Field *f, FieldTaskType *task, int *segment)
{
	f->UpdateCurrentTask();
	if (f->tasks.empty() || f->GetTaskState(f->cur_task) != Field::TaskState::Available) return FieldWorkAvailability::None;

	FieldTaskType type = f->tasks[f->cur_task].type;
	if (!CanFieldMachineDo(v, type)) return FieldWorkAvailability::None;
	if (type == FieldTaskType::Harvest) {
		if (!IsValidCargoType(f->crop) || v->cargo_type != f->crop) return FieldWorkAvailability::None;
		if (v->cargo.StoredCount() + GetFieldQuarterMaxYield(f->crop) > v->cargo_cap) return FieldWorkAvailability::None;
	}

	bool busy;
	int s = FindFieldSegment(v, *f, type, busy);
	if (s < 0) return busy ? FieldWorkAvailability::Busy : FieldWorkAvailability::None;

	if (task != nullptr) *task = type;
	if (segment != nullptr) *segment = s;
	return FieldWorkAvailability::Start;
}

/**
 * Should a farm vehicle stay in the depot because none of its fields has work for it?
 * @param v The vehicle, in a depot.
 * @return True if it has field orders, no cargo to deliver, and nothing to do on any of its fields.
 */
bool IsFieldMachineIdle(const RoadVehicle *v)
{
	if (!IsFieldMachine(v) || v->cargo.StoredCount() > 0) return false;
	bool has_field = false;
	for (const Order &order : v->Orders()) {
		if (!order.IsType(OT_WORK_FIELD)) continue;
		Field *f = Field::GetByStation(order.GetDestination().ToStationID());
		if (f == nullptr) continue;
		has_field = true;
		if (CanStartFieldWork(v, f, nullptr, nullptr) != FieldWorkAvailability::None) return false;
	}
	return has_field;
}

/**
 * Does a vehicle have any order to stop at a station, i.e. somewhere to deliver cargo?
 * @param v The vehicle.
 * @return True if so.
 */
bool HasStationOrder(const Vehicle *v)
{
	return std::ranges::any_of(v->Orders(), [](const Order &o) { return o.IsType(OT_GOTO_STATION); });
}

/**
 * Decide what a farm vehicle does at the entry corner of a field.
 * Does not change any vehicle state.
 * @param v The vehicle.
 * @param f The field of the corner.
 * @return The action.
 */
FieldCornerAction EvaluateFieldCorner(const RoadVehicle *v, Field *f)
{
	if (!v->current_order.IsType(OT_WORK_FIELD) || v->current_order.GetDestination() != f->station) return FieldCornerAction::Leave;

	switch (CanStartFieldWork(v, f, nullptr, nullptr)) {
		case FieldWorkAvailability::Start: return FieldCornerAction::StartWork;
		case FieldWorkAvailability::Busy: return FieldCornerAction::Park; // Wait for the other machines.
		case FieldWorkAvailability::None: break;
	}

	/* A load with nowhere to go in the orders is delivered to this field's own station. */
	if (v->cargo.StoredCount() > 0 && !HasStationOrder(v)) return FieldCornerAction::Unload;

	/* Nothing to do here. With other kinds of orders, carry on with them. */
	bool only_fields = true;
	bool other_field_has_work = false;
	for (const Order &order : v->Orders()) {
		if (!order.IsType(OT_WORK_FIELD)) {
			/* Stopping at a field's station with nothing to unload is no reason to go away. */
			if (order.IsType(OT_GOTO_STATION) && v->cargo.StoredCount() == 0 && Field::GetByStation(order.GetDestination().ToStationID()) != nullptr) continue;
			only_fields = false;
			break;
		}
		Field *other = Field::GetByStation(order.GetDestination().ToStationID());
		if (other != nullptr && other != f && CanStartFieldWork(v, other, nullptr, nullptr) == FieldWorkAvailability::Start) other_field_has_work = true;
	}
	if (only_fields && !other_field_has_work) return FieldCornerAction::Park;
	return FieldCornerAction::MoveOn;
}
