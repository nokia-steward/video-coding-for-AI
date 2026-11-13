// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <array>
#include <map>
#include <string_view>
#include <utility>

/**
 * @file sfu_config.height
 * @brief Defines configuration data structures and mappings for SFU video sequences.
 */

/**
 * @brief Maps sequence names to (class, canonical sequence name) pairs.
 */
const std::map<std::string_view, std::pair<char, std::string_view>> seq_dict = {
        {"Traffic", {'A', "Traffic_2560x1600_30"}},      {"ParkScene", {'B', "ParkScene_1920x1080_24"}},
        {"Cactus", {'B', "Cactus_1920x1080_50"}},        {"BasketballDrive", {'B', "BasketballDrive_1920x1080_50"}},
        {"BQTerrace", {'B', "BQTerrace_1920x1080_60"}},  {"BasketballDrill", {'C', "BasketballDrill_832x480_50"}},
        {"BQMall", {'C', "BQMall_832x480_60"}},          {"PartyScene", {'C', "PartyScene_832x480_50"}},
        {"RaceHorsesC", {'C', "RaceHorses_832x480_30"}}, {"BasketballPass", {'D', "BasketballPass_416x240_50"}},
        {"BQSquare", {'D', "BQSquare_416x240_60"}},      {"BlowingBubbles", {'D', "BlowingBubbles_416x240_50"}},
        {"RaceHorsesD", {'D', "RaceHorses_416x240_30"}}, {"Kimono", {'O', "Kimono_1920x1080_24"}},
};

/**
 * @brief Maps sequence class letters to (width, height) resolution pairs.
 */
const std::map<char, std::pair<int, int>> res_dict = {
        {'A', {2560, 1600}}, {'B', {1920, 1080}}, {'C', {832, 480}},
        {'D', {416, 240}},   {'E', {1280, 720}},  {'O', {1920, 1080}},
};

/**
 * @brief Frame rate settings for a sequence.
 */
struct frame_rate_info {
    int intra_period;
    int frame_rate;
    int frames_to_be_encoded;
    int frame_skip;
};

/**
 * @brief Maps sequence names to frame rate configuration data.
 */
const std::map<std::string_view, frame_rate_info> fr_dict = {
        {"Traffic", {32, 30, 33, 117}},     {"ParkScene", {32, 24, 33, 207}},
        {"Cactus", {64, 50, 97, 403}},      {"BasketballDrive", {64, 50, 97, 403}},
        {"BQTerrace", {64, 60, 129, 471}},  {"BasketballDrill", {64, 50, 97, 403}},
        {"BQMall", {64, 60, 129, 471}},     {"PartyScene", {64, 50, 97, 403}},
        {"RaceHorsesC", {32, 30, 65, 235}}, {"BasketballPass", {64, 50, 97, 403}},
        {"BQSquare", {64, 60, 129, 471}},   {"BlowingBubbles", {64, 50, 97, 403}},
        {"RaceHorsesD", {32, 30, 65, 235}}, {"Kimono", {32, 24, 33, 207}},
};

/** @brief A pair representing a QP value and an offset. */
using qp_offset_pair = std::pair<int, int>;

/** @brief A fixed-size array of QP configurations. */
using qp_settings = std::array<qp_offset_pair, 6>;

/**
 * @brief Builds a fixed-size list of QP/offset pairs.
 * @param start_qp Starting QP value.
 * @param interval Increment between QP entries.
 * @param nn_intra_qp_offset Offset for intra QP in neural network mode.
 * @return A fixed-size array of QP/offset pairs.
 */
constexpr auto get_qp_list(const size_t start_qp, const size_t interval, const int nn_intra_qp_offset = -5) {
    qp_settings qp_list{};
    for (size_t i = 0; i < 6; ++i) {
        qp_list[i] = {static_cast<int>(start_qp + i * interval), nn_intra_qp_offset};
    }
    return qp_list;
}

/**
 * @brief Default QP settings per sequence.
 */
const std::map<std::string_view, qp_settings> seq_cfg = {
        {"Traffic", get_qp_list(35, 4)},     {"ParkScene", get_qp_list(30, 5)},
        {"Cactus", get_qp_list(44, 2)},      {"BasketballDrive", get_qp_list(38, 3)},
        {"BQTerrace", get_qp_list(38, 3)},   {"BasketballDrill", get_qp_list(22, 4)},
        {"BQMall", get_qp_list(27, 5)},      {"PartyScene", get_qp_list(32, 4)},
        {"RaceHorsesC", get_qp_list(27, 4)}, {"BasketballPass", get_qp_list(22, 4)},
        {"BQSquare", get_qp_list(22, 4)},    {"BlowingBubbles", get_qp_list(22, 4)},
        {"RaceHorsesD", get_qp_list(27, 5)}, {"Kimono", get_qp_list(32, 5)},
};

/**
 * @brief AI-oriented QP settings per sequence.
 */
const std::map<std::string_view, qp_settings> seq_cfg_ai = {
        {"Traffic", get_qp_list(32, 5, 0)},     {"ParkScene", get_qp_list(22, 5, 0)},
        {"Cactus", get_qp_list(22, 5, 0)},      {"BasketballDrive", get_qp_list(22, 5, 0)},
        {"BQTerrace", get_qp_list(22, 5, 0)},   {"BasketballDrill", get_qp_list(22, 5, 0)},
        {"BQMall", get_qp_list(22, 5, 0)},      {"PartyScene", get_qp_list(22, 5, 0)},
        {"RaceHorsesC", get_qp_list(22, 5, 0)}, {"BasketballPass", get_qp_list(22, 5, 0)},
        {"BQSquare", get_qp_list(22, 5, 0)},    {"BlowingBubbles", get_qp_list(22, 5, 0)},
        {"RaceHorsesD", get_qp_list(22, 5, 0)}, {"Kimono", get_qp_list(32, 5, 0)},
};

/**
 * @brief Default network name for ROI configuration.
 */
constexpr std::string_view seq_roi_cfg_network = "faster_rcnn_X_101_32x8d_FPN_3x";
