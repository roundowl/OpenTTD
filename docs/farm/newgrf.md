# Farm fork: NewGRF extensions

This fork adds player-built farm fields worked by farm machinery. NewGRFs can supply
farm vehicles, crops and graphics through the extensions below. NML does not know them
yet; write them in NFO (see `farmtest_example.nfo`) or patch NML.

## Road vehicles: property 0x40, field tasks

| Property | Size | Meaning |
|---|---|---|
| `40` | B | Bitmask of field tasks the vehicle can do: `01` cultivate, `02` sow, `04` fertilise, `08` spray, `10` harvest |

- Any road vehicle with a non-zero mask is farm machinery: it accepts "Work on" orders by clicking a field tile in Go-To mode.
- A harvester only harvests a crop whose cargo it is (refitted to). It carries what it harvests and backs out to unload when nearly full.
- A machine carrying cargo with label `FERT` uses one unit per quarter when fertilising; only the shortfall is paid in money.
- The built-in Farmhand Tractor and Reaper Combine Harvester are original road vehicles 88 and 89. A NewGRF that takes over one of these slots gets a mask of 0 unless it sets property 40 itself.

## Feature 0x16: crops

A crop is identified by the cargo it produces. Without NewGRF crops, fields grow the climate's
grain, wheat or maize with the defaults below. The field window lets the player pick which crop
to sow from all crops available in the climate.

| Property | Size | Default | Meaning |
|---|---|---|---|
| `08` | D | — | Cargo label produced. Must be set first; defines the crop. |
| `09` | B | `0F` | Climates: bit 0 temperate, 1 sub-arctic, 2 sub-tropical, 3 toyland |
| `0A` | B | `01` | Months per growth stage from sown to ripe (1..15). There are four stages; ripe and overripe always last one month each. |
| `0B` | B | `03` | Cargo units per quarter at 100%. The baseline is 80%; fertilising and spraying add 20% each. |

A crop is only offered if its cargo exists in the game and the climate matches. When several
NewGRFs define a crop for the same cargo, the last one wins.

### Crop graphics

Action 1 for feature 0x16 with one set of 540 sprites, a real Action 2 group (one loaded set),
and Action 3 mapping the crop id to it. The 540 sprites follow the layout of the built-in set:

- stage-major, 9 stages: fallow, cultivated, sown, sprouted, growing, maturing, ripe, overripe, withered;
- then 15 slopes, in `SlopeToSpriteOffset` order (flat and the 14 non-steep slopes);
- then 4 quarters, `(y half << 1) | x half`, each drawn at the tile position.

`media/baseset/openttd/gen_field_quarters.py` cuts full-tile ground sprites into this layout.
A crop without graphics uses the built-in quarter sprites.

## Action 5 types

| Type | Sprites | Content |
|---|---|---|
| `40` | 540 | Built-in field quarters, the layout above (for crops without their own graphics) |
| `41` | 16 | Built-in farm machinery: tractor, then harvester, 8 directions each (N, NE, E, SE, S, SW, W, NW) |

Types `1C`..`3F` are left free for upstream OpenTTD.
