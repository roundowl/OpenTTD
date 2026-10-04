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
#include "company_type.h"
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

	Field(FieldID index) : FieldPool::PoolItem<&_field_pool>(index) {}
	~Field() {}

	static Field *GetByTile(TileIndex tile);
	static Field *GetByCornerTile(TileIndex tile);
};

#endif /* FIELD_BASE_H */
