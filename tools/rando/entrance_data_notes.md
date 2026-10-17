# entrance_data.yml — notes

## Sources
- `tools/rando/loading_zones.json` — every 0x190 `AreaLevelOpen` zone. 114 of them sit in `Field_*` rooms and lead somewhere other than `Field`. All 114 are in `doors:`, each exactly once. The generator asserts this.
- `tools/rando/location_rooms.json` and LAS `RandomizerCore/Data/logic.yml` (v0.41) — regions, item conditions, room/dungeon nodes, followers, and the condition vocabulary. Every condition in the YAML was checked token by token against that vocabulary.
- Actor scan of `extracted/romfs/region_common/level/*/*.leb`, done with LAS's `leb` reader (read-only). Used for:
  - finding which interior holds each NPC or item actor;
  - finding non-0x190 transitions (holes, warps);
  - telling DogHouse and MadamHouse apart.
- LADXR (daid/LADXR, master): `entranceInfo.py` (names, GB rooms, types, door index and x/y), `logic/overworld.py` (outside/inside locations, entrance and exit requirements, connector internals), `logic/requirements.py` (bush, pit_bush and enemy definitions), `worldSetup.py` (entrance pools per type).
- LAS mod generator and Randomizers (`mod_generator.py`, `npcs.py`, `rapids.py`): which event flow each NPC location patches, for example RaftShopMan, RibbonBowWow, QuadrupletsMother and MarthasBayFisherman.

## GB room ↔ Switch Field mapping (verified)
The mapping is `Field_RRC` ⇔ GB room `(RR-1)*16 + (C-'A')`. Spot checks against LADXR `entranceInfo`:

| Switch | GB room | What it is |
|---|---|---|
| Field_14D | 0xD3 | `d1`, Tail Cave |
| Field_11C | 0xA2 | `start_house`, Marin & Tarin |
| Field_02A | 0x10 | d8 |
| Field_03E | 0x24 | d2 |
| Field_12F | 0xB5 | d3 |
| Field_03L | 0x2B | d4 |
| Field_14J | 0xD9 | d5 |
| Field_09M | 0x8C | d6 |
| Field_01O | 0x0E | d7 |
| Field_08H | 0x77 | d0 |
| Field_09K | 0x8A | Seashell Mansion |

All 104 LADXR `ENTRANCE_INFO` entries, plus the commented-out `d4_connector` and `d4_connector_exit`, land on a Switch door. Where LADXR lists several doors on one screen (`index`, `x`), the Switch zone x-offset inside the room, scaled from 15 units to 160 px, matches the LADXR x. Examples: Field_02P idx3 at x≈40 is `right_taltal_connector2`; idx2 at x≈88 is `connector4`. 1 Switch unit ≈ 10.67 GB px.

### Caveat: some zones sit in the file for the screen below
A few zone actors are stored in the room file of the screen *below* their GB screen. You can tell because the interior's return spawn is named after the real screen. `gb_screen` records the real screen.

| Zone file | Actual screen |
|---|---|
| Field_02E | 01E (0x04) |
| Field_02H idx1 | 01H (0x07) |
| Field_02K idx0, idx1 | 01K (0x0A) |
| Field_02N idx2 | 01N (0x0D) |
| Field_02P idx4 | 01P (0x0F) |
| Field_08H idx1 | 09H (0x87) |
| Field_09M idx1 | 10M (0x9C) |
| Field_16D | 16E (0xF4) |

The Field_16D/16E case is one column over, at the edge of the room.

## Conventions
The header of the YAML defines `outer_region`, `outer_extra`, `pocket`, `door_req`, `exit_req`, `inside`, connector `edges` and `traversal`.

- **Bushes and walls vs. rocks:**
  - A bush, bombable wall or keyhole on the door is `door_req`. It is needed to get in, but not to get out (LADXR `_addEntranceRequirementExit(None)`).
  - A rock or obstacle that also traps you after you exit goes in `outer_extra`. This follows LADXR's pocket locations, e.g. `outside_dream_hut` and `outside_hookshot_cave`.
- **Pockets:** a pocket is an area that vanilla only reaches through a connector or a dungeon.
  - Its `outer_extra` repeats the vanilla route, so that `outer_region & outer_extra & door_req & inside` matches the LAS condition.
  - `pocket.vanilla_access` says which connector that is. An entrance randomizer must replace these terms with real pocket logic.
- **LAS regions that already assume a vanilla connector:**
  - `taltal-east-0` (mountain-access)
  - `taltal-west-0` (taltal-crossing-cave)
  - `taltal-west-2` (fire-cave)
  - `eagle-tower` (rainbow caves + path-to-d7)
  - `kanalet-outside` (kanalet-tunnel)
  - `kanalet-inside` (kanalet-castle)
  - `pothole` (richard-villa)
  - `animal-village` (the `break-obstacle` branch = river-crossing-cave)
  - room `D6-8D` (d6-connector)
  - `angler-tunnel` (d4 connector)
  - `plains`, which absorbs the west Martha's Bay shore through bay-cliff-cave
  - item `mad-batter-bay` (bay-batter-tunnel)
- **Types:**
  - `single` covers all one-exit interiors, including LADXR's `dummy`, `trade`, `water`, `insanity` and `start` types. These are recorded as `ladxr_type`, so you can rebuild LADXR-style pools.
  - Water entrances are flagged `water: true`. The player exits them swimming, so don't mix them with land doors without a fix.
  - `special` means never shuffle.

## Counts
- **Doors (114):** single 54, connector 45, dungeon 9 (D1–D8 plus D0), special 6.
- **Connector groups (22):** 21 that can be shuffled, plus `catfish-maw-dive`, marked `special: true`. The 45 connector doors exclude the two catfish dive doors, which are typed `special`.
- **Special doors:**
  - the Wind Fish's Egg;
  - the two D8 side exits (Field_01A and Field_01C);
  - the D6 side exit (Field_07M);
  - the two CatfishsMawEnter dive spots.

## Judgment calls
1. **Splitting multi-cave levels by LADXR room IDs** (`target` 0x2xx):
   - **Tamaranch04** becomes `mountain-access` (rooms 03A/03B/02D/02E, three doors) and `taltal-crossing-cave` (01B…04A; room 02C holds the bombs heart piece = LADXR 0x2BA).
   - **Tamaranch01** becomes the outer rainbow cave (01A/01B/01C) and the inner rainbow cave (02A…04B).
   - **KanaletCastle** becomes the main castle connector (front + roof-west) plus `kanalet-castle-roof-east`, a single (rooms 02D/01D, the flail guard). This follows LADXR `castle_upper_right` = single.
   - None of these internal splits were checked against the in-game room geometry.
2. **QuadupletHouse** (two doors into one room) is a connector. It was missing from the original list.
3. **D8 and D6 side exits** (Lv08TurtleRock_03C/03F → Field_01A/01C; Lv06FaceShrine_02A → Field_07M) are `special`. LADXR has no entrance for them, and their outside areas are pockets only reached from inside the dungeon.
4. **Telephone booths and fairy fountains** are `single` with `ladxr_type: dummy`, not special. LADXR shuffles them as junk, and nothing about them breaks shuffling. In LAS the phones swap tunics.
5. **Ciao-ciao (ribbon → dog food) is in `madam-meowmeow-house`**, not the kennel. Only MadamHouse's `NpcRibbonBow` carries the `RibbonBowWow` event; the DogHouse copy has an empty event param. LADXR puts the trade in the kennel. `doghouse-dig` is in DogHouse, which has the `ItemSecretSeashell` actor.
6. **Rapids race prizes are in `raft-shop`** (RapidsRideTicket, the RaftShopMan event). The race itself runs on the overworld, and LADXR only shuffles the raft house in "insanity" mode.
7. **Locations not inside any door:**
   - `marin` (Ballad) and `ghost-reward` are given outdoors.
   - `papahl` is on the papahl-ledge pocket.
   - The `fishing-*` locations come from the fisherman NPC on Field_09B; FishingPond has no door.
   - `mabe-well` is in TownWell, which you reach through the hole in Field_11A.
8. **D4 entry needs the Angler Key.** `door_req: angler-key` follows LADXR (ANGLER_TUNNEL_OPENED). LAS's `angler-tunnel` second branch (`taltal-river & cross-water`) needs no key.
9. **Conflicting conditions — LAS wins.** Where LAS and LADXR disagree, the YAML uses the LAS condition and the note records LADXR's:
   - rainbow cave: LAS `(feather & hookshot) | rooster`, LADXR `(feather | rooster) & hookshot`;
   - bird key: LADXR also allows feather & bracelet:2;
   - bay-batter tunnel: `cross-water` vs `flippers`;
   - Goponga left chest: `feather` vs `feather | rooster | hookshot`.
10. **One-way traversals follow LADXR `back=False`:**
    - taltal-crossing-cave west→east is impossible;
    - inner rainbow cave east→west is impossible;
    - raft-return north→south is impossible;
    - five-chest game room → tunnel is free, the reverse needs bombs;
    - bay-cliff plateau ↔ cave is asymmetric;
    - richard-villa cave → house is free.
11. **Heart pieces in connectors are placed on the node that matches the LAS condition**, even when the actor's room suggests otherwise. `river-crossing-cave` is placed at the west node: the actor is in room 01B, but LAS needs no boots.
12. **`d0-color-dungeon` and `windfish-egg` list `other_return_zones`.** Each has two interior zones that return to the same screen: Lv10 04C/08C and Lv09 02A/08C.

## Low-confidence entries (`confidence: low`)
- `d4-connector-west` and `d4-connector-east`, plus connector `angler-tunnel-connector`. LADXR leaves this pair as a TODO. Direction and whether the D4-opened event is needed are not verified.
- `catfish-maw-dive-east` and `catfish-maw-dive-west`, plus connector `catfish-maw-dive`. These are underwater side-view passages that LADXR doesn't model, and their internal connectivity is assumed.
- `mermaid-statue` (`shuffle_recommended: false`). The split of hookshot (approach) vs. scale (door) is a guess, and the statue opening is event state.
- `bay-fisherman-boat` (`shuffle_recommended: false`). LADXR models it as an overworld trade, not an entrance.
- Connector `raft-return-cave`. The heart piece node and the one-way direction are unverified.

## Medium-confidence entries
- `d8-west-side-exit` and `d8-east-side-exit`
- `five-chest-tunnel-west` and `five-chest-tunnel-east`: the LAS region is `taltal-east-2`, but LADXR reaches the west door from `taltal-east-0` with cross-water.
- `goponga-cave-east`: the nearby `taltal-west-escape` heart piece is in `taltal-west-1`.
- `d4-anglers-tunnel`
- `raft-return-north`, `raft-return-south` and `raft-shop`
- `woods-crossing-cave-north`
- `kanalet-castle-roof-east`
- `d6-north-side-exit`
- `kanalet-south-cave`: the LAS condition was moved entirely into `outer_extra`.
- `armos-fairy`
- `mabe-kennel` and `madam-meowmeow-house`
- `bay-cliff-cave-south` and `bay-phone`: LADXR `left_bay_area` is folded into `plains`.

Connectors at medium confidence: `mountain-access`, `taltal-crossing-cave`, `five-chest-cave`, both rainbow caves, `kanalet-castle`, `river-crossing-cave`.

## Transitions in Field that are not 0x190 doors (not in `doors:`)
- **Holes (actor 0x1FD `AreaHoleLower`):**
  - Field_11A → TownWell_01A (mabe-well heart piece; exit zone TownWell_01A → Field_11A).
  - Field_02O → EagleTowerExit_01A_a (drop into the damp cave).
  - EagleKeyCave_01A/01B/02A/02B holes → Field_01K_d (one-way down to taltal-east-0).
- **Quicksand** (`AreaAntlion`, desert) sinks you into LanmolaCave.
- **Warp holes** (`AreaWarpHole`): Field_01B, 03M, 10F, 15M.
- **Manbo's Mambo warp points** (`ObjWarpManbo`): Field_02D, 02P, 03H, 05F, 09L, 15H.
- **Event-driven moves:**
  - FishingPond, via the NPC on Field_09B;
  - DreamShrine, via the bed in BedRoom;
  - RichardHouse → RichardCave (interior zone);
  - dungeon-internal stairs.

## LAS oddities noticed
- `rapids-south-island` requires `D6-2A`, which is not a defined LAS room node. It is probably meant to be D6-3A.
- `christine-grateful` uses a region (`mr-write-house`) as a condition term. The YAML keeps it as is.
- LAS has no item location in the Moblin cave (LADXR has a chest there); only the `moblin-cave` follower, which is BowWow.
- LAS `taltal-east-1` requires `angler-key` and `heights`, which is stricter than LADXR's middle Tal Tal.
