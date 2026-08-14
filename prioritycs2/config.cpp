#include "pch.h"

#include "config.hpp"
#include "settings.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace
{
    namespace fs = std::filesystem;

    fs::path directory;
    std::vector<std::string> profile_names;
    std::size_t current_profile{};
    std::string status_text;
    std::string saved_snapshot;
    ULONGLONG dirty_since{};
    bool ready{};

    std::string clean_name(std::string_view input)
    {
        std::string result;
        result.reserve(std::min<std::size_t>(input.size(), 32));
        for (const unsigned char character : input)
        {
            if (result.size() >= 32) break;
            if (std::isalnum(character) || character == '-' || character == '_')
                result.push_back(static_cast<char>(character));
        }
        return result;
    }

    fs::path profile_path(std::string_view name)
    {
        return directory / (std::string(name) + ".cfg");
    }

    std::string serialize()
    {
        std::ostringstream output;
        output << std::boolalpha << std::setprecision(9);
        const auto boolean = [&](const char* key, bool value) { output << key << '=' << value << '\n'; };
        const auto integer = [&](const char* key, int value) { output << key << '=' << value << '\n'; };
        const auto number = [&](const char* key, float value) { output << key << '=' << value << '\n'; };
        const auto color = [&](const char* key, const ImVec4& value)
        {
            output << key << '=' << value.x << ',' << value.y << ',' << value.z << ',' << value.w << '\n';
        };

        boolean("aim.enabled", settings::aim_enabled);
        boolean("aim.visible_only", settings::aim_visible_only);
        boolean("aim.teammates", settings::aim_teammates);
        boolean("aim.show_fov", settings::aim_show_fov);
        number("aim.fov", settings::aim_fov);
        number("aim.smooth", settings::aim_smooth);
        integer("aim.key", settings::aim_key);
        output << "aim.hitboxes=";
        for (std::size_t index = 0; index < settings::aim_hitboxes.size(); ++index)
            output << (settings::aim_hitboxes[index] ? '1' : '0') << (index + 1 == settings::aim_hitboxes.size() ? '\n' : ',');
        color("aim.fov_color", settings::aim_fov_color);

        boolean("trigger.enabled", settings::trigger_enabled);
        boolean("trigger.teammates", settings::trigger_teammates);
        boolean("trigger.revolver_hold", settings::trigger_revolver_hold);
        number("trigger.delay", settings::trigger_delay);
        number("trigger.min_damage", settings::trigger_min_damage);

        boolean("viewmodel.enabled", settings::viewmodel_enabled);
        number("viewmodel.x", settings::viewmodel_x);
        number("viewmodel.y", settings::viewmodel_y);
        number("viewmodel.z", settings::viewmodel_z);
        number("viewmodel.fov", settings::viewmodel_fov);

        boolean("esp.enabled", settings::esp_enabled);
        boolean("esp.box", settings::esp_box);
        boolean("esp.name", settings::esp_name);
        boolean("esp.health", settings::esp_health);
        boolean("esp.skeleton", settings::esp_skeleton);
        boolean("esp.offscreen", settings::esp_offscreen);
        boolean("esp.teammates", settings::esp_teammates);
        number("esp.distance", settings::esp_distance);
        color("esp.box_enemy", settings::enemy_color);
        color("esp.box_team", settings::team_color);
        color("esp.name_color", settings::esp_name_color);
        color("esp.health_high", settings::esp_health_high_color);
        color("esp.health_low", settings::esp_health_low_color);
        color("esp.skeleton_enemy", settings::esp_skeleton_enemy_color);
        color("esp.skeleton_team", settings::esp_skeleton_team_color);
        color("esp.arrow_enemy", settings::esp_arrow_enemy_color);
        color("esp.arrow_team", settings::esp_arrow_team_color);

        integer("menu.scale", settings::menu_scale_index);
        number("menu.opacity", settings::menu_opacity);
        color("menu.accent", settings::menu_accent);
        return output.str();
    }

    bool write_file(const fs::path& path, const std::string& data)
    {
        const fs::path temporary = path.wstring() + L".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) return false;
            output.write(data.data(), static_cast<std::streamsize>(data.size()));
            if (!output) return false;
        }
        if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return true;
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return false;
    }

    void remember_active()
    {
        if (profile_names.empty()) return;
        write_file(directory / "active.txt", profile_names[current_profile]);
    }

    void refresh(std::string_view prefer = {})
    {
        const std::string previous = prefer.empty() && current_profile < profile_names.size() ? profile_names[current_profile] : std::string(prefer);
        profile_names.clear();
        std::error_code error;
        for (const fs::directory_entry& entry : fs::directory_iterator(directory, error))
            if (entry.is_regular_file() && entry.path().extension() == ".cfg")
                profile_names.push_back(entry.path().stem().string());
        std::sort(profile_names.begin(), profile_names.end());
        current_profile = 0;
        const auto found = std::find(profile_names.begin(), profile_names.end(), previous);
        if (found != profile_names.end())
            current_profile = static_cast<std::size_t>(found - profile_names.begin());
    }

    std::unordered_map<std::string, std::string> parse(const fs::path& path)
    {
        std::unordered_map<std::string, std::string> result;
        std::ifstream input(path, std::ios::binary);
        std::string line;
        while (std::getline(input, line))
        {
            const std::size_t separator = line.find('=');
            if (separator != std::string::npos)
                result[line.substr(0, separator)] = line.substr(separator + 1);
        }
        return result;
    }

    void apply(const std::unordered_map<std::string, std::string>& values)
    {
        const auto boolean = [&](const char* key, bool& value)
        {
            if (const auto found = values.find(key); found != values.end()) value = found->second == "true" || found->second == "1";
        };
        const auto integer = [&](const char* key, int& value)
        {
            if (const auto found = values.find(key); found != values.end()) try { value = std::stoi(found->second); } catch (...) {}
        };
        const auto number = [&](const char* key, float& value)
        {
            if (const auto found = values.find(key); found != values.end()) try { value = std::stof(found->second); } catch (...) {}
        };
        const auto color = [&](const char* key, ImVec4& value)
        {
            const auto found = values.find(key);
            if (found == values.end()) return;
            std::istringstream input(found->second);
            char comma{};
            ImVec4 parsed{};
            if (input >> parsed.x >> comma >> parsed.y >> comma >> parsed.z >> comma >> parsed.w)
                value = parsed;
        };

        boolean("aim.enabled", settings::aim_enabled);
        boolean("aim.visible_only", settings::aim_visible_only);
        boolean("aim.teammates", settings::aim_teammates);
        boolean("aim.show_fov", settings::aim_show_fov);
        number("aim.fov", settings::aim_fov);
        number("aim.smooth", settings::aim_smooth);
        integer("aim.key", settings::aim_key);
        if (const auto found = values.find("aim.hitboxes"); found != values.end())
        {
            std::istringstream input(found->second);
            for (std::size_t index = 0; index < settings::aim_hitboxes.size(); ++index)
            {
                char value{}, comma{};
                if (!(input >> value)) break;
                settings::aim_hitboxes[index] = value == '1';
                input >> comma;
            }
        }
        color("aim.fov_color", settings::aim_fov_color);

        boolean("trigger.enabled", settings::trigger_enabled);
        boolean("trigger.teammates", settings::trigger_teammates);
        boolean("trigger.revolver_hold", settings::trigger_revolver_hold);
        number("trigger.delay", settings::trigger_delay);
        number("trigger.min_damage", settings::trigger_min_damage);

        boolean("viewmodel.enabled", settings::viewmodel_enabled);
        number("viewmodel.x", settings::viewmodel_x);
        number("viewmodel.y", settings::viewmodel_y);
        number("viewmodel.z", settings::viewmodel_z);
        number("viewmodel.fov", settings::viewmodel_fov);

        boolean("esp.enabled", settings::esp_enabled);
        boolean("esp.box", settings::esp_box);
        boolean("esp.name", settings::esp_name);
        boolean("esp.health", settings::esp_health);
        boolean("esp.skeleton", settings::esp_skeleton);
        boolean("esp.offscreen", settings::esp_offscreen);
        boolean("esp.teammates", settings::esp_teammates);
        number("esp.distance", settings::esp_distance);
        color("esp.box_enemy", settings::enemy_color);
        color("esp.box_team", settings::team_color);
        color("esp.name_color", settings::esp_name_color);
        color("esp.health_high", settings::esp_health_high_color);
        color("esp.health_low", settings::esp_health_low_color);
        color("esp.skeleton_enemy", settings::esp_skeleton_enemy_color);
        color("esp.skeleton_team", settings::esp_skeleton_team_color);
        color("esp.arrow_enemy", settings::esp_arrow_enemy_color);
        color("esp.arrow_team", settings::esp_arrow_team_color);
        integer("menu.scale", settings::menu_scale_index);
        number("menu.opacity", settings::menu_opacity);
        color("menu.accent", settings::menu_accent);

        settings::aim_key = std::clamp(settings::aim_key, 0, 3);
        settings::menu_scale_index = std::clamp(settings::menu_scale_index, 0, 3);
        settings::aim_fov = std::clamp(settings::aim_fov, 0.5f, 20.0f);
        settings::aim_smooth = std::clamp(settings::aim_smooth, 1.0f, 20.0f);
        settings::menu_opacity = std::clamp(settings::menu_opacity, 65.0f, 100.0f);
        settings::viewmodel_x = std::clamp(settings::viewmodel_x, -2.0f, 2.5f);
        settings::viewmodel_y = std::clamp(settings::viewmodel_y, -2.0f, 2.0f);
        settings::viewmodel_z = std::clamp(settings::viewmodel_z, -2.0f, 2.0f);
        settings::viewmodel_fov = std::clamp(settings::viewmodel_fov, 60.0f, 68.0f);
    }
}

namespace config
{
    bool initialize()
    {
        if (ready) return true;
        wchar_t app_data[32768]{};
        const DWORD length = GetEnvironmentVariableW(L"APPDATA", app_data, static_cast<DWORD>(std::size(app_data)));
        if (!length || length >= std::size(app_data)) return false;
        directory = fs::path(app_data) / "prioritycs2" / "configs";
        std::error_code error;
        fs::create_directories(directory, error);
        if (error) return false;

        refresh();
        if (profile_names.empty())
        {
            if (!write_file(profile_path("default"), serialize())) return false;
            refresh("default");
        }
        std::ifstream active_file(directory / "active.txt", std::ios::binary);
        std::string active;
        std::getline(active_file, active);
        const auto found = std::find(profile_names.begin(), profile_names.end(), clean_name(active));
        if (found != profile_names.end()) current_profile = static_cast<std::size_t>(found - profile_names.begin());
        ready = true;
        return load();
    }

    void tick()
    {
        if (!ready || profile_names.empty()) return;
        const std::string current = serialize();
        if (current == saved_snapshot) { dirty_since = 0; return; }
        const ULONGLONG now = GetTickCount64();
        if (!dirty_since) dirty_since = now;
        if (now - dirty_since >= 750) save();
    }

    void shutdown()
    {
        if (ready && !profile_names.empty() && serialize() != saved_snapshot) save();
    }

    const std::vector<std::string>& profiles() { return profile_names; }
    std::size_t selected_index() { return current_profile; }

    void select(std::size_t index)
    {
        if (index >= profile_names.size()) return;
        current_profile = index;
        load();
    }

    bool create(std::string_view name)
    {
        const std::string cleaned = clean_name(name);
        if (cleaned.empty()) { status_text = "Invalid profile name"; return false; }
        if (!write_file(profile_path(cleaned), serialize())) { status_text = "Create failed"; return false; }
        refresh(cleaned);
        remember_active();
        saved_snapshot = serialize();
        dirty_since = 0;
        status_text = "Created " + cleaned;
        return true;
    }

    bool save()
    {
        if (profile_names.empty()) return false;
        const std::string snapshot = serialize();
        if (!write_file(profile_path(profile_names[current_profile]), snapshot)) { status_text = "Save failed"; return false; }
        saved_snapshot = snapshot;
        dirty_since = 0;
        remember_active();
        status_text = "Saved " + profile_names[current_profile];
        return true;
    }

    bool load()
    {
        if (profile_names.empty()) return false;
        const fs::path path = profile_path(profile_names[current_profile]);
        std::ifstream check(path, std::ios::binary);
        if (!check) { status_text = "Load failed"; return false; }
        check.close();
        apply(parse(path));
        saved_snapshot = serialize();
        dirty_since = 0;
        remember_active();
        status_text = "Loaded " + profile_names[current_profile];
        return true;
    }

    bool remove()
    {
        if (profile_names.empty()) return false;
        const std::string removed = profile_names[current_profile];
        std::error_code error;
        fs::remove(profile_path(removed), error);
        if (error) { status_text = "Delete failed"; return false; }
        refresh();
        if (profile_names.empty())
        {
            write_file(profile_path("default"), serialize());
            refresh("default");
        }
        load();
        status_text = "Deleted " + removed;
        return true;
    }

    const std::string& status() { return status_text; }
}
