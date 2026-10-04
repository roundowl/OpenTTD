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
	Ripe, ///< Ready for harvest.
	Withered, ///< Left ripe for too long; yields nothing.
	End, ///< End marker.
};

#endif /* FIELD_TYPE_H */
