#include "config.h"
#include "imgui.h"
#include "logger.h"
#include "plugin_manager.h"
#include "win32_hooks.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <windows.h>

namespace GamePlug {

Config& Config::Get() {
    static Config instance;
    return instance;
}

void Config::Load(const std::string& filename) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_lastFilename = filename;
    m_settings.clear();

    // Get the path of the current module to load config from the same directory
    char buf[MAX_PATH];
    GetModuleFileNameA(NULL, buf, MAX_PATH);
    std::string path = std::string(buf);
    size_t lastSlash = path.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        path = path.substr(0, lastSlash + 1) + m_lastFilename;
    } else {
        path = m_lastFilename;
    }

    std::ifstream f(path);
    if (!f.is_open()) {
        Logger::warn("Config: Could not open " + path + " for reading.");
        return;
    }

    std::string line;
    while (std::getline(f, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;

        size_t pos = line.find('=');
        if (pos == std::string::npos)
            continue;

        std::string key = Trim(line.substr(0, pos));
        std::string value = Trim(line.substr(pos + 1));
        m_settings[key] = value;

        if (key == "ScreenResolution") {
            size_t xPos = value.find('x');
            if (xPos != std::string::npos) {
                m_targetWidth = (uint32_t)std::stoul(value.substr(0, xPos));
                m_targetHeight = (uint32_t)std::stoul(value.substr(xPos + 1));
                Logger::info("Config: ScreenResolution detected: " + std::to_string(m_targetWidth) + "x" + std::to_string(m_targetHeight));
            }
        }
        if (key == "ExtraEnumeratedResolutions") {
            m_extraResolutions.clear();
            std::stringstream ss(value);
            std::string item;
            while (std::getline(ss, item, ',')) {
                item = Trim(item);
                size_t xPos = item.find('x');
                if (xPos != std::string::npos) {
                    try {
                        Resolution res;
                        res.width = (uint32_t)std::stoul(item.substr(0, xPos));
                        res.height = (uint32_t)std::stoul(item.substr(xPos + 1));
                        m_extraResolutions.push_back(res);
                        Logger::info("Config: Extra resolution added: " + std::to_string(res.width) + "x" + std::to_string(res.height));
                    } catch (...) {
                    }
                }
            }
        }
    }

    auto it = m_settings.find("EnabledEnumeratedResolutions");
    bool enabled = false;
    if (it != m_settings.end()) {
        enabled = (it->second == "true" || it->second == "1" || it->second == "on");
    }
    if (!enabled) {
        m_extraResolutions.clear();
    }

    Logger::info("Config: Loaded " + std::to_string(m_settings.size()) + " settings from " + path);

#ifndef GAMEPLUG_DIRECTX
    InstallWin32Hooks();
#else
    Logger::info("Config: Skipping Win32 hooks for D3D12 backend");
#endif
}

void Config::Save(const std::string& filename) {
    std::lock_guard<std::mutex> lock(m_mtx);

    char buf[MAX_PATH];
    GetModuleFileNameA(NULL, buf, MAX_PATH);
    std::string path = std::string(buf);
    size_t lastSlash = path.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        path = path.substr(0, lastSlash + 1) + filename;
    } else {
        path = filename;
    }

    std::ofstream f(path);
    if (!f.is_open()) {
        Logger::error("Config: Could not open " + path + " for writing.");
        return;
    }

    for (const auto& [key, value] : m_settings) {
        f << key << "=" << value << "\n";
    }

    Logger::info("Config: Saved settings to " + path);
}

bool Config::GetBool(const std::string& key, bool defaultValue) {
    if (key == "PluginEnabled")
        return true;
    std::lock_guard<std::mutex> lock(m_mtx);
    auto it = m_settings.find(key);
    if (it == m_settings.end())
        return defaultValue;
    return (it->second == "true" || it->second == "1" || it->second == "on");
}

int Config::GetInt(const std::string& key, int defaultValue) {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto it = m_settings.find(key);
    if (it == m_settings.end())
        return defaultValue;
    try {
        return std::stoi(it->second);
    } catch (...) {
        return defaultValue;
    }
}

float Config::GetFloat(const std::string& key, float defaultValue) {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto it = m_settings.find(key);
    if (it == m_settings.end())
        return defaultValue;
    try {
        return std::stof(it->second);
    } catch (...) {
        return defaultValue;
    }
}

std::string Config::GetString(const std::string& key, const std::string& defaultValue) {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto it = m_settings.find(key);
    if (it == m_settings.end())
        return defaultValue;
    return it->second;
}

void Config::SetBool(const std::string& key, bool value) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_settings[key] = value ? "true" : "false";
}

void Config::SetInt(const std::string& key, int value) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_settings[key] = std::to_string(value);
}

void Config::SetFloat(const std::string& key, float value) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_settings[key] = std::to_string(value);
}

void Config::SetString(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_settings[key] = value;
}

bool Config::IsPluginEnabled(const std::string& pluginName) {
    return GetBool("Plugin_" + pluginName + "_Enabled", true);
}

void Config::SetPluginEnabled(const std::string& pluginName, bool enabled) {
    SetBool("Plugin_" + pluginName + "_Enabled", enabled);
}

std::string Config::Trim(const std::string& s) {
    std::string res = s;
    res.erase(res.begin(), std::find_if(res.begin(), res.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    res.erase(std::find_if(res.rbegin(), res.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), res.end());
    return res;
}

void Config::RenderUI(bool showResolutionEnumeration) {
#ifdef GAMEPLUG_DIRECTX
    showResolutionEnumeration = false;
#endif
    if (showResolutionEnumeration) {
        // Extra Resolutions
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        bool enabled = GetBool("EnabledEnumeratedResolutions", false);
        if (ImGui::Checkbox("Enable Resolution Enums", &enabled)) {
            SetBool("EnabledEnumeratedResolutions", enabled);
            Save();
            Load();
        }

        if (enabled) {
            // Parse current state from config
            std::string currentResolutions = GetString("ExtraEnumeratedResolutions");
            std::vector<std::string> resList;
            {
                std::stringstream ss(currentResolutions);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    std::string trimmed = Trim(item);
                    if (!trimmed.empty()) {
                        resList.push_back(trimmed);
                    }
                }
            }

            bool check1440 = false;
            bool check2160 = false;

            for (const auto& res : resList) {
                if (res == "2560x1440") {
                    check1440 = true;
                } else if (res == "3840x2160") {
                    check2160 = true;
                }
            }

            if (!m_resInit) {
                strncpy(m_resBuffer, currentResolutions.c_str(), sizeof(m_resBuffer) - 1);
                m_resInit = true;
            }

            // Render Checkboxes
            bool checkChanged = false;

            if (ImGui::Checkbox("2560x1440", &check1440)) {
                checkChanged = true;
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("3840x2160", &check2160)) {
                checkChanged = true;
            }

            auto UpdateFromCheckboxes = [&](bool c1440, bool c2160) {
                std::vector<std::string> list;
                std::stringstream ss(m_resBuffer);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    std::string trimmed = Trim(item);
                    if (!trimmed.empty()) {
                        list.push_back(trimmed);
                    }
                }

                auto it1440 = std::find(list.begin(), list.end(), "2560x1440");
                if (c1440) {
                    if (it1440 == list.end()) {
                        list.insert(list.begin(), "2560x1440");
                    }
                } else {
                    if (it1440 != list.end()) {
                        list.erase(it1440);
                    }
                }

                auto it2160 = std::find(list.begin(), list.end(), "3840x2160");
                if (c2160) {
                    if (it2160 == list.end()) {
                        auto itInsert = std::find(list.begin(), list.end(), "2560x1440");
                        if (itInsert != list.end()) {
                            list.insert(itInsert + 1, "3840x2160");
                        } else {
                            list.insert(list.begin(), "3840x2160");
                        }
                    }
                } else {
                    if (it2160 != list.end()) {
                        list.erase(it2160);
                    }
                }

                std::string newStr = "";
                for (size_t i = 0; i < list.size(); i++) {
                    newStr += list[i];
                    if (i + 1 < list.size()) {
                        newStr += ", ";
                    }
                }
                strncpy(m_resBuffer, newStr.c_str(), sizeof(m_resBuffer) - 1);
                SetString("ExtraEnumeratedResolutions", newStr);
                Save();
                Load();
            };

            if (checkChanged) {
                UpdateFromCheckboxes(check1440, check2160);
            }

            if (ImGui::InputTextWithHint("Resolution Enums", "2560x1440, 3840x2160", m_resBuffer, sizeof(m_resBuffer))) {
                SetString("ExtraEnumeratedResolutions", m_resBuffer);
            }

            if (ImGui::IsItemDeactivatedAfterEdit()) {
                Save();
                Load();
            }

            ImGui::TextDisabled("(Restart required).");
        }
    }
}

} // namespace GamePlug
