/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file newgrf_crop.h NewGRF crops for player-built farm fields (farm fork). */

#ifndef NEWGRF_CROP_H
#define NEWGRF_CROP_H

#include "cargo_type.h"
#include "gfx_type.h"
#include "landscape_type.h"

struct GRFFile;
struct SpriteGroup;

/**
 * A crop that can be sown on fields, defined by a NewGRF (feature 0x16).
 * A crop is identified by the cargo it produces.
 */
struct CropSpec {
	CargoLabel label{}; ///< Cargo produced by the crop.
	LandscapeTypes climates{LandscapeType::Temperate, LandscapeType::Arctic, LandscapeType::Tropic, LandscapeType::Toyland}; ///< Climates the crop grows in.
	uint8_t months_per_stage = 1; ///< Months between the growth stages from sown to ripe.
	uint8_t yield = 3; ///< Cargo units per quarter at 100%.
	const GRFFile *grffile = nullptr; ///< NewGRF that defined the crop.
	const SpriteGroup *group = nullptr; ///< Quarter graphics; same layout as the built-in "Farm field quarters" set.

	CargoType GetCargo() const;
	bool IsAvailable() const;
};

extern std::vector<CropSpec> _crop_specs;

void ResetCrops();
const CropSpec *GetCropSpec(CargoType cargo);
SpriteID GetCropQuarterSpriteBase(const CropSpec *spec);
std::vector<CargoType> GetAvailableCrops();
uint GetCropYield(CargoType cargo);
uint GetCropMonthsPerStage(CargoType cargo);

#endif /* NEWGRF_CROP_H */
