/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file field_map.h Map accessors for player-built farm field tiles.
 *
 * Layout of a #TileType::Field tile:
 *  - m1 bits 0..4: owner
 *  - m2: #FieldID
 *  - m8: four 4-bit #FieldStage values, one per quarter; quarter index is (y half << 1) | x half
 *  - m3, m4, m5, m6 bits 2..7, m7: unused, zero
 */

#ifndef FIELD_MAP_H
#define FIELD_MAP_H

#include "tile_map.h"
#include "field_type.h"

/**
 * Get the field index of a field tile.
 * @param t The tile.
 * @pre IsTileType(t, TileType::Field)
 * @return The field the tile belongs to.
 */
inline FieldID GetFieldIndex(Tile t)
{
	assert(IsTileType(t, TileType::Field));
	return FieldID(t.m2());
}

/**
 * Get the growth stage of one quarter of a field tile.
 * @param t The tile.
 * @param quarter Quarter index, 0..3, (y half << 1) | x half.
 * @pre IsTileType(t, TileType::Field)
 * @return The stage.
 */
inline FieldStage GetFieldQuarterStage(Tile t, uint quarter)
{
	assert(IsTileType(t, TileType::Field));
	assert(quarter < 4);
	return static_cast<FieldStage>(GB(t.m8(), quarter * 4, 4));
}

/**
 * Set the growth stage of one quarter of a field tile.
 * @param t The tile.
 * @param quarter Quarter index, 0..3, (y half << 1) | x half.
 * @param stage The new stage.
 * @pre IsTileType(t, TileType::Field)
 */
inline void SetFieldQuarterStage(Tile t, uint quarter, FieldStage stage)
{
	assert(IsTileType(t, TileType::Field));
	assert(quarter < 4);
	SB(t.m8(), quarter * 4, 4, to_underlying(stage));
}

/**
 * Make a field tile.
 * @param t The tile.
 * @param o The owner.
 * @param index The field the tile belongs to.
 */
inline void MakeFieldTile(Tile t, Owner o, FieldID index)
{
	SetTileType(t, TileType::Field);
	SetTileOwner(t, o);
	t.m2() = index.base();
	t.m3() = 0;
	t.m4() = 0;
	t.m5() = 0;
	SB(t.m6(), 2, 6, 0);
	t.m7() = 0;
	t.m8() = 0;
}

#endif /* FIELD_MAP_H */
