/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file field_type.h Types related to player-built farm fields. */

#ifndef FIELD_TYPE_H
#define FIELD_TYPE_H

#include "core/pool_type.hpp"

using FieldID = PoolID<uint16_t, struct FieldIDTag, 64000, 0xFFFF>;

struct Field;

static const uint FIELD_MIN_SIZE = 2;  ///< Minimum length of a field side in tiles (3 growing tiles plus the entry corner).
static const uint FIELD_MAX_SIZE = 16; ///< Maximum length of a field side in tiles.

/**
 * Growth stage of one quarter (half tile by half tile) of a field tile.
 * Stored in 4 bits per quarter.
 */
enum class FieldStage : uint8_t {
	Fallow, ///< Untouched or harvested ground (stubble).
	Cultivated, ///< Ploughed / cultivated, ready for sowing.
	Sown, ///< Seed in the ground.
	Sprouted, ///< First growth stage.
	Growing, ///< Second growth stage.
	Maturing, ///< Third growth stage.
	Ripe, ///< Ready for harvest, first month.
	Overripe, ///< Ready for harvest, second and last month.
	Withered, ///< Left ripe for too long; yields nothing.
	End, ///< End marker.
};

/** Work that can be planned on a field. */
enum class FieldTaskType : uint8_t {
	Cultivate, ///< Fallow or withered ground becomes cultivated.
	Sow, ///< Cultivated ground becomes sown.
	Fertilise, ///< Bonus yield for cultivated or growing ground.
	Spray, ///< Bonus yield for cultivated or growing ground.
	Harvest, ///< Ripe crops become cargo; withered ones are cleared. Ground becomes fallow.
	End, ///< End marker.
};

/** Ways to modify the task plan of a field. */
enum class FieldTaskAction : uint8_t {
	Insert, ///< Insert a task of type \a value before position \a pos.
	Delete, ///< Delete the task at \a pos.
	SetMonth, ///< Set the earliest start month of the task at \a pos to \a value (0 = any, 1..12).
	SkipTo, ///< Make the task at \a pos the current task.
	PerformNow, ///< Test helper: do the current task instantly on every eligible quarter.
	End, ///< End marker.
};

/** What a vehicle inside a field is doing; selects the route it follows. */
enum class FieldRouteKind : uint8_t {
	Work, ///< Doing a task, from the entry corner round the field back to it.
	Backtrack, ///< Driving the work route backwards to the entry corner (full, or orders changed).
	ToPark, ///< Driving from the entry corner to the service quarter.
	Parked, ///< Waiting on the service quarter for work.
	FromPark, ///< Driving from the service quarter to the entry corner.
	ToCorner, ///< Driving from the end of a segment to the entry corner.
	ToService, ///< Driving from the entry corner to the service quarter to load or unload there.
	Servicing, ///< Loading or unloading on the service quarter, for an order to the field's own station.
};

/** A point a field vehicle drives to, in world pixel coordinates. */
struct FieldWaypoint {
	int32_t x = 0; ///< World X coordinate.
	int32_t y = 0; ///< World Y coordinate.
	bool work = false; ///< Whether the quarter at this point is worked when the vehicle leaves it.
};

static const uint FIELD_MAX_TASKS = 32; ///< Maximum length of a field's task plan.
static const uint FIELD_QUARTER_YIELD = 3; ///< Cargo units one quarter yields at 100%.
static const uint FIELD_QUARTER_MAX_YIELD = (FIELD_QUARTER_YIELD * 140 + 99) / 100; ///< Most cargo units one quarter can yield (all bonuses).

#endif /* FIELD_TYPE_H */
