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

static const SaveLoad _field_desc[] = {
	SaveLoad::Variable<VarFileType::U8>("owner", SLE_OBJECT_ADDRESS(Field, owner)),
	SaveLoad::Variable<VarFileType::U32>("location.tile", SLE_OBJECT_ADDRESS(Field, location.tile)),
	SaveLoad::Variable<VarFileType::U16>("location.w", SLE_OBJECT_ADDRESS(Field, location.w)),
	SaveLoad::Variable<VarFileType::U16>("location.h", SLE_OBJECT_ADDRESS(Field, location.h)),
	SaveLoad::Variable<VarFileType::U32>("corner", SLE_OBJECT_ADDRESS(Field, corner)),
	SaveLoad::Variable<VarFileType::U16>("station", SLE_OBJECT_ADDRESS(Field, station)),
	SaveLoad::Variable<VarFileType::I32>("build_date", SLE_OBJECT_ADDRESS(Field, build_date)),
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
