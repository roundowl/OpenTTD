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
struct Vehicle;

bool IsFieldMachine(const Vehicle *v);

/** What a farm vehicle does at the entry corner of a field. */
enum class FieldCornerAction : uint8_t {
	StartWork, ///< Drive a work route.
	Park, ///< Wait on the service quarter; only field orders and no work anywhere.
	MoveOn, ///< Advance to the next order and leave.
	Unload, ///< Unload the harvest at this field's own station; harvesters never leave a field loaded.
	Leave, ///< The current order is elsewhere; just leave.
};

struct RoadVehicle;
struct RoadVehFieldWork;
struct FieldWaypoint;
std::vector<FieldWaypoint> BuildFieldRoute(const Field &f, const RoadVehFieldWork &work);
/** Whether a farm vehicle can start working on a field. */
enum class FieldWorkAvailability : uint8_t {
	Start, ///< There is a free segment for it.
	Busy, ///< There is work, but other machines hold all of it.
	None, ///< Nothing it can do here.
};

FieldWorkAvailability CanStartFieldWork(const RoadVehicle *v, Field *f, FieldTaskType *task, int *segment);
std::pair<int, int> GetFieldSegmentEnd(const Field &f, int segment);
bool IsFieldServiceQuarter(const Field &f, int x, int y);
int GetFieldSegmentCount(const Field &f);
uint CountFieldSegmentEligibleQuarters(const Field &f, int segment, FieldTaskType type);
int GetFieldSegmentResume(const Field &f, int segment, FieldTaskType type);
bool IsFieldRowOccupied(const Field &f, int segment, const RoadVehicle *self);
bool IsFieldCornerClosedTo(const RoadVehicle *v, TileIndex tile);
FieldCornerAction EvaluateFieldCorner(const RoadVehicle *v, Field *f);
bool IsFieldMachineIdle(const RoadVehicle *v);
bool CanFieldMachineDo(const Vehicle *v, FieldTaskType type);

CommandCost ClearField(Field *f, TileIndex tile, DoCommandFlags flags);
CommandCost RemoveFieldRoadStop(TileIndex tile, DoCommandFlags flags);

Window *ShowBuildFarmToolbar();
void ShowFieldWindow(FieldID field);

#endif /* FIELD_FUNC_H */
