#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <optional>
#include <vector>

namespace sema {

enum class gameplay_client_message : std::uint16_t {
    hello = 0x3610,
    clock_acknowledgement = 0x3601,
    ping = 0x3602,
    unresolved_0043 = 0x4300,
    unresolved_0e57 = 0x570E,
    unresolved_1056 = 0x5610,
    unresolved_1c8a = 0x8A1C,
    client_runtime_identity = 0x3622,
    // Two-byte request seen once late after a tutorial exit; treated like 25 36 in the tutorial.
    leave_tutorial_request = 0x3617,
    // Character creation (assumed plaintext opcodes: the official replies are request + 1,
    // 1A 36 name check, 1C 36 created character, 20 36 world-entry character info).
    character_name_check = 0x3619,
    character_create = 0x361B,
    enter_world = 0x361F,
    return_to_character_selection = 0x3625,
    exit_game_request = 0x3630,
    tutorial_objective_progress = 0x3636,
    login = 0x3613,
    minimap_filter_settings = 0x8D40,
    unresolved_428d = 0x8D42,
    unresolved_4836 = 0x3648,
    initialize_tutorial = 0xE24F,
    streaming_distance_tier = 0x3651,
    unresolved_568d = 0x8D56,
    unresolved_59e2 = 0xE259,
    unresolved_8d56 = 0x568D,
    unresolved_a1ff = 0xFFA1,
    unresolved_a756 = 0x56A7,
    movement_started = 0x3700,
    movement_updated = 0x3701,
    air_movement_started = 0x3702,
    air_movement_updated = 0x3703,
    movement_stopped = 0x3704,
    air_movement_stopped = 0x3705,
    dash_movement_state = 0x370E,
    dash_path_update = 0x370F,
    movement_snapshot = 0x3718,
    cast = 0x3800,
    target_select_request = 0x3830,
    target_lock_request = 0x3833,
    skill_input_begin = 0x383A,
    skill_input_end = 0x383C,
    cutscene_finished = 0xE334,
};

enum class gameplay_server_message : std::uint16_t {
    tick = 0x3600,
    ping = 0x3603,
    hello = 0x3611,
    login_result = 0x3615,
    unresolved_1836 = 0x3618,
    player_name = 0x361A,
    unresolved_2036 = 0x3620,
    player_spawn = 0x3621,
    player_position = 0x3623,
    unresolved_2836 = 0x3628,
    unresolved_2b36 = 0x362B,
    exit_game_result = 0x3631,
    unresolved_3436 = 0x3634,
    unresolved_3536 = 0x3635,
    unresolved_3736 = 0x3637,
    unresolved_3836 = 0x3638,
    unresolved_3a36 = 0x363A,
    unresolved_3b36 = 0x363B,
    entity_spawn = 0x3641,
    entity_death = 0x3642,
    unresolved_4736 = 0x3647,
    unresolved_4936 = 0x3649,
    unresolved_4a36 = 0x364A,
    unresolved_5636 = 0x3656,
    unresolved_1a37 = 0x371A,
    unresolved_1b37 = 0x371B,
    unresolved_1c37 = 0x371C,
    entity_movement = 0x371D,
    unresolved_1e37 = 0x371E,
    unresolved_1f37 = 0x371F,
    unresolved_2037 = 0x3720,
    unresolved_2137 = 0x3721,
    unresolved_2237 = 0x3722,
    unresolved_2337 = 0x3723,
    unresolved_2537 = 0x3725,
    unresolved_2837 = 0x3728,
    unresolved_2937 = 0x3729,
    unresolved_2a37 = 0x372A,
    unresolved_2e37 = 0x372E,
    unresolved_2f37 = 0x372F,
    unresolved_3c37 = 0x373C,
    unresolved_4137 = 0x3741,
    unresolved_4237 = 0x3742,
    unresolved_4637 = 0x3746,
    skill_started = 0x3801,
    skill_cast = 0x3802,
    skill_hit = 0x3804,
    unresolved_0538 = 0x3805,
    skill_completed = 0x3806,
    unresolved_0938 = 0x3809,
    combo_next = 0x3818,
    combo_stage = 0x3819,
    player_attribute_update = 0x382A,
    unresolved_2b38 = 0x382B,
    player_action_state = 0x382C,
    target_selection_state = 0x3831,
    target_selected = 0x3834,
    entity_target = 0x3835,
    skill_sequence_begin = 0x383B,
    skill_impact_begin = 0x383D,
    unresolved_0140 = 0x4001,
    unresolved_0143 = 0x4301,
    unresolved_0f57 = 0x570F,
    unresolved_2d57 = 0x572D,
    unresolved_0795 = 0x9507,
    unresolved_0895 = 0x9508,
    unresolved_0c91 = 0x910C,
    unresolved_2d92 = 0x922D,
    entity_property_update = 0x8D00,
    unresolved_038d = 0x8D03,
    unresolved_048d = 0x8D04,
    combat_state = 0x8D21,
    cutscene_transition = 0x8D41,
    unresolved_438d = 0x8D43,
    entity_ownership = 0x8D52,
    unresolved_578d = 0x8D57,
    unresolved_6d8d = 0x8D6D,
    tutorial_prompt = 0x8D15,
    unresolved_0390 = 0x9003,
    unresolved_15e2 = 0xE215,
    world_bootstrap = 0xE250,
    unresolved_5ae2 = 0xE25A,
    unresolved_63e2 = 0xE263,
    unresolved_74e2 = 0xE274,
    cutscene_start = 0xE335,
    inbox_mail_notification = 0xE349,
    cutscene_instance_open = 0xE002,
    cutscene_instance_close = 0xE004,
    scheduled_event_state = 0x5684,
    unresolved_8e56 = 0x568E,
    unresolved_a856 = 0x56A8,
    unresolved_a9ff = 0xFFA9,
    world_snapshot_container = 0xFFFF,
};

struct gameplay_client_login {
    std::string account_session;
    std::string lobby_token;
};

struct gameplay_ping {
    std::uint64_t client_time_milliseconds;
};

struct gameplay_compressed_json {
    std::uint32_t declared_compressed_length;
    std::uint32_t uncompressed_length;
    std::vector<std::uint8_t> zlib_data;
};

struct gameplay_player_name {
    std::string name;
    std::uint8_t state;
};

struct gameplay_movement {
    float x;
    float y;
    float z;
};

struct gameplay_timed_message {
    std::uint32_t delay_milliseconds;
    std::vector<std::uint8_t> body;
};

enum class gameplay_cast_phase : std::uint8_t {
    initial = 0x01,
    continuation = 0xA1,
    combo = 0xA5,
    channel = 0xE1,
};

enum class gameplay_skill_kind : std::uint8_t {
    dash,
    primary_attack,
    secondary_attack,
};

struct gameplay_skill_definition {
    std::uint32_t skill_id;
    std::string name;
    gameplay_skill_kind kind;
    std::uint32_t server_skill_id;
    std::uint32_t next_request_skill_id;
    std::uint32_t announced_next_skill_id;
    bool sends_combo_stage;
    bool begins_combat_state;
    std::uint32_t terminal_release_delay_milliseconds;
};

struct gameplay_client_cast {
    gameplay_cast_phase phase;
    std::uint32_t skill_id;
    std::uint32_t target_entity_id;
    std::array<std::uint8_t, 4> facing;
    std::array<std::uint8_t, 12> position_or_direction;
};

struct gameplay_attack_target {
    std::uint32_t entity_id;
    std::uint32_t remaining_health;
    std::uint16_t damage;
    bool killed;
};

[[nodiscard]] bool has_gameplay_opcode(
    std::span<const std::uint8_t> body,
    gameplay_client_message message) noexcept;
[[nodiscard]] bool has_gameplay_opcode(
    std::span<const std::uint8_t> body,
    gameplay_server_message message) noexcept;

[[nodiscard]] gameplay_client_login parse_gameplay_client_login(std::span<const std::uint8_t> body);
// 15 36 login result. Official: u8 count, the account's character entries (as in the 26 36
// roster), 00, then the account session, server ID and build strings. Without entries the
// observed empty form carries a u32 0 instead.
[[nodiscard]] std::vector<std::uint8_t> encode_empty_gameplay_login(
    const gameplay_client_login& client,
    std::uint16_t server_id,
    std::uint64_t login_time_milliseconds,
    std::span<const std::vector<std::uint8_t>> characters = {});
[[nodiscard]] gameplay_ping parse_gameplay_ping(std::span<const std::uint8_t> body);
[[nodiscard]] gameplay_compressed_json parse_gameplay_minimap_filter_settings(
    std::span<const std::uint8_t> body);
[[nodiscard]] gameplay_player_name parse_gameplay_player_name(std::span<const std::uint8_t> body);
[[nodiscard]] gameplay_movement parse_gameplay_movement(std::span<const std::uint8_t> body);
[[nodiscard]] std::vector<std::uint8_t> encode_gameplay_ping(
    gameplay_ping client,
    std::uint64_t server_time_milliseconds);
[[nodiscard]] std::vector<std::uint8_t> encode_gameplay_tick(std::uint64_t server_time_milliseconds);
[[nodiscard]] std::vector<std::uint8_t> encode_gameplay_exit_result();
[[nodiscard]] bool refresh_gameplay_server_time(
    std::vector<std::uint8_t>& body,
    std::uint64_t server_time_milliseconds);
[[nodiscard]] std::vector<gameplay_timed_message> gameplay_tutorial_dash_sequence(
    const gameplay_client_cast& request,
    const gameplay_skill_definition& skill,
    std::uint32_t player_entity_id,
    std::uint8_t action_sequence,
    std::uint8_t player_state,
    std::uint64_t server_time_milliseconds,
    gameplay_movement position);
// 22 37 then 42 37: the player's dash/sprint forced movement ends and is released.
[[nodiscard]] std::vector<std::vector<std::uint8_t>> encode_gameplay_sprint_end(std::uint32_t player_entity_id);
[[nodiscard]] gameplay_client_cast parse_gameplay_client_cast(std::span<const std::uint8_t> body);
[[nodiscard]] std::vector<gameplay_timed_message> gameplay_tutorial_attack_sequence(
    gameplay_client_cast request,
    const gameplay_skill_definition& skill,
    std::uint32_t player_entity_id,
    std::uint8_t action_sequence,
    std::span<const gameplay_attack_target> targets = {});
[[nodiscard]] std::vector<std::uint8_t> encode_gameplay_monster_spawn(
    std::uint32_t entity_id,
    std::uint32_t template_id,
    float x,
    float y,
    float z,
    float heading,
    std::uint32_t max_health);
[[nodiscard]] std::vector<std::uint8_t> encode_gameplay_monster_movement_started(
    std::uint32_t entity_id,
    gameplay_movement position,
    float heading);
[[nodiscard]] std::vector<std::uint8_t> encode_gameplay_monster_movement_stopped(
    std::uint32_t entity_id,
    gameplay_movement position,
    float heading);
[[nodiscard]] std::vector<std::uint8_t> encode_gameplay_monster_movement_updated(
    std::uint32_t entity_id,
    float direction_x,
    float direction_y,
    float heading);
// 00 8D 32-bit resource property: 01 01 <index> <u32>; index 01 is MP and 03 is SP (stamina).
[[nodiscard]] std::vector<std::uint8_t> encode_gameplay_entity_resource(
    std::uint32_t entity_id, std::uint8_t index, std::uint32_t value);
// 07 95 (state 1) / 08 95 (state 2): social action begin or end on an entity.
[[nodiscard]] std::vector<std::uint8_t> encode_gameplay_social_action(
    std::uint32_t entity_id, std::uint32_t social_action_id, bool begin);
// 46 37: the player's ground height when it takes off.
[[nodiscard]] std::vector<std::uint8_t> encode_gameplay_takeoff_height(std::uint32_t entity_id, float z);
// 00 8D health property update for any entity.
[[nodiscard]] std::vector<std::uint8_t> encode_gameplay_entity_health(
    std::uint32_t entity_id,
    std::uint64_t health);
[[nodiscard]] std::vector<gameplay_timed_message> gameplay_monster_spawn_sequence(
    std::span<const std::uint8_t> spawn_body,
    std::uint32_t entity_id,
    std::uint32_t player_entity_id);
[[nodiscard]] std::vector<gameplay_timed_message> gameplay_monster_activation_sequence(
    std::uint32_t entity_id,
    std::uint32_t player_entity_id);
[[nodiscard]] std::vector<gameplay_timed_message> gameplay_monster_attack_sequence(
    std::uint32_t entity_id,
    std::uint32_t player_entity_id,
    std::uint32_t server_skill_id,
    std::uint8_t action_sequence,
    gameplay_movement target_position,
    float facing_degrees,
    std::uint8_t damage,
    std::uint64_t remaining_player_health);
[[nodiscard]] std::vector<gameplay_timed_message> gameplay_tutorial_light_prelude(
    std::uint32_t player_entity_id,
    std::uint64_t server_time_milliseconds,
    std::uint8_t player_state);
[[nodiscard]] std::vector<std::uint8_t> encode_gameplay_tutorial_event_state(
    std::uint32_t state,
    std::uint64_t server_time_milliseconds);
[[nodiscard]] std::vector<std::uint8_t> encode_gameplay_tutorial_movement_prompt();
[[nodiscard]] std::vector<std::uint8_t> encode_gameplay_tutorial_monster_prompt();
// 84 56 scheduled map-event state (single record).
[[nodiscard]] std::vector<std::uint8_t> encode_gameplay_map_event(
    std::uint32_t event_id,
    std::uint32_t state,
    std::uint64_t server_time_milliseconds);
// 2D 92 quest step: quest instance, chapter, step, then one completion flag per objective.
[[nodiscard]] std::vector<std::uint8_t> encode_gameplay_quest_step(
    std::uint32_t quest_instance,
    std::uint8_t chapter,
    std::uint8_t step,
    std::span<const std::uint8_t> objective_flags,
    std::uint8_t status = 0x02);  // 02 in progress, 03 quest complete (official tutorial end)
// 35 36 <entity> <state> 03 00: the entity leaves the client; at the tutorial exit the room's
// objects go with state 03 when destroyed, 01 otherwise (official-tutorial-full 249.68 s).
[[nodiscard]] std::vector<std::uint8_t> encode_gameplay_entity_disappear(std::uint32_t entity_id, bool destroyed);
// 93 8D <object> 01 00 00 00: a breakable environment object (tutorial test tube) is destroyed;
// followed by 6D 8D <object>.
[[nodiscard]] std::vector<std::vector<std::uint8_t>> encode_gameplay_object_destroyed(std::uint32_t object_entity_id);
// Replaces every length-prefixed occurrence of a character name (u8 length + bytes).
[[nodiscard]] std::vector<std::uint8_t> rename_character(
    std::vector<std::uint8_t> body, const std::string& from, const std::string& to);
// Tutorial end map records: 01 61 00 <map> 01 00 (map leave flag) and 00 61 <map> 03 <now> <now + 600000>
// (official-tutorial-full 249.28 s). The client answers with its 17 36 leave request.
[[nodiscard]] std::vector<std::vector<std::uint8_t>> encode_gameplay_tutorial_map_end(
    std::uint32_t map_id, std::uint64_t server_time_milliseconds);
// Ranged monster attack (official-tutorial-full 214.5-216.2 s, skill 1239740): cast, projectile
// launch 03 38 0.9 s later, then the hit, the projectile impact 0E 38, health, and completion.
[[nodiscard]] std::vector<gameplay_timed_message> gameplay_monster_ranged_attack_sequence(
    std::uint32_t entity_id,
    std::uint32_t player_entity_id,
    std::uint32_t server_skill_id,
    std::uint8_t action_sequence,
    gameplay_movement target_position,
    float facing_degrees,
    std::uint8_t damage,
    std::uint64_t remaining_player_health);
// Interaction with an environment object: 38 36, 3A 36, 6D 8D, then 3B 36 / 37 36. A door
// (MapEvent object the player activates) uses kind 02, result 01, and is not removed.
[[nodiscard]] std::vector<gameplay_timed_message> gameplay_interaction_sequence(
    std::uint32_t object_entity_id,
    std::uint32_t player_entity_id,
    bool door);
// 2C 38 action end: the action sequence a 2A 38 started, and its type (07 dash, 0F cutscene, 01 effect).
[[nodiscard]] std::vector<std::uint8_t> encode_gameplay_action_finished(
    std::uint32_t player_entity_id, std::uint8_t action_state, std::uint8_t type);
// 2A 38 movement action start (kind 11): action sequence, two u32 parameters (dash 200/300,
// jump landing 231/200), four zero bytes, the current time, the player, 01 00, and the position.
[[nodiscard]] std::vector<std::uint8_t> encode_gameplay_movement_action(
    std::uint32_t player_entity_id, std::uint8_t action_state, std::uint32_t first, std::uint32_t second,
    std::uint64_t server_time_milliseconds, gameplay_movement position);
// Jump landing (231/200); the matching 2C 38 type 01 follows 0.2 s later
// (official-tutorial-combat-door 218.5 s).
[[nodiscard]] std::vector<std::uint8_t> encode_gameplay_jump_landed(
    std::uint32_t player_entity_id, std::uint8_t action_state, std::uint64_t server_time_milliseconds,
    gameplay_movement position);
// 04 8D kill record: victim, killing skill, killer, character token of the killer.
[[nodiscard]] std::vector<std::uint8_t> encode_gameplay_kill_record(
    std::uint32_t victim_entity_id,
    std::uint32_t skill_id,
    std::uint32_t killer_entity_id,
    std::string_view character_token);
// Life Potion use as the official server answers it (official-tutorial-combat-door 84.2 s):
// item-skill cast and completion, inventory update, item used, the self heal hit, the
// heal-over-time action start (2A 38 kind 13), and its two abnormal records.
struct gameplay_inventory_item {
    std::uint64_t instance_id{};
    std::uint32_t template_id{};
    std::uint32_t count{};
};
[[nodiscard]] std::vector<gameplay_timed_message> gameplay_potion_use_sequence(
    std::uint32_t player_entity_id,
    std::uint32_t item_skill_id,
    std::uint8_t action_sequence,
    std::uint8_t action_state,
    const gameplay_inventory_item& remaining,
    std::uint64_t server_time_milliseconds,
    gameplay_movement position);
// 05 38 update of the potion's two heal-over-time abnormals: `remaining` counts down by 9 per
// tick (0x30 at the start) and `stacks` from 5; stacks 0 removes the effect.
[[nodiscard]] std::vector<std::vector<std::uint8_t>> encode_gameplay_potion_tick(
    std::uint32_t player_entity_id, std::uint32_t item_skill_id, std::uint8_t remaining, std::uint8_t stacks);
// 6D 8D: an environment object was used. Sent alone it opens a MapEvent door (official: the
// Blocked Door right after the first tutorial encounter is cleared).
[[nodiscard]] std::vector<std::uint8_t> encode_gameplay_object_used(std::uint32_t object_entity_id);
// 15 8D Tutorial_Basic_* prompt with its length-prefixed parameter ("0", "6000", ...).
[[nodiscard]] std::vector<std::uint8_t> encode_gameplay_tutorial_prompt(
    std::string_view name,
    std::string_view parameter);
// 15 8D Talk_* prompt, optionally spoken by an entity (0 for none).
[[nodiscard]] std::vector<std::uint8_t> encode_gameplay_talk_prompt(
    std::string_view name,
    std::uint32_t speaker_entity_id);
// Replies to client 33 38 (34 38 target_selected) and 30 38 (31 38 target_selection_state).
[[nodiscard]] std::vector<std::uint8_t> encode_gameplay_target_selected(std::uint32_t target_entity_id);
[[nodiscard]] std::vector<std::uint8_t> encode_gameplay_target_state(std::uint32_t target_entity_id);

// Client 36 36: the player reached or used an environment object.
struct gameplay_interaction {
    std::uint32_t object_entity_id{};
    std::uint32_t template_id{};
};
[[nodiscard]] gameplay_interaction parse_gameplay_interaction(std::span<const std::uint8_t> body);
// Target entity of client 30 38 / 33 38 (0 clears the target).
[[nodiscard]] std::uint32_t parse_gameplay_target_request(std::span<const std::uint8_t> body);

// FF FF world-snapshot container: u32 decompressed size, then an LZ4 block of records framed
// like top-level frames (uvarint body length + 4, then the body). Returns the record bodies.
[[nodiscard]] std::vector<std::vector<std::uint8_t>> unpack_gameplay_snapshot(std::span<const std::uint8_t> body);

} // namespace sema
