#pragma once

#include "plugin_interface.h"
#include <string>
#include <vector>
#include <windows.h>

#include "framework_export.h"

namespace GamePlug {

struct LoadedPlugin {
    std::string filename;
    std::string name;
    HMODULE handle;
    GamePlugPluginInterface* pInterface;

    LoadedPlugin()
        : handle(NULL)
        , pInterface(nullptr) {}
};

struct DiscoveredPlugin {
    std::string filename;
    std::string name;
    bool isLoaded;
};

class FRAMEWORK_API PluginManager {
public:
    static PluginManager& Get();

    // Call once during engine/overlay initialization.
    // Searches for plugins in (ExeDir)/GamePlug/plugins.
    void LoadPlugins();

    // Call during overlay rendering.
    void RenderPlugins();

    bool IsEmpty() const;
    const std::vector<DiscoveredPlugin>& GetDiscoveredPlugins() const;

    void LoadIndividualPlugin(DiscoveredPlugin& dp);
    void UnloadIndividualPlugin(const std::string& name);

    // Call during shutdown.
    void UnloadPlugins();

private:
    PluginManager() = default;
    ~PluginManager();

    std::vector<LoadedPlugin> m_plugins;
    std::vector<DiscoveredPlugin> m_discoveredPlugins;
    bool m_searchDone = false;
};

} // namespace GamePlug
