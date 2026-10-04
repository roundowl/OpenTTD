/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file field_sl.cpp Code handling saving and loading of player-built farm fields. */

#include "../stdafx.h"

#include "saveload.h"

#include "../field_base.h"

#include "../safeguards.h"

/** Save/load handler for the work plan of a field. */
class SlFieldTasks : public VectorSaveLoadHandler<SlFieldTasks, Field, Field::Task> {
public:
	static inline const SaveLoad description[] = {
		SaveLoad::Variable<VarFileType::U8>("type", SLE_OBJECT_ADDRESS(Field::Task, type)),
		SaveLoad::Variable<VarFileType::U8>("start_month", SLE_OBJECT_ADDRESS(Field::Task, start_month)),
	};
	static inline const SaveLoadCompatTable compat_description = {};

	std::vector<Field::Task> &GetVector(Field *f) const override { return f->tasks; }
};

static const SaveLoad _field_desc[] = {
	SaveLoad::Variable<VarFileType::U8>("owner", SLE_OBJECT_ADDRESS(Field, owner)),
	SaveLoad::Variable<VarFileType::U32>("location.tile", SLE_OBJECT_ADDRESS(Field, location.tile)),
	SaveLoad::Variable<VarFileType::U16>("location.w", SLE_OBJECT_ADDRESS(Field, location.w)),
	SaveLoad::Variable<VarFileType::U16>("location.h", SLE_OBJECT_ADDRESS(Field, location.h)),
	SaveLoad::Variable<VarFileType::U32>("corner", SLE_OBJECT_ADDRESS(Field, corner)),
	SaveLoad::Variable<VarFileType::U16>("station", SLE_OBJECT_ADDRESS(Field, station)),
	SaveLoad::Variable<VarFileType::I32>("build_date", SLE_OBJECT_ADDRESS(Field, build_date)),
	SaveLoad::Variable<VarFileType::U8>("cur_task", SLE_OBJECT_ADDRESS(Field, cur_task)),
	SaveLoad::Variable<VarFileType::Bool>("cur_task_started", SLE_OBJECT_ADDRESS(Field, cur_task_started)),
	SaveLoad::Variable<VarFileType::U8>("crop", SLE_OBJECT_ADDRESS(Field, crop)),
	SaveLoad::Variable<VarFileType::U8>("planned_crop", SLE_OBJECT_ADDRESS(Field, planned_crop)),
	SaveLoad::Variable<VarFileType::U8>("growth_counter", SLE_OBJECT_ADDRESS(Field, growth_counter)),
	SaveLoad::Variable<VarFileType::U16>("harvest_remainder", SLE_OBJECT_ADDRESS(Field, harvest_remainder)),
	SaveLoad::Variable<VarFileType::U32>("last_harvest", SLE_OBJECT_ADDRESS(Field, last_harvest)),
	SaveLoad::StructList<SlFieldTasks>("tasks"),
};

struct FILDChunkHandler : ChunkHandler {
	FILDChunkHandler() : ChunkHandler("FILD", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_field_desc);

		for (Field *f : Field::Iterate()) {
			SlSetArrayIndex(f->index);
			SlObject(f, _field_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlTableHeader(_field_desc);

		int index;
		while ((index = SlIterateArray()) != -1) {
			Field *f = Field::CreateAtIndex(FieldID(index));
			SlObject(f, slt);
		}
	}
};

static const FILDChunkHandler FILD;
static const ChunkHandlerRef field_chunk_handlers[] = {
	FILD,
};

extern const ChunkHandlerTable _field_chunk_handlers(field_chunk_handlers);
