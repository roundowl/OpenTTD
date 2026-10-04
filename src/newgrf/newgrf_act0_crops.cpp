/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file newgrf_act0_crops.cpp NewGRF Action 0x00 handler for crops (farm fork, feature 0x16). */

#include "../stdafx.h"
#include "../debug.h"
#include "../newgrf_crop.h"
#include "newgrf_bytereader.h"
#include "newgrf_internal.h"

#include "../safeguards.h"

/**
 * Define properties for crops.
 * Properties: 08 cargo label (identity, must come first), 09 climates (byte, bits 0..3),
 * 0A months per growth stage (byte), 0B cargo units per quarter at 100% (byte).
 * @param first First crop id of this GRF.
 * @param last Last crop id (exclusive).
 * @param prop The property.
 * @param buf Property data.
 * @return Whether the property was handled.
 */
static ChangeInfoResult CropChangeInfo(uint first, uint last, int prop, ByteReader &buf)
{
	ChangeInfoResult ret = ChangeInfoResult::Success;
	auto &map = _cur_gps.grffile->crop_map;

	for (uint id = first; id < last; ++id) {
		if (prop != 0x08 && (id >= map.size() || map[id] == UINT16_MAX)) {
			GrfMsg(Severity::Error, "CropChangeInfo: Attempt to modify undefined crop {}, ignoring", id);
			return ChangeInfoResult::InvalidId;
		}

		switch (prop) {
			case 0x08: { // Cargo label
				if (id >= map.size()) map.resize(id + 1, UINT16_MAX);
				if (map[id] == UINT16_MAX) {
					map[id] = static_cast<uint16_t>(_crop_specs.size());
					_crop_specs.emplace_back();
				}
				CropSpec &spec = _crop_specs[map[id]];
				spec.label = buf.ReadLabel<CargoLabel>();
				spec.grffile = _cur_gps.grffile;
				break;
			}

			case 0x09: // Climates
				_crop_specs[map[id]].climates = LandscapeTypes{buf.ReadByte()};
				break;

			case 0x0A: // Months per growth stage
				_crop_specs[map[id]].months_per_stage = std::clamp<uint8_t>(buf.ReadByte(), 1, 15);
				break;

			case 0x0B: // Yield per quarter
				_crop_specs[map[id]].yield = buf.ReadByte();
				break;

			default:
				ret = ChangeInfoResult::Unknown;
				break;
		}
	}

	return ret;
}

template <> ChangeInfoResult GrfChangeInfoHandler<GrfSpecFeature::Crops>::Reserve(uint, uint, int, ByteReader &) { return ChangeInfoResult::Unhandled; }
template <> ChangeInfoResult GrfChangeInfoHandler<GrfSpecFeature::Crops>::Activation(uint first, uint last, int prop, ByteReader &buf) { return CropChangeInfo(first, last, prop, buf); }
