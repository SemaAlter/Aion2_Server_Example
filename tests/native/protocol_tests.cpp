#include "sema/frame_codec.hpp"
#include "sema/gameplay.hpp"
#include "sema/handshake.hpp"
#include "sema/lobby.hpp"
#include "sema/data/game_data.hpp"
#include "sema/data/database.hpp"
#include "tutorial_script.hpp"
#include "tutorial_world.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<std::uint8_t> hex(const std::string& text) {
    std::vector<std::uint8_t> result;
    result.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        result.push_back(static_cast<std::uint8_t>(std::stoul(text.substr(i, 2), nullptr, 16)));
    }
    return result;
}

void frame_round_trip() {
    std::vector<std::uint8_t> body(142);
    for (std::size_t i = 0; i < body.size(); ++i) body[i] = static_cast<std::uint8_t>(i);
    const auto wire = sema::encode_frame(body);
    require(wire.size() == 144 && wire[0] == 0x92 && wire[1] == 0x01, "two-byte frame prefix mismatch");

    sema::frame_decoder decoder;
    sema::frame frame;
    decoder.append(std::span(wire).first(1));
    require(!decoder.try_read(frame), "fragmented prefix decoded too early");
    decoder.append(std::span(wire).subspan(1, 40));
    require(!decoder.try_read(frame), "fragmented body decoded too early");
    decoder.append(std::span(wire).subspan(41));
    require(decoder.try_read(frame), "complete frame did not decode");
    require(frame.declared_length == 146 && frame.body == body && decoder.buffered_byte_count() == 0,
        "decoded frame mismatch");
}

void coalesced_frames() {
    const auto bytes = hex("111D37D6242F03BFAB075AA55AA50E0036F9AAEA1FA1010000");
    sema::frame_decoder decoder;
    decoder.append(bytes);
    sema::frame first;
    sema::frame second;
    require(decoder.try_read(first) && decoder.try_read(second), "coalesced frames were not decoded");
    require(first.wire_length() == 14 && second.wire_length() == 11, "coalesced wire lengths mismatch");
    require(!decoder.try_read(first), "decoder emitted a third frame");
}

void handshake_layout() {
    const auto der = hex(
        "308201080282010100D04823B0524E2255DCAEAD43AF0EC3222207B20B4FA1E052AA784AAAD1680634BC31D01504C6E1CEFD634FBE43C98E0C4F3864F3A23A54E6DC96E6F737F61E91D3B31ACBED6709EAB59B2A37FB7E8E7E296C7BC206C95506D2A8A9B41D0EAE521EA70F27A7F7D2B9676B8CF353B52EC04909DA783F3D303F7E6003F2141E8DF0E8C2821F8F5A1182CF768646518EA465AFA5E8105DB09E12C9C3A4EF08D3F73AD8F3818793C8843EB8ECDBE3A9D8F15131E8898C7D11EEBF3D2F1BA94BF92073B6221C50FFEF1C9345E4997A3ABD357F9C9B4552317E89070809DE7B7513CBB9104CA12AB94F1626F5A9EAC3E3EC047F69611EB4BDE327FBE60C0CB4878B5EF5020103");
    std::vector<std::uint8_t> body{0x10, 0x36, 0x8C, 0x02};
    body.insert(body.end(), der.begin(), der.end());
    const auto trailer = hex("C70D000004060502455304");
    body.insert(body.end(), trailer.begin(), trailer.end());

    const auto client = sema::parse_client_hello(body);
    require(client.public_key_der == der && client.trailer == trailer, "client hello parse mismatch");

    std::vector<std::uint8_t> session_data(32);
    for (std::size_t i = 0; i < session_data.size(); ++i) session_data[i] = static_cast<std::uint8_t>(i + 1);
    const auto response = sema::create_server_hello(client, session_data);
    const auto server = sema::parse_server_hello(response);
    require(response.size() == 278, "server hello size mismatch");
    require(server.encrypted_session_data.size() == 256, "RSA ciphertext size mismatch");
    require(server.header == hex("1136000060EA0000"), "server hello header mismatch");
    require(server.trailer == hex("0000000000000000F9FFFFFF"), "server hello trailer mismatch");

    body[0] = 0x00;
    body[1] = 0x39;
    const auto lobby_client = sema::parse_lobby_client_hello(body);
    const auto lobby_response = sema::create_lobby_server_hello(lobby_client, session_data);
    require(lobby_response.size() == 274, "lobby server hello size mismatch");
    require(lobby_response[0] == 0x01 && lobby_response[1] == 0x39, "lobby server opcode mismatch");
}

void lobby_server_layout() {
    sema::lobby_server server;
    const auto body = sema::encode_lobby_server_list(std::span(&server, 1));
    require(body == hex("0939000001FF0F01FF0F00000A53656D61204C6F63616C01030A5001000101"),
        "current lobby server record mismatch");

    std::vector<sema::lobby_server> group(5, server);
    for (std::size_t i = 0; i < group.size(); ++i) {
        group[i].id = static_cast<std::uint16_t>(4090 + i);
        group[i].name = std::string(1, static_cast<char>('A' + i));
        group[i].status = sema::lobby_status::none;
        group[i].availability = i == 1
            ? sema::lobby_availability::unavailable
            : sema::lobby_availability::available;
    }
    const auto grouped = sema::encode_lobby_server_list(group);
    // First record starts at byte 5. Its packed availability byte follows the
    // fixed seven bytes and one-byte name: 1,0,1,1 => 0b01010001 (0x51).
    require(grouped[14] == 0x51, "four-entry availability mask mismatch");

    const auto redirect = sema::encode_lobby_redirect(server.id, "192.168.1.10", 13328);
    require(redirect == hex("0F390000FF0F0C3139322E3136382E312E31301034"), "lobby redirect mismatch");
    require(sema::encode_lobby_account(server.id).size() == 96, "lobby account layout mismatch");
    require(sema::encode_empty_lobby_characters() == hex("0B390000000101"),
        "empty lobby character list mismatch");
    require(sema::encode_lobby_join_accepted(1321) == hex("0D390000002905"),
        "lobby join acceptance mismatch");
}

void gameplay_login_layout() {
    static_assert(static_cast<std::uint16_t>(sema::gameplay_server_message::entity_spawn) == 0x3641);
    static_assert(static_cast<std::uint16_t>(sema::gameplay_server_message::skill_hit) == 0x3804);
    static_assert(static_cast<std::uint16_t>(sema::gameplay_server_message::tutorial_prompt) == 0x8D15);
    std::vector<std::uint8_t> proof{0x13, 0x36, 0x03, '1', ':', 'x', 0x00, 0x03, 'a', 'b', 'c'};
    proof.insert(proof.end(), 10, 0);
    const auto parsed = sema::parse_gameplay_client_login(proof);
    require(parsed.account_session == "1:x" && parsed.lobby_token == "abc", "gameplay client login parse mismatch");
    require(sema::has_gameplay_opcode(proof, sema::gameplay_client_message::login),
        "gameplay login opcode classification mismatch");
    require(sema::has_gameplay_opcode(hex("4FE2"), sema::gameplay_client_message::initialize_tutorial),
        "tutorial opcode classification mismatch");
    require(sema::has_gameplay_opcode(hex("3036"), sema::gameplay_client_message::exit_game_request),
        "exit-game request classification mismatch");
    require(sema::has_gameplay_opcode(hex("2536"),
            sema::gameplay_client_message::return_to_character_selection),
        "return-to-character-selection request classification mismatch");
    require(sema::has_gameplay_opcode(
            hex("34E30000154375747363656E655F4C5F415F5365715F313237387966000000000000"),
            sema::gameplay_client_message::cutscene_finished),
        "cutscene-finished request classification mismatch");
    require(sema::has_gameplay_opcode(hex("363601D1B2010000000000000000E1C81000"),
            sema::gameplay_client_message::tutorial_objective_progress),
        "tutorial objective progress classification mismatch");
    require(sema::has_gameplay_opcode(hex("0037020000"), sema::gameplay_client_message::movement_started),
        "tutorial movement opcode classification mismatch");
    const auto movement = sema::parse_gameplay_movement(
        hex("0137023D25C5C7408855477937BC460C01F628BC3F016A8DAA20A1010000"));
    require(movement.x < -100938.0F && movement.x > -100939.0F
            && movement.y > 54664.0F && movement.y < 54665.0F,
        "tutorial movement position parse mismatch");
    const auto cast_request = sema::parse_gameplay_client_cast(
        hex("00380101A126000002B10E000000000000000000000000000000000000000000000000"));
    require(cast_request.phase == sema::gameplay_cast_phase::initial
            && cast_request.skill_id == 9889 && cast_request.target_entity_id == 1841,
        "gameplay cast request parse mismatch");

    const auto login = sema::encode_empty_gameplay_login(parsed, 4095, 1000);
    require(login[0] == 0x15 && login[1] == 0x36, "gameplay login opcode mismatch");
    require(login[20] == 0, "empty gameplay login character count mismatch");
    require(std::vector<std::uint8_t>(login.begin() + 4, login.begin() + 12) == hex("E803000000000000"),
        "gameplay login timestamp mismatch");
    require(std::vector<std::uint8_t>(login.begin() + 12, login.begin() + 20) == hex("E877B70100000000"),
        "gameplay login expiration mismatch");
    const std::vector<std::vector<std::uint8_t>> login_entries{hex("0102030405")};
    const auto listed = sema::encode_empty_gameplay_login(parsed, 4095, 1000, login_entries);
    require(listed[20] == 1 && std::vector<std::uint8_t>(listed.begin() + 21, listed.begin() + 27) == hex("010203040500"),
        "gameplay login character entries mismatch");

    const auto tick = sema::encode_gameplay_tick(0x0102030405060708ULL);
    require(tick == hex("00360807060504030201"), "gameplay tick layout mismatch");

    const auto client_ping = sema::parse_gameplay_ping(hex("0236080706050403020100"));
    require(client_ping.client_time_milliseconds == 0x0102030405060708ULL, "gameplay ping parse mismatch");
    const auto tutorial_ping = sema::parse_gameplay_ping(hex("02360807060504030201A6A8858A0F"));
    require(tutorial_ping.client_time_milliseconds == client_ping.client_time_milliseconds,
        "tutorial gameplay ping parse mismatch");
    require(sema::encode_gameplay_ping(client_ping, 0x1112131415161718ULL)
            == hex("033608070605040302011817161514131211"),
        "gameplay ping response mismatch");
    require(sema::encode_gameplay_exit_result() == hex("31360000"),
        "gameplay exit result mismatch");
    require(sema::encode_gameplay_entity_disappear(0x933A, true) == hex("3536BAA602030300")
            && sema::encode_gameplay_entity_disappear(0xAE0B, false) == hex("35368BDC02010300"),
        "entity disappear mismatch");
    require(sema::rename_character(hex("1C36000009456C796F733030303101"), "Elyos0001", "Ab")
            == hex("1C360000024162" "01"),
        "character rename mismatch");
    // official-tutorial-full 249.28 s
    const auto map_end = sema::encode_gameplay_tutorial_map_end(100001, 0x000001A12718E5E4ULL);
    require(map_end.size() == 2 && map_end[0] == hex("016100A18601000100")
            && map_end[1] == hex("0061A186010003E4E51827A1010000A40D2227A1010000"),
        "tutorial map end mismatch");
    auto scheduled_event = hex("845601A86CF21101000000C6768620A101000000");
    require(sema::refresh_gameplay_server_time(scheduled_event, 0x0102030405060708ULL)
            && scheduled_event == hex("845601A86CF21101000000080706050403020100"),
        "scheduled-event server time refresh mismatch");

    const auto dash_request = sema::parse_gameplay_client_cast(
        hex("00380101A10F000002B10E2C6B1C43C3E2C2C2C0C1B34100000000020000000000000000"));
    const sema::gameplay_skill_definition dash_skill{
        4001, "Tutorial Dash", sema::gameplay_skill_kind::dash, 4002, 4001, 0, false, false, 0};
    const auto dash = sema::gameplay_tutorial_dash_sequence(dash_request, dash_skill, 1841, 1, 0x12,
        0x000001A120936746ULL, {std::bit_cast<float>(0xC7C38193U), std::bit_cast<float>(0x4755834CU), 24064.0F});
    require(dash.size() == 8 && dash.front().body == hex("038DB10E00000000"),
        "tutorial dash sequence mismatch");
    // The official dash action start (official-tutorial-dash) is 43 bytes.
    require(dash[4].body == hex("2A38B10E011112C80000002C0100000000000046679320A1010000B10E01009381C3C74C8355470000BC46")
            && dash[7].body == hex("2C38B10E01001207")
            && std::ranges::none_of(dash, [](const auto& message) { return message.body[0] == 0x22 || message.body[0] == 0x42; }),
        "tutorial dash action start, state, or sprint handling mismatch");
    require(dash[2].body[0] == 0x02 && dash[2].body[1] == 0x38,
        "tutorial dash cast is missing");
    require(std::vector<std::uint8_t>(dash[2].body.begin() + 13, dash[2].body.begin() + 29)
            == hex("2C6B1C43C3E2C2C2C0C1B34100000000"),
        "tutorial dash facing and direction mismatch");

    const sema::gameplay_skill_definition primary_skill{
        9865, "Primary Attack 1", sema::gameplay_skill_kind::primary_attack,
        9865, 9866, 9866, false, true, 0};
    const auto primary = sema::gameplay_tutorial_attack_sequence(
        {sema::gameplay_cast_phase::initial, 9865, 1841, {}, {}}, primary_skill, 1841, 2);
    require(primary.size() == 7 && primary[1].body[0] == 0x02 && primary[2].body[0] == 0x18
            && primary[4].body == hex("218DB10E0001") && primary.back().body[0] == 0x06,
        "tutorial primary attack sequence mismatch");
    const sema::gameplay_skill_definition primary_second_skill{
        9866, "Primary Attack 2", sema::gameplay_skill_kind::primary_attack,
        9866, 9867, 9867, true, false, 0};
    const auto primary_second = sema::gameplay_tutorial_attack_sequence(
        {sema::gameplay_cast_phase::combo, 9866, 1841, {}, {}}, primary_second_skill, 1841, 3);
    require(primary_second.size() == 7 && primary_second[2].body[0] == 0x18
            && primary_second[3].body[0] == 0x19,
        "tutorial primary combo continuation mismatch");
    const sema::gameplay_skill_definition primary_third_skill{
        9867, "Primary Attack 3", sema::gameplay_skill_kind::primary_attack,
        9867, 9865, 0, true, false, 3000};
    const auto primary_third = sema::gameplay_tutorial_attack_sequence(
        {sema::gameplay_cast_phase::combo, 9867, 1841, {}, {}}, primary_third_skill, 1841, 4);
    require(primary_third.size() == 7 && primary_third[2].body[0] == 0x19
            && primary_third.back().delay_milliseconds == 3000
            && primary_third.back().body == hex("218DB10E0000"),
        "tutorial primary combo completion mismatch");
    const sema::gameplay_skill_definition secondary_skill{
        9868, "Secondary Attack 1", sema::gameplay_skill_kind::secondary_attack,
        9868, 9869, 0, false, false, 0};
    const std::array targets{sema::gameplay_attack_target{27516, 50, 50, false}};
    const auto secondary = sema::gameplay_tutorial_attack_sequence(
        {sema::gameplay_cast_phase::initial, 9868, 27516, {}, {}}, secondary_skill, 1841, 5, targets);
    require(secondary.size() == 7 && secondary[4].body[0] == 0x04 && secondary[4].body[1] == 0x38
            && secondary[5].body[0] == 0x00 && secondary[5].body[1] == 0x8D,
        "tutorial secondary hit sequence mismatch");
    require(secondary[4].body == hex("0438FCD6012400B10E8C2600000502BB0E0F0001000000904E08030101010100"),
        "tutorial hit payload mismatch");

    const auto spawn = sema::encode_gameplay_monster_spawn(
        27516, 2702072, -94815.0F, 54564.0F, 23676.0F, 9.84375F, 100);
    require(spawn == hex("4136FCD6010C2000F83A29000002802FB9C70024554700F8B84600801D4100070185018501640000006400000000000000000000000000000000000000000000000000000001000000000000000000000000000000000000000B0006010001000000B928349400"),
        "tutorial monster spawn layout mismatch");
    require(sema::encode_gameplay_monster_movement_started(
                27516, {-94815.0F, 54564.0F, 23676.0F}, 9.74F)
            == hex("1B37FCD6010503802FB9C70024554700F8B846ED06ED06"),
        "tutorial monster movement-start layout mismatch");
    require(sema::encode_gameplay_monster_movement_stopped(
                27516, {-94336.03125F, 55158.16796875F, 23679.0F}, 9.74F)
            == hex("1A37FCD60100000440B8C72B76574700FEB846ED06"),
        "tutorial monster movement-stop layout mismatch");
    require(sema::encode_gameplay_monster_movement_updated(
                27516, 1.0F, 9.0F / 53.0F, 9.74F)
            == hex("1D37FCD60127033509ED06ED06"),
        "tutorial monster movement-update layout mismatch");
    const auto activation = sema::gameplay_monster_activation_sequence(27516, 1841);
    require(activation.size() == 3 && activation[0].body == hex("528DFCD601")
            && activation[1].body == hex("3538FCD60100B10E")
            && activation[2].body == hex("218DFCD6010001"),
        "tutorial monster activation layout mismatch");
    // Official cast (official-tutorial-combat-door 75.47 s): facing, then the target's position.
    const auto official_cast = sema::gameplay_monster_attack_sequence(48429, 3363, 1224230, 1,
        {std::bit_cast<float>(0xC7B7E8A8U), std::bit_cast<float>(0x47559429U), std::bit_cast<float>(0x46B8FA00U)},
        std::bit_cast<float>(0x411BD8F9U), 1, 119);
    require(official_cast[0].body == hex("0238ADFA020026AE12000102A31AF9D81B41A8E8B7C729945547" "00FAB846904E01E807"),
        "monster cast does not match the official layout");
    const auto monster_attack = sema::gameplay_monster_attack_sequence(
        27516, 1841, 1224230, 7,
        {-94815.0F, 54564.0F, 23676.0F}, 9.84375F, 1, 119);
    require(monster_attack.size() == 4
            && monster_attack[0].body == hex("0238FCD6010026AE12000702B10E00801D41802FB9C70024554700F8B846904E01E807")
            && monster_attack[1].body == hex("0438B10E0600FCD60126AE12000702000001E3064C0701000000904E010100")
            && monster_attack[2].body == hex("008DB10E0201007700000000000000")
            && monster_attack[3].body == hex("0638FCD60126AE12000700"),
        "tutorial monster attack layout mismatch");
    const auto light = sema::gameplay_tutorial_light_prelude(1841, 0x0102030405060708ULL, 0x08);
    require(light.size() == 6
            && light[0].body == hex("35E30000154375747363656E655F4C5F415F5365715F313237387966000000000000")
            && light[1].body == hex("2C38B10E0100080F")
            && light[2].body == hex("8456010C6DF21101000000080706050403020100")
            && light[3].body == hex("02E0796600000000000007060000")
            && light.back().body == hex("418D0000"),
        "tutorial light transition mismatch");
    require(sema::encode_gameplay_tutorial_event_state(2, 0x0102030405060708ULL)
            == hex("8456010C6DF21102000000080706050403020100"),
        "tutorial event completion mismatch");
    require(sema::encode_gameplay_tutorial_monster_prompt()
            == hex("158D002E115475746F7269616C5F42617369635F3033000000000000000000000000000130"),
        "tutorial monster prompt mismatch");
    require(sema::encode_gameplay_tutorial_movement_prompt()
            == hex("158D002E115475746F7269616C5F42617369635F3032000000000000000000000000000436303030"),
        "tutorial movement prompt mismatch");
    // Tutorial progression records, checked against official-tutorial-combat-door.
    const std::vector<std::uint8_t> encounter_flags{0, 1, 0};
    require(sema::encode_gameplay_quest_step(0x0002A503, 2, 3, encounter_flags)
            == hex("2D9203A50200020203030001000800000000000000000000000000000000"),
        "quest step layout mismatch");
    require(sema::encode_gameplay_map_event(311100100, 5, 0x000001A12227047AULL)
            == hex("845601C4028B12050000007A042722A101000000"),
        "map event layout mismatch");
    const auto checkpoint_chain = sema::gameplay_interaction_sequence(34896, 3363, false);
    require(checkpoint_chain.size() == 5 && checkpoint_chain[0].body == hex("3836D09002A31A01")
            && checkpoint_chain[1].body == hex("3A36D09002A31A0000000000000000")
            && checkpoint_chain[2].body == hex("6D8DD09002") && checkpoint_chain[3].body == hex("3B36D09002")
            && checkpoint_chain[4].body == hex("3736000000D090020000000000000000"),
        "checkpoint interaction layout mismatch");
    const auto door_chain = sema::gameplay_interaction_sequence(45148, 3363, true);
    require(door_chain.size() == 4 && door_chain[0].body == hex("3836DCE002A31A02")
            && door_chain[1].body == hex("3A36DCE002A31A0100000000000000")
            && door_chain[3].body == hex("3736000000DCE0020000000000000000"),
        "door interaction layout mismatch");
    require(sema::encode_gameplay_tutorial_prompt("Tutorial_Basic_06", "10000")
            == hex("158D002E115475746F7269616C5F42617369635F303600000000000000000000000000053130303030"),
        "tutorial prompt layout mismatch");
    require(sema::encode_gameplay_talk_prompt("Talk_L_Starter_InstanceLayer_1_32", 55173)
            == hex("158D012B2154616C6B5F4C5F537461727465725F496E7374616E63654C617965725F315F3332000000000000000000000000000185AF0300")
            && sema::encode_gameplay_talk_prompt("Talk_L_Starter_InstanceLayer_1_04", 0)
            == hex("158D00132154616C6B5F4C5F537461727465725F496E7374616E63654C617965725F315F30340000000000000000000100000000"),
        "talk prompt layout mismatch");
    require(sema::encode_gameplay_target_selected(45148) == hex("3438000000DCE002")
            && sema::encode_gameplay_target_selected(0) == hex("343800000000")
            && sema::encode_gameplay_target_state(41778) == hex("31380000B2C6020000")
            && sema::parse_gameplay_target_request(hex("3338008FD103")) == 59535,
        "target reply layout mismatch");
    // Life Potion use and kill record (official-tutorial-combat-door 84.2 s, official-secondary 8.2 s).
    const auto potion = sema::gameplay_potion_use_sequence(3363, 2011101, 0x04, 0x10,
        {0x0000150000130AE6ULL, 530126003, 99}, 0x000001A12224C44CULL,
        {std::bit_cast<float>(0xC7B7E8A8U), std::bit_cast<float>(0x47559429U), std::bit_cast<float>(0x46B8FA00U)});
    require(potion.size() == 8 && potion[0].body == hex("0238A31A00DDAF1E000400A31A0000B443904E0100")
            && potion[1].body == hex("0638A31ADDAF1E000400")
            && potion[2].body == hex("1B56010100E60A130000150000B314991F6300000000000000010000000000000000000000000000002600")
            && potion[3].body == hex("1656E60A130000150000B314991F")
            && potion[4].body == hex("0438A31A0000A31ADDAF1E0004025FB2FC0B01000000904E0100")
            && potion[5].body == hex("2A38A31A011310A3DE320110270000000000004CC42422A1010000A31A01DDAF1E0000A8E8B7C72994554700FAB846")
            && potion[6].body == hex("0538A31A09A31A105FB2FC0B30DDAF1E00")
            && potion[7].body == hex("0538A31A09A31A1060B2FC0B05DDAF1E00"),
        "potion use sequence does not match the official layout");
    require(sema::encode_gameplay_potion_tick(3363, 2011101, 0x27, 4)
            == std::vector<std::vector<std::uint8_t>>{hex("0538A31A0BA31A105FB2FC0B2709DDAF1E00"),
                hex("0538A31A0BA31A1060B2FC0B0401DDAF1E00")}
            && sema::encode_gameplay_potion_tick(3363, 2011101, 0x03, 0)
            == std::vector<std::vector<std::uint8_t>>{hex("0538A31A0BA31A105FB2FC0B0309DDAF1E00"),
                hex("0538A31A0AA31A1060B2FC0B01DDAF1E00")},
        "potion tick layout mismatch");
    require(sema::encode_gameplay_kill_record(27516, 9869, 1560, "$Q7mXv2LkP9a")
            == hex("048DFCD6018D260000980C20050C2451376D5876324C6B50396100000000000000000100"),
        "kill record layout mismatch");
    require(sema::encode_gameplay_jump_landed(3363, 0x28, 0x000001A12226AA3CULL,
                {std::bit_cast<float>(0xC7A24D3CU), std::bit_cast<float>(0x47559C24U), std::bit_cast<float>(0x46B43BEAU)})
            == hex("2A38A31A011128E7000000C8000000000000003CAA2622A1010000A31A01003C4DA2C7249C5547EA3BB446"),
        "jump landing layout mismatch");
    require(sema::encode_gameplay_entity_resource(3363, 0x03, 90'000) == hex("008DA31A010103905F0100")
            && sema::encode_gameplay_entity_resource(3363, 0x01, 1'300) == hex("008DA31A01010114050000")
            && sema::encode_gameplay_social_action(59535, 20001, false) == hex("08958FD103214E00000200000000")
            && sema::encode_gameplay_takeoff_height(3363, std::bit_cast<float>(0x46B440FFU)) == hex("4637A31AFF40B446"),
        "resource, social action, or takeoff layout mismatch");
    require(sema::encode_gameplay_sprint_end(3363) == std::vector<std::vector<std::uint8_t>>{hex("2237A31A"), hex("4237A31A")},
        "sprint end layout mismatch");
    require(sema::encode_gameplay_action_finished(3363, 0x10, 0x01) == hex("2C38A31A01001001"),
        "action end layout mismatch");
    // Positions from a local session: dash path update and movement snapshot.
    const auto dash_path = sema::parse_gameplay_movement(hex("0F37027D38BAC72B2756473BFDB84609E073BF28B49BBE000000001B034141000000"));
    const auto snapshot = sema::parse_gameplay_movement(hex("18370000A4C9B9C7F16D5647D3FCB8464C420B417738F126A1010000"));
    require(dash_path.x == std::bit_cast<float>(0xC7BA387DU) && snapshot.x == std::bit_cast<float>(0xC7B9C9A4U)
            && snapshot.z == std::bit_cast<float>(0x46B8FCD3U),
        "dash path or snapshot position parse mismatch");
    const auto reached = sema::parse_gameplay_interaction(hex("363601D1B2010000000000000000E1C81000"));
    require(reached.object_entity_id == 22865 && reached.template_id == 1100001, "interaction parse mismatch");
    // Literal-only and back-reference LZ4 blocks, each holding one framed record.
    require(sema::unpack_gameplay_snapshot(hex("FFFF040000004007010203")) == std::vector<std::vector<std::uint8_t>>{hex("010203")}
            && sema::unpack_gameplay_snapshot(hex("FFFF06000000" "2009AA0100")) == std::vector<std::vector<std::uint8_t>>{hex("AAAAAAAAAA")},
        "snapshot unpack mismatch");

    const auto player_name = sema::parse_gameplay_player_name(hex("1A36000009456C796F733030303101"));
    require(player_name.name == "Elyos0001" && player_name.state == 1,
        "gameplay player-name layout mismatch");
    const auto minimap = sema::parse_gameplay_minimap_filter_settings(hex("408D070A000000789C00"));
    require(minimap.declared_compressed_length == 7 && minimap.uncompressed_length == 10
            && minimap.zlib_data == hex("789C00"),
        "gameplay minimap settings envelope mismatch");
}

void game_data_catalogs_and_walk_mesh() {
    const auto directory = std::filesystem::temp_directory_path() / "sema-game-data-tests";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory / "walk-meshes");
    std::filesystem::create_directories(directory / "generated");
    const auto write = [&](const char* name, const char* contents) {
        std::ofstream output(directory / name);
        output << contents;
    };
    write("skills.csv", "id,name,max_level,cooldown_ms,cast_time_ms,range,target,hit_shape,max_targets,angle_degrees\n1,\"Strike, Light\",3,500,100,4.5,hostile,frontal_cone,4,120\n");
    {
        std::ofstream generated(directory / "generated" / "skills.csv");
        generated << "id,name,max_level,cooldown_ms,cast_time_ms,range,target,hit_shape,max_targets,angle_degrees\n"
                  << "1,Client Strike,1,0,0,3,hostile,single,1,0\n"
                  << "6,Client Passive,1,0,0,0,self,single,1,0\n";
    }
    write("items.csv", "id,name,category,max_stack,required_level\n2,Potion,consumable,20,1\n");
    write("npcs.csv", "id,name,kind,level,faction,interaction\n3,Training Dummy,monster,1,neutral,\n");
    write("monsters.csv", "id,npc_id,rank,max_health,attack,defense,aggro_radius,move_speed,attack_range,server_skill_id,attack_interval_ms\n4,3,normal,100,5,2,8,3.5,3,1224230,1500\n");
    write("spawns.csv", "id,template_id,kind,map_id,x,y,z,heading,respawn_seconds\n5,4,monster,100,2,2,1,90,30\n"
        "55173,4,monster,100,5000,5000,1,0,30\n");
    write("walk-meshes/100.obj", "v 0 0 0\nv 10 0 0\nv 0 10 10\nf 1 2 3\n");
    write("database.ini", "[database]\nhost=127.0.0.1\nport=33066\nuser=root\npassword=test\ndatabase=aion2_client_data\nclient_library=libmysql.dll\n");

    const auto database = sema::data::load_database_config(directory / "database.ini");
    require(database.host == "127.0.0.1" && database.port == 33066 && database.user == "root"
            && database.password == "test" && database.database == "aion2_client_data"
            && database.client_library == "libmysql.dll",
        "database config load mismatch");

    const auto data = sema::data::load_game_data(directory);
    require(data.skills.size() == 2 && data.skills.find(1)->name == "Strike, Light"
            && data.skills.find(1)->hit_shape == sema::data::skill_hit_shape::frontal_cone
            && data.skills.find(1)->max_targets == 4
            && data.skills.find(1)->angle_degrees == 120.0F,
        "skill data load mismatch");
    require(data.skills.find(6) && data.skills.find(6)->name == "Client Passive",
        "generated client skill was not retained");
    require(data.items.size() == 1 && data.npcs.size() == 1 && data.monsters.size() == 1
        && data.spawns.size() == 2, "world catalog load mismatch");
    require(data.monsters.find(4)->server_skill_id == 1224230
            && data.monsters.find(4)->attack_interval_ms == 1500
            && data.monsters.find(4)->attack_range == 3.0F,
        "monster combat data load mismatch");
    const auto mesh = data.walk_meshes.find(100);
    require(mesh != data.walk_meshes.end() && mesh->second.contains(2, 2), "walk mesh containment mismatch");
    const auto projected = mesh->second.project(2, 2);
    require(projected && projected->z > 1.9F && projected->z < 2.1F, "walk mesh projection mismatch");
    require(!mesh->second.project(20, 20), "walk mesh accepted an outside point");

    tutorial_world world(data, 100);
    world.update_player_position({0.0F, 0.0F, 0.0F});
    require(world.monster(5)->group == 1 && world.monster(55173)->group == 2, "tutorial spawn groups mismatch");
    require(world.apply_attack(sema::gameplay_skill_kind::primary_attack, sema::data::skill_hit_shape::radius,
                4, 0, {0.0F, 0.0F, 0.0F}, 50000.0F, 0.0F, 0.0F).empty(),
        "an unspawned monster was attackable");
    (void)world.spawn_group(1, 1841, true, false);
    const auto primary_targets = world.apply_attack(sema::gameplay_skill_kind::primary_attack,
        sema::data::skill_hit_shape::frontal_cone,
        4, 999, {0.0F, 0.0F, 0.0F}, 5.0F, 0.0F, 120.0F);
    require(primary_targets.size() == 1 && primary_targets.front().entity_id == 5,
        "self-targeted primary attack did not select the nearest in-range monster");
    const auto distant_secondary = world.apply_attack(sema::gameplay_skill_kind::secondary_attack,
        sema::data::skill_hit_shape::frontal_cone,
        4, 0, {100.0F, 100.0F, 0.0F}, 5.0F, 0.0F, 120.0F);
    require(distant_secondary.empty(), "secondary attack selected a monster outside its range");
    const auto behind = world.apply_attack(sema::gameplay_skill_kind::primary_attack,
        sema::data::skill_hit_shape::frontal_cone,
        4, 0, {4.0F, 2.0F, 0.0F}, 5.0F, 0.0F, 120.0F);
    require(behind.empty(), "frontal skill selected a monster behind the player");

    // The monster chases a distant player: walk start, updates, stop inside its reach, then a cast.
    world.update_player_position({1000.0F, 2.0F, 1.0F});
    const auto opcode = [](const sema::gameplay_timed_message& message) {
        return static_cast<std::uint16_t>(message.body[0] << 8 | message.body[1]);
    };
    bool started = false, updated = false, stopped = false, attacked = false;
    std::uint64_t elapsed = 0;
    while (!attacked && elapsed < 10'000) {
        for (const auto& message : world.tick(1841, 100)) {
            started |= opcode(message) == 0x1B37;
            updated |= started && opcode(message) == 0x1D37;
            stopped |= updated && opcode(message) == 0x1A37;
            attacked |= opcode(message) == 0x0238;
        }
        elapsed += 100;
    }
    require(started && updated && stopped && attacked, "monster did not chase the player before attacking");

    for (int hit = 0; hit < 20; ++hit) {
        (void)world.apply_attack(sema::gameplay_skill_kind::primary_attack,
            sema::data::skill_hit_shape::frontal_cone, 4, 0, {1000.0F, 2.0F, 1.0F}, 400.0F, 180.0F, 120.0F);
    }
    require(world.group_cleared(1) && world.is_dead(5), "group 1 was not cleared");

    // An idle hostile group engages only when the player comes close.
    const auto group2 = world.spawn_group(2, 1841, false, false);
    require(group2.size() == 1 && opcode(group2.front()) == 0x4136, "group 2 spawn mismatch");
    const auto activated = [&] {
        bool found = false;
        for (const auto& message : world.tick(1841, 100)) found |= opcode(message) == 0x528D;
        return found;
    };
    require(!activated(), "a distant idle monster engaged");
    world.update_player_position({4500.0F, 5000.0F, 1.0F});
    require(activated(), "an idle monster did not engage when the player came close");

    // A dash spends 10000 SP and a secondary attack 200 MP; with a monster engaged, one second
    // regenerates the battle rates (5000 SP, 25 MP).
    (void)world.spend_resources(1841, 200, 10'000);
    require(world.player_sp() == 90'000 && world.player_mp() == 1'300, "skill costs were not applied");
    for (int step = 0; step < 10; ++step) (void)world.tick(1841, 100);
    require(world.player_sp() == 95'000 && world.player_mp() == 1'325, "resources did not regenerate at battle rates");
    world.start_sprint();
    for (int step = 0; step < 10; ++step) (void)world.tick(1841, 100);
    require(world.stop_sprint() && !world.stop_sprint(), "sprint state mismatch");
    require(world.player_sp() == 90'000, "sprint did not drain 500 SP per 100 ms");

    const auto now = std::chrono::steady_clock::now();
    require(world.try_use_skill(9865, 1000, now), "first cast was rejected");
    require(!world.try_use_skill(9865, 1000, now + std::chrono::milliseconds(500)), "cast ignored its cooldown");
    require(world.try_use_skill(9866, 1000, now + std::chrono::milliseconds(500)), "cooldowns are not per skill");
    require(world.try_use_skill(9865, 1000, now + std::chrono::milliseconds(950)),
        "cast inside the latency allowance was rejected");
    std::filesystem::remove_all(directory);
}

void loot_graph_and_world_indexes() {
    sema::data::game_data_store data;
    data.items.add({10, "Potion", sema::data::item_category::consumable, 20, 1});
    data.items.add({11, "Sword", sema::data::item_category::weapon, 1, 1});
    data.items.add({12, "Low-level gem", sema::data::item_category::material, 99, 1});
    data.items.add({13, "High-level gem", sema::data::item_category::material, 99, 1});
    // 1 = monster table: a guaranteed potion, a never-dropping sword, and a level-scaled group (2)
    data.loot_tables.add({1, "reward_monster", {{10, 2, 2, 1.0F}, {11, 1, 1, 0.0F}},
        {{2, 1.0F, 1, 0, 0, sema::data::loot_link_source::package}}});
    data.loot_tables.add({2, "reward_basic_group", {},
        {{3, 1.0F, 1, 1, 10, sema::data::loot_link_source::level_scale},
            {4, 1.0F, 1, 11, 20, sema::data::loot_link_source::level_scale}}});
    data.loot_tables.add({3, "reward_basic_group_low", {{12, 1, 1, 1.0F}}, {}});
    data.loot_tables.add({4, "reward_basic_group_high", {{13, 1, 1, 1.0F}}, {}});
    data.add_loot_owner(sema::data::loot_owner_kind::npc, 500, 1);
    data.add_loot_owner(sema::data::loot_owner_kind::npc, 500, 1);  // duplicate links are ignored
    data.npcs.add({500, "Test Monster", sema::data::npc_kind::monster, 5, sema::data::faction::neutral, ""});
    data.spawns.add({7, 500, sema::data::spawn_kind::npc, 100, {1, 2, 3}, 0, 0});
    data.spawns.add({6, 500, sema::data::spawn_kind::npc, 100, {4, 5, 6}, 0, 0});
    data.env_objects.add({900, "Herb", "GatherSource", "Herb_01", 80, 2500});
    data.env_object_spawns.add({1, 900, 100, {7, 8, 9}, 0});
    data.build_indexes();
    data.validate();

    const auto owned = data.loot_tables_for(sema::data::loot_owner_kind::npc, 500);
    require(owned.size() == 1 && owned.front() == 1 && data.loot_owner_count() == 1, "loot owner index mismatch");
    require(data.loot_tables_for(sema::data::loot_owner_kind::env_object, 500).empty(), "loot owner kinds are not separate");
    std::mt19937 random(42);
    const auto low = sema::data::roll_loot(data, owned.front(), 5, random);
    require(low.size() == 2 && low[0].item_id == 10 && low[0].count == 2 && low[1].item_id == 12,
        "loot roll ignored chances or level ranges");
    const auto high = sema::data::roll_loot(data, owned.front(), 15, random);
    require(high.size() == 2 && high[1].item_id == 13, "level-scaled loot picked the wrong level range");

    const auto spawns = data.spawns_on(100);
    require(spawns.size() == 2 && spawns[0] == 6 && spawns[1] == 7, "per-map spawn index is not sorted by ID");
    require(data.env_object_spawns_on(100).size() == 1 && data.spawns_on(101).empty(), "per-map index mismatch");

    data.loot_tables.add({5, "broken", {{999, 1, 1, 1.0F}}, {}});
    bool rejected = false;
    try { data.validate(); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "validation accepted a loot entry for an unknown item");
}

} // namespace

void tutorial_script_progression() {
    sema::data::game_data_store data;
    data.npcs.add({2701091, "Monster", sema::data::npc_kind::monster, 1, sema::data::faction::neutral, ""});
    data.monsters.add({2701091, 2701091, sema::data::monster_rank::normal, 16, 1, 0, 20, 5.4F, 1.8F, 1224230, 1500});
    data.spawns.add({42773, 2701091, sema::data::spawn_kind::monster, 202300, {-94000, 54600, 23676}, 0, 0});
    tutorial_world world(data, 202300);

    // Bootstrap: a quest step and the Arrival Points / door of the official run.
    const auto object = [](const std::uint32_t entity, const std::uint32_t template_id, const float x, const float y) {
        std::vector<std::uint8_t> body{0x34, 0x36};
        for (auto value = entity;; value >>= 7) {
            body.push_back(static_cast<std::uint8_t>((value & 0x7F) | (value > 0x7F ? 0x80 : 0)));
            if (value <= 0x7F) break;
        }
        body.insert(body.end(), {0x01, 0x00});
        for (const auto value : {template_id, std::bit_cast<std::uint32_t>(x), std::bit_cast<std::uint32_t>(y),
                 std::bit_cast<std::uint32_t>(23676.0F)}) {
            for (int i = 0; i < 4; ++i) body.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
        }
        body.resize(57);
        return body;
    };
    const std::vector<std::uint8_t> first_step{1};
    replay_scenario bootstrap{"test", 1841, 202300, {
        {std::chrono::milliseconds(0), sema::encode_gameplay_quest_step(0x968A, 1, 1, first_step)},
        {std::chrono::milliseconds(0), object(22865, 1100001, -97703, 54664)},
        {std::chrono::milliseconds(0), object(44683, 1100001, -95931, 54664)},
        {std::chrono::milliseconds(0), object(40972, 1100001, -88237, 54664)},
        {std::chrono::milliseconds(0), object(38052, 1400218, -76338, 64478)},
        {std::chrono::milliseconds(0), object(17903, 1400217, -88818, 54664)},
    }};
    tutorial_script script(bootstrap, std::nullopt, std::nullopt);
    require(script.quest_instance() == 0x968A && script.objects().size() == 5
            && script.checkpoint_of(40972) == tutorial_script::checkpoint::corridor
            && !script.checkpoint_of(38052),
        "tutorial bootstrap objects were not read");

    const auto has = [](const std::vector<sema::gameplay_timed_message>& messages, const std::vector<std::uint8_t>& body) {
        return std::ranges::any_of(messages, [&](const auto& message) { return message.body == body; });
    };
    const auto step = [](std::uint8_t chapter, std::uint8_t number, std::vector<std::uint8_t> flags) {
        return sema::encode_gameplay_quest_step(0x968A, chapter, number, flags);
    };
    const auto movement = script.on_interaction({22865, 1100001}, 1841, world, 0);
    require(has(movement, step(1, 3, {1})) && has(movement, step(1, 4, {0}))
            && has(movement, sema::encode_gameplay_tutorial_movement_prompt())
            && script.on_interaction({22865, 1100001}, 1841, world, 0).empty(),
        "movement checkpoint progression mismatch");
    require(script.on_interaction({40972, 1100001}, 1841, world, 0).empty(),
        "the corridor checkpoint fired before the first encounter was cleared");

    (void)world.spawn_group(1, 1841, true, false);
    world.update_player_position({-94000, 54600, 23676});
    const auto targets = world.apply_attack(sema::gameplay_skill_kind::primary_attack,
        sema::data::skill_hit_shape::radius, 4, 0, {-94000, 54600, 23676}, 400, 0, 0);
    const auto targets_again = world.apply_attack(sema::gameplay_skill_kind::primary_attack,
        sema::data::skill_hit_shape::radius, 4, 0, {-94000, 54600, 23676}, 400, 0, 0);
    require(targets.size() == 1 && !targets[0].killed && targets_again.size() == 1 && targets_again[0].killed,
        "test monster did not die on the second hit");
    const auto kills = script.on_kills(targets_again, 9865, 1841, world, 1000);
    require(has(kills, sema::encode_gameplay_map_event(311100100, 1, 1000))
            && has(kills, sema::encode_gameplay_map_event(311100200, 1, 1000))
            && has(kills, sema::encode_gameplay_map_event(310100100, 1, 1000))
            && has(kills, step(1, 5, {1, 0, 0, 0})) && has(kills, step(2, 1, {0, 0}))
            && has(kills, sema::encode_gameplay_object_used(17903))
            && has(kills, sema::encode_gameplay_talk_prompt("Talk_L_Starter_InstanceLayer_1_04", 0)),
        "encounter kill progression mismatch");
    const auto corridor = script.on_interaction({40972, 1100001}, 1841, world, 0);
    require(has(corridor, step(2, 1, {1, 0})) && corridor.front().body[0] == 0x38,
        "corridor checkpoint progression mismatch");
    require(script.on_interaction({38052, 1400218}, 1841, world, 0).empty(),
        "the castle door opened before the room checkpoint");

    // The handoff phase was recorded with quest instance 0x2103F and light object 50211.
    std::vector<replay_event> handoff{
        {std::chrono::milliseconds(0), hex("3836A38803870C01")},
        {std::chrono::milliseconds(0), hex("2D923F10020002010401010800000000000000000000000000000000")},
        {std::chrono::milliseconds(0), hex("2A38B10E011108122700008813")},
        {std::chrono::milliseconds(0), hex("2B38B10E1108122700009323")},
    };
    script.adapt_handoff(handoff, 0x0C);
    require(handoff[0].body == hex("38368BDD02870C01")
            && handoff[1].body == hex("2D928A96000002010401010800000000000000000000000000000000")
            && handoff[2].body == hex("2A38B10E01110C122700008813")
            && handoff[3].body == hex("2B38B10E110C122700009323"),
        "cutscene handoff was not adapted to the session");

    // The checked-in scenarios: every checkpoint of the official run exists in our bootstrap.
    const std::filesystem::path scenarios = "scenarios";
    if (std::filesystem::is_regular_file(scenarios / "tutorial-bootstrap.json")) {
        const tutorial_script real(load_replay_scenario(scenarios / "tutorial-bootstrap.json"),
            load_replay_phase(scenarios / "tutorial-room-spawns.json"), load_replay_phase(scenarios / "tutorial-exit.json"));
        std::array<bool, 9> found{};
        for (const auto& environment : real.objects()) {
            if (const auto reached = real.checkpoint_of(environment.entity_id)) found[static_cast<std::size_t>(*reached)] = true;
        }
        require(real.quest_instance() == 0x968A && real.objects().size() == 25
                && std::ranges::all_of(found, [](const bool value) { return value; })
                && std::ranges::any_of(real.objects(), [](const auto& environment) {
                       return environment.template_id == tutorial_script::castle_door_template;
                   }),
            "checked-in tutorial bootstrap does not contain the official checkpoints");
        auto handoff_events = prepare_replay_phase(load_replay_phase(scenarios / "tutorial-cutscene-handoff.json"), 1841, 0);
        real.adapt_handoff(handoff_events, 0x08);
        const std::string token = "$A1b2C3d4E5f";
        require(real.character_token() == token
                && std::ranges::any_of(handoff_events, [&](const replay_event& event) {
                       return event.body.size() > 30 && event.body[0] == 0x33 && event.body[1] == 0x36
                           && std::search(event.body.begin(), event.body.end(), token.begin(), token.end()) != event.body.end();
                   }),
            "handoff character record does not carry the session's character token");
        require(std::ranges::none_of(handoff_events, [](const replay_event& event) {
                    return event.body.size() > 6 && event.body[0] == 0x2D && event.body[1] == 0x92
                        && !(event.body[2] == 0x8A && event.body[3] == 0x96);
                }),
            "handoff quest steps still use the recorded quest instance");
    }
}

// Plays the whole Elyos tutorial script on the checked-in data and scenarios.
void tutorial_full_run() {
    const std::filesystem::path scenarios = "scenarios";
    if (!std::filesystem::is_regular_file(scenarios / "tutorial-bootstrap.json")
        || !std::filesystem::is_directory("config/game-data")) {
        return;
    }
    const auto data = sema::data::load_game_data("config/game-data");
    tutorial_world world(data, 202300);
    tutorial_script script(load_replay_scenario(scenarios / "tutorial-bootstrap.json"),
        load_replay_phase(scenarios / "tutorial-room-spawns.json"), load_replay_phase(scenarios / "tutorial-exit.json"));
    std::map<tutorial_script::checkpoint, std::uint32_t> point;
    std::uint32_t castle_door = 0;
    for (const auto& environment : script.objects()) {
        if (const auto reached = script.checkpoint_of(environment.entity_id)) point[*reached] = environment.entity_id;
        if (environment.template_id == tutorial_script::castle_door_template) castle_door = environment.entity_id;
    }
    require(point.size() == 9 && castle_door != 0, "tutorial bootstrap lacks a checkpoint or the castle door");
    const auto reach = [&](const tutorial_script::checkpoint which) {
        return script.on_interaction({point[which], tutorial_script::arrival_point_template}, 1841, world, 1'000);
    };
    const auto contains = [](const std::vector<sema::gameplay_timed_message>& messages, const std::vector<std::uint8_t>& body) {
        return std::ranges::any_of(messages, [&](const auto& message) { return message.body == body; });
    };
    const auto starts = [](const std::vector<sema::gameplay_timed_message>& messages, const std::uint8_t a, const std::uint8_t b) {
        return std::ranges::any_of(messages, [&](const auto& message) {
            return message.body.size() > 1 && message.body[0] == a && message.body[1] == b;
        });
    };
    const auto step = [&](std::uint8_t chapter, std::uint8_t number, std::vector<std::uint8_t> flags, std::uint8_t status = 2) {
        return sema::encode_gameplay_quest_step(script.quest_instance(), chapter, number, flags, status);
    };
    const auto kill_all = [&] {
        std::vector<sema::gameplay_timed_message> progression;
        for (int hit = 0; hit < 20; ++hit) {
            const auto targets = world.apply_attack(sema::gameplay_skill_kind::primary_attack,
                sema::data::skill_hit_shape::radius, 20, 0, {-80000, 60000, 24000}, 100000, 0, 0);
            for (auto& message : script.on_kills(targets, 9867, 1841, world, 2'000)) progression.push_back(std::move(message));
        }
        return progression;
    };

    (void)reach(tutorial_script::checkpoint::movement);
    (void)reach(tutorial_script::checkpoint::light);
    (void)world.spawn_group(1, 1841, true, false);
    require(contains(kill_all(), step(2, 1, {0, 0})), "first encounter did not advance the quest");
    (void)reach(tutorial_script::checkpoint::corridor);
    (void)reach(tutorial_script::checkpoint::corridor_end);
    require(starts(reach(tutorial_script::checkpoint::encounter), 0x41, 0x36), "group 2 did not spawn");
    require(contains(kill_all(), step(2, 4, {0, 0})), "second encounter did not advance the quest");
    (void)reach(tutorial_script::checkpoint::room);
    require(starts(script.on_interaction({castle_door, tutorial_script::castle_door_template}, 1841, world, 1'000), 0x41, 0x36),
        "castle door did not spawn group 3");
    require(contains(reach(tutorial_script::checkpoint::finale), step(3, 1, {1, 0, 0, 0, 0, 0})), "finale mismatch");
    const auto h = reach(tutorial_script::checkpoint::room_a);
    require(contains(h, step(3, 1, {1, 1, 0, 0, 0, 0}))
            && contains(h, sema::encode_gameplay_tutorial_prompt("Tutorial_Basic_08", "15000")),
        "checkpoint H mismatch");
    // Break every tube from the middle of the room with a wide radius attack while the pack lives:
    // the captive walks free, the Balaur waits for the pack.
    const auto balaur_prompt = sema::encode_gameplay_talk_prompt("Talk_L_Starter_InstanceLayer_1_20", 0);
    const auto tubes = script.on_attack({-71500, 64500, 24003}, 0, 2'000, 0, false, 1841, world, 3'000);
    require(starts(tubes, 0x93, 0x8D) && contains(tubes, hex("2C38939F0101000201")) && !contains(tubes, balaur_prompt),
        "test tubes did not break, release the captive, or held the Balaur early");
    const auto cleared = kill_all();
    require(contains(cleared, step(3, 2, {1, 0, 0, 1})) && contains(cleared, step(3, 2, {1, 1, 0, 2}))
            && contains(cleared, balaur_prompt) && starts(cleared, 0x41, 0x36) && starts(cleared, 0x2A, 0x38),
        "the Balaur did not appear after the pack and every tube");
    require(contains(kill_all(), step(3, 3, {0})), "the Balaur did not open the last step");
    const auto end = reach(tutorial_script::checkpoint::room_b);
    require(contains(end, step(3, 3, {1}, 0x03)) && starts(end, 0x00, 0x61) && !starts(end, 0x26, 0x36),
        "the final checkpoint did not complete the tutorial");
    require(script.leave_if_overdue(1841, 2'900).empty(), "the exit fallback fired before its delay");
    const auto fallback = script.leave_if_overdue(1841, 3'100);
    require(contains(fallback, hex("4736B10E0A00")) && starts(fallback, 0x26, 0x36)
            && script.leave_if_overdue(1841, 7'000).empty(),
        "the exit fallback did not fire once");
    const auto exit = script.leave(1841, 8'000);
    require(contains(exit, hex("4736B10E0A00")) && starts(exit, 0x26, 0x36) && script.leave(1841, 9'000).empty()
            && exit.front().delay_milliseconds == tutorial_script::exit_answer_delay_milliseconds
            && starts(exit, 0x35, 0x36),
        "the leave request did not answer with the exit once");
}

int main() {
    try {
        std::cerr << "running frame_round_trip\n";
        frame_round_trip();
        std::cerr << "running coalesced_frames\n";
        coalesced_frames();
        std::cerr << "running handshake_layout\n";
        handshake_layout();
        std::cerr << "running lobby_server_layout\n";
        lobby_server_layout();
        std::cerr << "running gameplay_login_layout\n";
        gameplay_login_layout();
        std::cerr << "running game_data_catalogs_and_walk_mesh\n";
        game_data_catalogs_and_walk_mesh();
        std::cerr << "running loot_graph_and_world_indexes\n";
        loot_graph_and_world_indexes();
        std::cerr << "running tutorial_script_progression\n";
        tutorial_script_progression();
        std::cerr << "running tutorial_full_run\n";
        tutorial_full_run();
        std::cout << "Sema native protocol tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
