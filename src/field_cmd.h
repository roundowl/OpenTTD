/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file field_cmd.h Command definitions related to player-built farm fields. */

#ifndef FIELD_CMD_H
#define FIELD_CMD_H

#include "command_type.h"
#include "road_type.h"
#include "field_type.h"

CommandCost CmdBuildField(DoCommandFlags flags, TileIndex tile, TileIndex start_tile, RoadType rt);
CommandCost CmdModifyFieldTasks(DoCommandFlags flags, FieldID field_id, FieldTaskAction action, uint8_t pos, uint8_t value);

DEF_CMD_TRAIT(Commands::BuildField, CmdBuildField, CommandFlags({CommandFlag::NoWater, CommandFlag::Auto}), CommandType::LandscapeConstruction)
DEF_CMD_TRAIT(Commands::ModifyFieldTasks, CmdModifyFieldTasks, {}, CommandType::OtherManagement)

#endif /* FIELD_CMD_H */
