# Server game data

This directory holds the server's own rows. At startup the server loads the game database
(built by [Aion2_Client_Decoding](https://github.com/SemaAlter/Aion2_Client_Decoding)) and then applies these CSVs over it, replacing matching IDs
(`apply_game_data_overrides`). Client data stays the default; these files add what the client
does not ship, such as monster combat stats and the tutorial encounter spawns, as observed on
the official server.

- `skills.csv`: stable skill metadata, including authoritative range, hit shape, maximum targets, and sector angle; packet sequencing remains in `tutorial-skills.csv` until the protocol is generalized. The two Templar tutorial actions reach 4 m, affect at most four enemies, and are modeled as 120-degree frontal sectors from captured behavior.
- `items.csv`: item templates.
- `npcs.csv`: NPC identity and interaction role. Monsters must use `kind=monster`.
- `monsters.csv`: combat attributes linked to an NPC through `npc_id`.
- `spawns.csv`: NPC/monster placements and respawn timing.
- `walk-meshes/<map_id>.obj`: server navigation surfaces. OBJ faces are triangulated during loading.

Empty catalogs are valid. IDs must be non-zero and unique within each layer.
Invalid references stop server startup instead of leaving corrupt world state
running.

The tutorial rows use stable local entity IDs and positions observed in the continuous Elyos tutorial capture. All four are created when the client reports that the encounter cutscene has finished. Their headings face the fixed post-cutscene player position (`-94161.31, 54676.16`). The encounter uses the three observed templates `2701091`, `2701692`, and `2702072`; all currently have the captured 100 HP value. `move_speed` and `attack_range` drive authoritative pursuit before an attack; `server_skill_id` stores the captured monster-side combat skill and `attack_interval_ms` controls the local AI cadence. Fields not yet decoded remain conservative server values.

The other tutorial spawns are the official run's later groups, with their
official entity IDs and positions: group 2 (55173, 59535, 37205, 41778, 45702)
appears at the second-encounter checkpoint and idles until the player comes
within 8 m or attacks; group 3 (41456, 46192, 41228, 58382, 34583, 41927)
appears after the castle door and only stands while it speaks the final
dialogue. Group membership and quest-flag order are in
`src/server/tutorial_world.cpp`.

Run the server with `--data-dir config/game-data`. Client-extracted data should be transformed into these schemas; the server must not depend directly on unstable Unreal serialization.
