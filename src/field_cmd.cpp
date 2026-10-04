/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file field_cmd.cpp Handling of player-built farm fields. */

#include "stdafx.h"
#include "field_base.h"
#include "field_cmd.h"
#include "field_func.h"
#include "field_map.h"
#include "bridge_map.h"
#include "clear_map.h"
#include "command_func.h"
#include "company_base.h"
#include "company_func.h"
#include "core/pool_func.hpp"
#include "economy_func.h"
#include "industry_type.h"
#include "landscape.h"
#include "landscape_cmd.h"
#include "newgrf_roadstop.h"
#include "object_base.h"
#include "road.h"
#include "road_func.h"
#include "road_map.h"
#include "slope_func.h"
#include "station_base.h"
#include "station_cmd.h"
#include "station_map.h"
#include "vehicle_func.h"
#include "viewport_func.h"
#include "window_func.h"
#include "timer/timer_game_calendar.h"

#include "table/strings.h"
#include "table/sprites.h"

#include "safeguards.h"

FieldPool _field_pool("Field");
INSTANTIATE_POOL_METHODS(Field)

void DrawRailFenceOnBorder(const TileInfo *ti, DiagDirection side, PaletteID pal);

/**
 * Get the field a field tile belongs to.
 * @param tile A #TileType::Field tile.
 * @return The field.
 */
/* static */ Field *Field::GetByTile(TileIndex tile)
{
	return Field::Get(GetFieldIndex(tile));
}

/**
 * Get the field whose entry corner is the given tile.
 * @param tile Any tile.
 * @return The field, or \c nullptr if the tile is no field entry corner.
 */
/* static */ Field *Field::GetByCornerTile(TileIndex tile)
{
	for (Field *f : Field::Iterate()) {
		if (f->corner == tile) return f;
	}
	return nullptr;
}

/**
 * Does the neighbouring tile in the given direction offer a road connection back to us?
 * @param tile The tile we are looking from.
 * @param dir Direction towards the neighbour.
 * @return True if the neighbour has road bits pointing at \a tile.
 */
static bool HasRoadConnectionFrom(TileIndex tile, DiagDirection dir)
{
	TileIndex neighbour = TileAddByDiagDir(tile, dir);
	if (!IsValidTile(neighbour)) return false;
	return GetAnyRoadBits(neighbour, RoadTramType::Road).Any(DiagDirToRoadBits(ReverseDiagDir(dir)));
}

/**
 * Choose which way the truck bay of a field's entry corner faces.
 * It faces one of the two sides pointing out of the field, preferring a side with a road next to it.
 * @param area The field rectangle.
 * @param corner The entry corner tile; one of the corners of \a area.
 * @return Entrance direction of the truck bay.
 */
static DiagDirection ChooseFieldEntrance(const TileArea &area, TileIndex corner)
{
	DiagDirection out_x = (TileX(corner) == TileX(area.tile)) ? DiagDirection::NE : DiagDirection::SW;
	DiagDirection out_y = (TileY(corner) == TileY(area.tile)) ? DiagDirection::NW : DiagDirection::SE;

	bool road_x = HasRoadConnectionFrom(corner, out_x);
	bool road_y = HasRoadConnectionFrom(corner, out_y);
	if (road_x != road_y) return road_x ? out_x : out_y;

	/* No preference from roads; face out of the longer side. */
	return area.w >= area.h ? out_y : out_x;
}

/**
 * Build a farm field.
 * @param flags Operation to perform.
 * @param tile End tile of the drag.
 * @param start_tile Start tile of the drag; becomes the entry corner.
 * @param rt Road type for the truck stop at the entry corner.
 * @return The cost of this operation or an error.
 */
CommandCost CmdBuildField(DoCommandFlags flags, TileIndex tile, TileIndex start_tile, RoadType rt)
{
	if (!Company::IsValidID(_current_company)) return CMD_ERROR;
	if (tile >= Map::Size() || start_tile >= Map::Size()) return CMD_ERROR;
	if (!ValParamRoadType(rt) || !RoadTypeIsRoad(rt)) return CMD_ERROR;

	TileArea area(start_tile, tile);
	if (area.w < FIELD_MIN_SIZE || area.h < FIELD_MIN_SIZE) return CommandCost(STR_ERROR_FIELD_TOO_SMALL);
	if (area.w > FIELD_MAX_SIZE || area.h > FIELD_MAX_SIZE) return CommandCost(STR_ERROR_FIELD_TOO_LARGE);
	if (!Field::CanAllocateItem()) return CommandCost(STR_ERROR_TOO_MANY_FIELDS);

	CommandCost cost(ExpensesType::Construction);

	for (TileIndex cur_tile : area) {
		if (cur_tile == start_tile) continue;

		if (IsSteepSlope(GetTileSlope(cur_tile))) return CommandCost(STR_ERROR_LAND_SLOPED_IN_WRONG_DIRECTION);
		if (IsBridgeAbove(cur_tile)) return CommandCost(STR_ERROR_MUST_DEMOLISH_BRIDGE_FIRST);

		CommandCost ret = Command<Commands::LandscapeClear>::Do(flags, cur_tile);
		if (ret.Failed()) return ret;
		cost.AddCost(ret.GetCost());
		cost.AddCost(_price[Price::ClearRough] * 2);
	}

	DiagDirection entrance = ChooseFieldEntrance(area, start_tile);
	CommandCost ret = Command<Commands::BuildRoadStop>::Do(flags, start_tile, 1, 1, RoadStopType::Truck, false, entrance, rt, ROADSTOP_CLASS_DFLT, 0, NEW_STATION, true);
	if (ret.Failed()) return ret;
	cost.AddCost(ret.GetCost());

	if (flags.Test(DoCommandFlag::Execute)) {
		Field *f = Field::Create();
		f->owner = _current_company;
		f->location = area;
		f->corner = start_tile;
		f->station = GetStationIndex(start_tile);
		f->build_date = TimerGameCalendar::date;
		f->tasks = {{FieldTaskType::Cultivate}, {FieldTaskType::Sow}, {FieldTaskType::Harvest}};

		for (TileIndex cur_tile : area) {
			if (cur_tile == start_tile) continue;
			MakeFieldTile(cur_tile, f->owner, f->index);
			MarkTileDirtyByTile(cur_tile);
		}
	}

	return cost;
}

/**
 * Change the work plan of a field.
 * @param flags Operation to perform.
 * @param field_id The field.
 * @param action What to change.
 * @param pos Position in the plan the action refers to.
 * @param value Task type for #FieldTaskAction::Insert, month for #FieldTaskAction::SetMonth.
 * @return The cost of this operation or an error.
 */
CommandCost CmdModifyFieldTasks(DoCommandFlags flags, FieldID field_id, FieldTaskAction action, uint8_t pos, uint8_t value)
{
	Field *f = Field::GetIfValid(field_id);
	if (f == nullptr) return CMD_ERROR;
	CommandCost ret = CheckOwnership(f->owner);
	if (ret.Failed()) return ret;

	switch (action) {
		case FieldTaskAction::Insert:
			if (pos > f->tasks.size() || value >= to_underlying(FieldTaskType::End)) return CMD_ERROR;
			if (f->tasks.size() >= FIELD_MAX_TASKS) return CommandCost(STR_ERROR_FIELD_TOO_MANY_TASKS);
			if (flags.Test(DoCommandFlag::Execute)) {
				f->tasks.insert(f->tasks.begin() + pos, Field::Task{static_cast<FieldTaskType>(value)});
				if (pos <= f->cur_task && f->tasks.size() > 1) f->cur_task++;
			}
			break;

		case FieldTaskAction::Delete:
			if (pos >= f->tasks.size()) return CMD_ERROR;
			if (flags.Test(DoCommandFlag::Execute)) {
				f->tasks.erase(f->tasks.begin() + pos);
				if (pos < f->cur_task) {
					f->cur_task--;
				} else if (pos == f->cur_task) {
					f->cur_task_started = false;
				}
			}
			break;

		case FieldTaskAction::SetMonth:
			if (pos >= f->tasks.size() || value > 12) return CMD_ERROR;
			if (flags.Test(DoCommandFlag::Execute)) f->tasks[pos].start_month = value;
			break;

		case FieldTaskAction::SkipTo:
			if (pos >= f->tasks.size()) return CMD_ERROR;
			if (flags.Test(DoCommandFlag::Execute)) {
				f->cur_task = pos;
				f->cur_task_started = false;
			}
			break;

		case FieldTaskAction::PerformNow:
			return f->PerformCurrentTask(flags);

		default:
			return CMD_ERROR;
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		/* A plan edit must not skip over tasks right away, only clamp the index. */
		if (f->cur_task >= f->tasks.size()) {
			f->cur_task = 0;
			f->cur_task_started = false;
		}
		SetWindowDirty(WindowClass::FieldView, f->index);
	}
	return CommandCost();
}

/**
 * Remove a field from the map; its growing tiles revert to ordinary farmland.
 * The truck stop at the entry corner is not touched.
 * @param f The field to remove.
 */
static void ReallyRemoveField(Field *f)
{
	CloseWindowById(WindowClass::FieldView, f->index);
	for (TileIndex cur_tile : f->location) {
		if (!IsTileType(cur_tile, TileType::Field)) continue;
		MakeField(cur_tile, 0, IndustryID::Invalid());
		MarkTileDirtyByTile(cur_tile);
	}
	delete f;
}

/**
 * Clear a whole field, including the truck stop at its entry corner.
 * @param f The field.
 * @param tile The tile the clearing was initiated on.
 * @param flags Operation to perform.
 * @return The cost of this operation or an error.
 */
CommandCost ClearField(Field *f, TileIndex tile, DoCommandFlags flags)
{
	if (flags.Test(DoCommandFlag::Auto)) return CommandCost(STR_ERROR_FIELD_IN_THE_WAY);
	if (_current_company == OWNER_TOWN) return CMD_ERROR;
	if (_current_company != OWNER_WATER) {
		CommandCost ret = CheckOwnership(f->owner);
		if (ret.Failed()) return ret;
	}

	CommandCost cost(ExpensesType::Construction);
	for (TileIndex cur_tile : f->location) {
		if (cur_tile == f->corner) continue;
		CommandCost ret = EnsureNoVehicleOnGround(cur_tile);
		if (ret.Failed()) return ret;
		cost.AddCost(_price[Price::ClearRough]);
	}

	CommandCost ret = RemoveFieldRoadStop(f->corner, flags);
	if (ret.Failed()) return ret;
	cost.AddCost(ret.GetCost());

	/* Make the other tiles of the field count as already cleared. */
	_cleared_object_areas.emplace_back(tile, f->location);

	if (flags.Test(DoCommandFlag::Execute)) ReallyRemoveField(f);

	return cost;
}

/**
 * Get the ground sprite of one quarter of a field tile.
 * @param stage Growth stage of the quarter.
 * @param tileh Slope of the tile; must not be steep.
 * @param quarter Quarter index, 0..3, (y half << 1) | x half.
 * @return The sprite; the four quarters of a tile are drawn at the same position.
 */
static SpriteID GetFieldQuarterSprite(FieldStage stage, Slope tileh, uint quarter)
{
	static_assert(to_underlying(FieldStage::End) * FIELD_QUARTER_SLOPE_COUNT * 4 == FIELD_QUARTERS_SPRITE_COUNT);
	uint slope = SlopeToSpriteOffset(tileh);
	assert(slope < FIELD_QUARTER_SLOPE_COUNT);
	return SPR_FIELD_QUARTERS_BASE + (to_underlying(stage) * FIELD_QUARTER_SLOPE_COUNT + slope) * 4 + quarter;
}

/** @copydoc DrawTileProc */
static void DrawTile_Field(TileInfo *ti)
{
	const Field *f = Field::GetByTile(ti->tile);

	for (uint q = 0; q < 4; q++) {
		DrawGroundSprite(GetFieldQuarterSprite(GetFieldQuarterStage(ti->tile, q), ti->tileh, q), PAL_NONE);
	}

	PaletteID pal = GetCompanyPalette(f->owner);
	for (DiagDirection side = DiagDirection::Begin; side < DiagDirection::End; side++) {
		TileIndex neighbour = TileAddByDiagDir(ti->tile, side);
		if (IsValidTile(neighbour) && f->location.Contains(neighbour)) continue;
		DrawRailFenceOnBorder(ti, side, pal);
	}
}

/** @copydoc GetSlopePixelZProc */
static int GetSlopePixelZ_Field(TileIndex tile, uint x, uint y, [[maybe_unused]] bool ground_vehicle)
{
	auto [tileh, z] = GetTilePixelSlope(tile);
	return z + GetPartialPixelZ(x & 0xF, y & 0xF, tileh);
}

/** @copydoc ClearTileProc */
static CommandCost ClearTile_Field(TileIndex tile, DoCommandFlags flags)
{
	return ClearField(Field::GetByTile(tile), tile, flags);
}

/** @copydoc GetTileDescProc */
static void GetTileDesc_Field(TileIndex tile, TileDesc &td)
{
	td.str = STR_LAI_FIELD_DESCRIPTION;
	td.owner[0] = GetTileOwner(tile);
	td.build_date = Field::GetByTile(tile)->build_date;
}

/** @copydoc ClickTileProc */
static bool ClickTile_Field(TileIndex tile)
{
	ShowFieldWindow(GetFieldIndex(tile));
	return true;
}

/** @copydoc ChangeTileOwnerProc */
static void ChangeTileOwner_Field(TileIndex tile, Owner old_owner, Owner new_owner)
{
	if (!IsTileOwner(tile, old_owner)) return;

	Field *f = Field::GetByTile(tile);
	if (new_owner != INVALID_OWNER) {
		SetTileOwner(tile, new_owner);
		f->owner = new_owner;
	} else {
		/* The truck stop is handled by the station code. */
		ReallyRemoveField(f);
	}
}

/** @copydoc TerraformTileProc */
static CommandCost TerraformTile_Field([[maybe_unused]] TileIndex tile, [[maybe_unused]] DoCommandFlags flags, [[maybe_unused]] int z_new, Slope tileh_new)
{
	/* Fields follow the land, as long as it does not get too steep. */
	if (IsSteepSlope(tileh_new)) return CommandCost(STR_ERROR_FIELD_IN_THE_WAY);
	return CommandCost();
}

/** @copydoc CheckBuildAboveProc */
static CommandCost CheckBuildAbove_Field([[maybe_unused]] TileIndex tile, [[maybe_unused]] DoCommandFlags flags, [[maybe_unused]] Axis axis, [[maybe_unused]] int height)
{
	return CommandCost(STR_ERROR_FIELD_IN_THE_WAY);
}

/** TileTypeProcs definitions for TileType::Field tiles. */
extern const TileTypeProcs _tile_type_field_procs = {
	.draw_tile_proc = DrawTile_Field,
	.get_slope_pixel_z_proc = GetSlopePixelZ_Field,
	.clear_tile_proc = ClearTile_Field,
	.get_tile_desc_proc = GetTileDesc_Field,
	.click_tile_proc = ClickTile_Field,
	.tile_loop_proc = [](TileIndex) {},
	.change_tile_owner_proc = ChangeTileOwner_Field,
	.terraform_tile_proc = TerraformTile_Field,
	.check_build_above_proc = CheckBuildAbove_Field,
};
