/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file field_func.h Functions related to player-built farm fields. */

#ifndef FIELD_FUNC_H
#define FIELD_FUNC_H

#include "command_type.h"
#include "field_type.h"
#include "tile_type.h"

struct Window;

CommandCost ClearField(Field *f, TileIndex tile, DoCommandFlags flags);
CommandCost RemoveFieldRoadStop(TileIndex tile, DoCommandFlags flags);

Window *ShowBuildFarmToolbar();

#endif /* FIELD_FUNC_H */
