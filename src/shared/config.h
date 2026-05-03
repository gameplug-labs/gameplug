#pragma once
#include <string>
#include <map>
#include <mutex>
#include "framework_export.h"

namespace GamePlug {

class FRAMEWORK_API Config {
public:
    static Config& Get();

    void Load(const std::string& filename = "GamePlug.conf");
    void Save(const std::string& filename = "GamePlug.conf");

    void RenderUI();

    // General accessors
    bool GetBool(const std::string& key, bool defaultValue = false);
    int GetInt(const std::string& key, int defaultValue = 0);
    float GetFloat(const std::string& key, float defaultValue = 0.0f);
    std::string GetString(const std::string& key, const std::string& defaultValue = "");

    void SetBool(const std::string& key, bool value);
    void SetInt(const std::string& key, int value);
    void SetFloat(const std::string& key, float value);
    void SetString(const std::string& key, const std::string& value);
    
    uint32_t GetTargetWidth() const { return m_targetWidth; }
    uint32_t GetTargetHeight() const { return m_targetHeight; }

    // Plugin specific
    bool IsPluginEnabled(const std::string& pluginName);
    void SetPluginEnabled(const std::string& pluginName, bool enabled);

private:
    Config() = default;
    ~Config() = default;

    std::map<std::string, std::string> m_settings;
    std::mutex m_mtx;
    std::string m_lastFilename = "GamePlug.conf";
    
    uint32_t m_targetWidth = 0;
    uint32_t m_targetHeight = 0;

    std::string Trim(const std::string& s);
};

} // namespace GamePlug
