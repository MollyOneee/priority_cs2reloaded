#pragma once

#include <array>
#include <cstdint>

namespace skin_catalog
{
    struct weapon
    {
        std::uint16_t definition;
        const char* name;
    };

    struct paint_kit
    {
        std::uint16_t weapon_definition;
        int id;
        const char* name;
        int rarity;
    };

    inline constexpr std::array weapons{
        weapon{ 7, "AK-47" }, weapon{ 60, "M4A1-S" }, weapon{ 16, "M4A4" },
        weapon{ 9, "AWP" }, weapon{ 1, "Desert Eagle" }, weapon{ 4, "Glock-18" },
        weapon{ 61, "USP-S" }, weapon{ 64, "R8 Revolver" }, weapon{ 40, "SSG 08" },
        weapon{ 36, "P250" }, weapon{ 34, "MP9" }, weapon{ 17, "MAC-10" },
        weapon{ 10, "FAMAS" }, weapon{ 13, "Galil AR" }
    };

    // Curated, weapon-compatible paint kits. Rarity follows the game's 0..7 scale.
    inline constexpr std::array paint_kits{
        paint_kit{ 7, 302, "Vulcan", 6 }, paint_kit{ 7, 282, "Redline", 5 },
        paint_kit{ 7, 707, "Neon Rider", 6 }, paint_kit{ 7, 801, "Asiimov", 6 },
        paint_kit{ 7, 639, "Bloodsport", 6 },
        paint_kit{ 60, 984, "Printstream", 6 }, paint_kit{ 60, 430, "Hyper Beast", 6 },
        paint_kit{ 60, 946, "Player Two", 6 }, paint_kit{ 60, 1017, "Blue Phosphor", 5 },
        paint_kit{ 60, 1161, "Night Terror", 4 },
        paint_kit{ 16, 309, "Howl", 7 }, paint_kit{ 16, 255, "Asiimov", 6 },
        paint_kit{ 16, 588, "Desolate Space", 5 }, paint_kit{ 16, 449, "Poseidon", 5 },
        paint_kit{ 16, 1239, "Chrome Cannon", 6 },
        paint_kit{ 9, 344, "Dragon Lore", 6 }, paint_kit{ 9, 279, "Asiimov", 6 },
        paint_kit{ 9, 446, "Medusa", 6 }, paint_kit{ 9, 756, "Gungnir", 6 },
        paint_kit{ 9, 819, "Desert Hydra", 6 },
        paint_kit{ 1, 37, "Blaze", 5 }, paint_kit{ 1, 711, "Code Red", 6 },
        paint_kit{ 1, 962, "Printstream", 6 }, paint_kit{ 1, 1090, "Ocean Drive", 6 },
        paint_kit{ 1, 527, "Kumicho Dragon", 5 },
        paint_kit{ 4, 38, "Fade", 5 }, paint_kit{ 4, 353, "Water Elemental", 5 },
        paint_kit{ 4, 957, "Bullet Queen", 6 }, paint_kit{ 4, 680, "Off World", 3 },
        paint_kit{ 4, 1032, "Snack Attack", 5 },
        paint_kit{ 61, 504, "Kill Confirmed", 6 }, paint_kit{ 61, 653, "Neo-Noir", 6 },
        paint_kit{ 61, 705, "Cortex", 5 }, paint_kit{ 61, 1142, "Printstream", 6 },
        paint_kit{ 61, 1065, "Whiteout", 5 },
        paint_kit{ 64, 522, "Fade", 5 }, paint_kit{ 64, 12, "Crimson Web", 4 },
        paint_kit{ 64, 781, "Skull Crusher", 5 }, paint_kit{ 64, 1145, "Crazy 8", 5 },
        paint_kit{ 64, 683, "Llama Cannon", 5 },
        paint_kit{ 40, 624, "Dragonfire", 6 }, paint_kit{ 40, 222, "Blood in the Water", 6 },
        paint_kit{ 40, 503, "Big Iron", 5 }, paint_kit{ 40, 1101, "Turbo Peek", 5 },
        paint_kit{ 40, 1052, "Death Strike", 5 },
        paint_kit{ 36, 551, "Asiimov", 5 }, paint_kit{ 36, 678, "See Ya Later", 6 },
        paint_kit{ 36, 1021, "Bengal Tiger", 3 }, paint_kit{ 36, 1141, "Visions", 5 },
        paint_kit{ 36, 34, "Metallic DDPAT", 2 },
        paint_kit{ 34, 1134, "Starlight Protector", 6 }, paint_kit{ 34, 1094, "Mount Fuji", 4 },
        paint_kit{ 34, 1037, "Food Chain", 5 }, paint_kit{ 34, 910, "Hydra", 5 },
        paint_kit{ 34, 39, "Bulldozer", 4 },
        paint_kit{ 17, 433, "Neon Rider", 6 }, paint_kit{ 17, 947, "Disco Tech", 5 },
        paint_kit{ 17, 898, "Stalker", 6 }, paint_kit{ 17, 1156, "Toybox", 5 },
        paint_kit{ 17, 651, "Last Dive", 4 },
        paint_kit{ 10, 919, "Commemoration", 6 }, paint_kit{ 10, 626, "Mecha Industries", 5 },
        paint_kit{ 10, 604, "Roll Cage", 6 }, paint_kit{ 10, 723, "Eye of Athena", 5 },
        paint_kit{ 10, 260, "Pulse", 5 },
        paint_kit{ 13, 398, "Chatterbox", 6 }, paint_kit{ 13, 428, "Eco", 5 },
        paint_kit{ 13, 661, "Sugar Rush", 5 }, paint_kit{ 13, 1038, "Chromatic Aberration", 5 },
        paint_kit{ 13, 379, "Cerberus", 4 }
    };
}
