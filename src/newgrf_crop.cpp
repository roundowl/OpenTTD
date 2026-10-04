/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file newgrf_crop.cpp NewGRF crops for player-built farm fields (farm fork). */

#include "stdafx.h"
#include "cargotype.h"
#include "field_base.h"
#include "newgrf_crop.h"
#include "newgrf_spritegroup.h"
#include "settings_type.h"
#include "table/sprites.h"

#include "safeguards.h"

std::vector<CropSpec> _crop_specs; ///< All crops defined by NewGRFs.

/** Forget all NewGRF crops. */
void ResetCrops()
{
	_crop_specs.clear();
}

/**
 * Get the cargo this crop produces.
 * @return The cargo type, or #INVALID_CARGO if it is not in the game.
 */
CargoType CropSpec::GetCargo() const
{
	return GetCargoTypeByLabel(this->label);
}

/**
 * Can the crop be sown in this game?
 * @return True if its cargo exists and it grows in the climate.
 */
bool CropSpec::IsAvailable() const
{
	return IsValidCargoType(this->GetCargo()) && this->climates.Test(_settings_game.game_creation.landscape);
}

/**
 * Get the crop producing a cargo.
 * @param cargo The cargo.
 * @return The last NewGRF crop for the cargo, or \c nullptr for built-in behaviour.
 */
const CropSpec *GetCropSpec(CargoType cargo)
{
	if (!IsValidCargoType(cargo)) return nullptr;
	for (auto it = _crop_specs.rbegin(); it != _crop_specs.rend(); ++it) {
		if (it->GetCargo() == cargo) return &*it;
	}
	return nullptr;
}

/**
 * Crops that can be sown in this game: the climate's default grain first, then NewGRF crops.
 * @return Cargo types of the crops.
 */
std::vector<CargoType> GetAvailableCrops()
{
	std::vector<CargoType> crops;
	CargoType def = GetDefaultFieldCrop();
	if (IsValidCargoType(def)) crops.push_back(def);
	for (const CropSpec &spec : _crop_specs) {
		CargoType cargo = spec.GetCargo();
		if (!spec.IsAvailable() || std::ranges::find(crops, cargo) != crops.end()) continue;
		crops.push_back(cargo);
	}
	return crops;
}

/**
 * Cargo units one quarter of a crop yields at 100%.
 * @param cargo The crop's cargo.
 * @return The yield.
 */
uint GetCropYield(CargoType cargo)
{
	const CropSpec *spec = GetCropSpec(cargo);
	return spec != nullptr ? spec->yield : FIELD_QUARTER_YIELD;
}

/**
 * Months between the growth stages of a crop.
 * @param cargo The crop's cargo.
 * @return At least 1.
 */
uint GetCropMonthsPerStage(CargoType cargo)
{
	const CropSpec *spec = GetCropSpec(cargo);
	return spec != nullptr ? std::max<uint>(1, spec->months_per_stage) : 1;
}

/** Resolver for crop graphics. */
struct CropResolverObject : public ResolverObject {
	const CropSpec *spec;

	CropResolverObject(const CropSpec *spec) : ResolverObject(spec->grffile), spec(spec)
	{
		this->root_spritegroup = spec->group;
	}

	GrfSpecFeature GetFeature() const override { return GrfSpecFeature::Crops; }
	uint32_t GetDebugID() const override { return FlattenNewGRFLabel(this->spec->label); }
};

/**
 * Get the first quarter sprite of a crop's own graphics.
 * @param spec The crop, may be \c nullptr.
 * @return Base sprite in the layout of #SPR_FIELD_QUARTERS_BASE, or 0 to use the built-in graphics.
 */
SpriteID GetCropQuarterSpriteBase(const CropSpec *spec)
{
	if (spec == nullptr || spec->group == nullptr) return 0;
	CropResolverObject object(spec);
	const auto *group = object.Resolve<ResultSpriteGroup>();
	if (group == nullptr || group->num_sprites < FIELD_QUARTERS_SPRITE_COUNT) return 0;
	return group->sprite;
}
