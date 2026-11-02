#pragma once
// The sidecar's deliberately small, bounded text protocol. No game dependencies.
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <limits>
#include <string>
#include <vector>

namespace probe {
struct Command {
    uint64_t id = 0;
    std::string name;
    std::vector<std::string> args;
};
inline bool Number(const std::string &text, double &value) {
    char *end = nullptr;
    errno = 0;
    value = std::strtod(text.c_str(), &end);
    return !text.empty() && end == text.c_str() + text.size() && !errno && std::isfinite(value);
}
inline bool Range(const std::string &text, double low, double high, bool integer = false) {
    double value;
    return Number(text, value) && value >= low && value <= high && (!integer || value == std::floor(value));
}
// Runtime grade readiness is an initialization capability, not a config toggle.
// Kept here so the actual command mutation policy can be tested without macVC.
inline bool SetGradeReady(uint8_t *readiness, bool initiallyReady, uint8_t requested, std::string &error) {
    if (!readiness) { error = "unavailable: build plugin UUID does not match generated symbol offsets"; return false; }
    if (requested && !initiallyReady) {
        error = "refused: grading was not initialized at plugin startup; restart with ModernLighting=1";
        return false;
    }
    *readiness = requested;
    return true;
}
inline bool CanSpawnPed(const uint8_t *flags, int capacity, std::string &error) {
    if (!flags || capacity <= 0 || capacity > 8192) {
        error = "refused: invalid native ped-pool capacity/flags";
        return false;
    }
    int free = 0;
    for (int i = 0; i < capacity; ++i)
        if ((flags[i] & 0x80) && ++free >= 4) return true;
    error = "refused: fixture requires at least four free native ped-pool slots";
    return false;
}
inline bool Parse(const std::string &text, Command &out, std::string &error) {
    if (text.empty() || text.size() > 1024 || text.find('\0') != std::string::npos) {
        error = "empty, binary, or oversized command"; return false;
    }
    std::istringstream input(text);
    std::string id;
    if (!(input >> id >> out.name) || id.size() > 19 || id.find_first_not_of("0123456789") != std::string::npos) {
        error = "expected positive integer request id then command"; return false;
    }
    char *end = nullptr;
    errno = 0;
    out.id = std::strtoull(id.c_str(), &end, 10);
    if (errno || !out.id) { error = "invalid request id"; return false; }
    std::string arg;
    while (input >> arg) {
        if (arg.size() > 64 || out.args.size() == 8) { error = "too many or oversized arguments"; return false; }
        out.args.push_back(arg);
    }
    const auto &a = out.args;
    auto count = [&](size_t n) { return a.size() == n; };
    auto xyz = [&](size_t i) {
        return a.size() >= i + 3 && Range(a[i], -2000, 2000) && Range(a[i+1], -2000, 2000) && Range(a[i+2], -100, 1200);
    };
    bool ok = false;
    if (out.name == "status" || out.name == "restore" || out.name == "remove" || out.name == "npcpose") ok = count(0);
    else if (out.name == "clock") ok = count(3) && Range(a[0], 0, 23, true) && Range(a[1], 0, 59, true) && Range(a[2], 0, 1, true);
    else if (out.name == "weather") ok = count(1) && Range(a[0], -1, 5, true);
    else if (out.name == "window") ok = (count(1) && a[0]=="status") || (count(2) && a[0]=="native" && Range(a[1],0,1,true));
    else if (out.name == "rt" || out.name == "grade" || out.name == "clouds") ok = count(1) && Range(a[0], 0, 1, true);
    else if (out.name == "lightmap" || out.name == "gloss" || out.name == "rimlight") ok = (count(1)||count(2)) && Range(a[0],0,1,true) && (count(1)||Range(a[1],0,8));
    else if (out.name == "wanted") ok = count(1) && Range(a[0], 0, 6, true);
    else if (out.name == "weapon") ok = count(1) && Range(a[0], 0, 36, true);
    else if (out.name == "playerhit") ok = count(1) && Range(a[0], std::numeric_limits<double>::min(), 10);
    else if (out.name == "carroll") ok = count(1) && Range(a[0], 0, 1, true);
    else if (out.name == "npcshot") ok = count(4) && (a[0]=="foot_l" || a[0]=="foot_r" || a[0]=="hand_l" || a[0]=="chest" || a[0]=="head") && xyz(1);
    else if (out.name == "filter") ok = (count(1)||count(2)) && Range(a[0], 0, 3, true) && (count(1)||Range(a[1],0,2));
    // On-foot physics regression visits at most100m beyond the actual sector
    // grid; NPC/camera/shot fixtures retain their original narrower bounds.
    else if (out.name == "teleport") ok = count(3) && Range(a[0], -2500, 1700) && Range(a[1], -2100, 2100) && Range(a[2], -100, 1200);
    else if (out.name == "npcmove" || out.name == "npcrun" || out.name == "spawn" || out.name == "ground") ok = count(3) && xyz(0);
    else if (out.name == "line") ok = count(6) && xyz(0) && xyz(3);
    else if (out.name == "camera") ok = (count(1) && a[0] == "restore") || (count(6) && xyz(0) && xyz(3));
    else if (out.name == "damage") ok = count(4) && (Range(a[0], 0, 36, true) || Range(a[0], 39, 44, true)) && Range(a[1], 0.01, 1000) && Range(a[2], 0, 6, true) && Range(a[3], 0, 3, true);
    else if (out.name == "pilotdamage") ok = count(5) && Range(a[0], 0, 1, true) && (Range(a[1], 0, 36, true) || Range(a[1], 39, 44, true)) && Range(a[2], 0.01, 1000) && Range(a[3], 0, 6, true) && Range(a[4], 0, 3, true);
    else if (out.name == "helishot") ok = count(2) && Range(a[0], 0, 1, true) && (a[1]=="lamp" || a[1]=="tail" || a[1]=="mast" || a[1]=="hull" || a[1]=="pilot");
    else if (out.name == "frames") ok = (count(1) && a[0] == "stop") || (count(2) && a[0] == "start" && !a[1].empty() && a[1].find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == std::string::npos);
    if (!ok) error = "unknown command or invalid arguments (see tools/runtime/README.md)";
    return ok;
}
} // namespace probe
