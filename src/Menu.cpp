#include "Menu.hpp"

#include "Config.hpp"

#include <pl/ModMenu.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace dof {
namespace {

constexpr std::string_view kModule = "depth_of_field";
constexpr std::string_view kOwnerModId = "Depth of Field";

using SetSchemaFn = bool (*)(std::string_view, std::string_view);
using SetEnabledFn = void (*)(std::string_view, bool);
SetSchemaFn gSetSchema{};
SetEnabledFn gSetEnabled{};
std::atomic_bool gRegistered{false};

std::string fstr(float v) {
    char buf[48]{};
    std::snprintf(buf, sizeof(buf), "%.2f", double(v));
    return buf;
}
std::string bstr(bool v) { return v ? "true" : "false"; }

float parseFloat(std::string_view v, float fallback, float lo, float hi) {
    const std::string s(v);
    char* end = nullptr;
    const float x = std::strtof(s.c_str(), &end);
    if (!(end && end != s.c_str()) || !std::isfinite(x)) return fallback;
    return std::clamp(x, lo, hi);
}
bool parseBool(std::string_view v, bool fallback) {
    if (v == "true" || v == "1" || v == "on" || v == "yes") return true;
    if (v == "false" || v == "0" || v == "off" || v == "no") return false;
    return fallback;
}

std::string jsonQuote(std::string_view value) {
    std::string out = "\"";
    for (const char c : value) {
        if (c == '"' || c == '\\') out += '\\';
        out += static_cast<unsigned char>(c) < 0x20 ? ' ' : c;
    }
    return out + '"';
}

std::string conditions(std::string_view key) {
    if (key == "focus_distance") return R"(,"enabled_when":[{"key":"auto_focus","op":"falsy","value":""}])";
    if (key == "focus_speed") return R"(,"enabled_when":[{"key":"auto_focus","op":"truthy","value":""}])";
    return {};
}

struct MenuNode {
    std::string_view section;
    std::string json;
};

MenuNode node(std::string_view key, std::string_view title, std::string_view description, std::string_view category,
              std::string_view section, std::string_view type, std::string_view def = {}, std::string_view current = {},
              std::string_view extra = {}) {
    return {section, "{\"id\":" + jsonQuote(key) + ",\"key\":" + jsonQuote(key) + ",\"category\":" + jsonQuote(category) + ",\"title\":" + jsonQuote(title) +
                         ",\"description\":" + jsonQuote(description) + ",\"section\":" + jsonQuote(section) + ",\"type\":" + jsonQuote(type) +
                         ",\"default_value\":" + jsonQuote(def) + ",\"current_value\":" + jsonQuote(current) + ",\"action_value\":\"true\"" +
                         std::string(extra) + conditions(key) + '}'};
}

MenuNode integerNode(const IntSetting& setting, int current) {
    std::string options;
    auto option = [&](int value, const std::string& label) {
        if (!options.empty()) options += ',';
        options += "{\"value\":" + jsonQuote(std::to_string(value)) + ",\"label\":" + jsonQuote(label) + '}';
    };
    if (setting.value == &RuntimeConfig::downscale) {
        option(1, "Full size: sharpest");
        option(2, "Half size: recommended");
        option(3, "Third size");
        option(4, "Quarter size: fastest");
    }
    const std::string extra = options.empty()
        ? ",\"min_value\":" + jsonQuote(std::to_string(setting.minimum)) + ",\"max_value\":" + jsonQuote(std::to_string(setting.maximum)) +
              ",\"step\":" + jsonQuote(std::to_string(setting.step))
        : ",\"style\":\"dropdown\",\"options\":[" + options + ']';
    return node(setting.key, setting.title, setting.description, setting.category, setting.section, options.empty() ? "slider_int" : "choice",
                std::to_string(setting.initial), std::to_string(current), extra);
}

std::string schemaJson() {
    const auto& c = config();
    std::string s =
        R"({"version":2,"default_category":"presets","categories":[)"
        R"({"id":"presets","title":"Presets","description":"Choose a starting point"},)"
        R"({"id":"focus","title":"Focus","description":"What stays sharp"},)"
        R"({"id":"blur","title":"Blur","description":"How the blur looks"},)"
        R"({"id":"performance","title":"Performance","description":"Blur cost and scene copies"},)"
        R"({"id":"advanced","title":"Advanced","description":"Depth setup and tools"}],"nodes":[)";

    struct Section {
        const char* id;
        const char* title;
        const char* category;
    };
    constexpr Section sections[]{
        {"choose_preset", "Choose a preset", "presets"},   {"reset_settings", "Reset", "presets"},
        {"focus_point", "Focus point", "focus"},           {"sharp_range", "Sharp range", "focus"},
        {"blur_look", "Blur look", "blur"},                {"blur_cost", "Blur cost", "performance"},
        {"depth_setup", "Depth setup", "advanced"},        {"tools", "Tools", "advanced"},
    };

    std::vector<MenuNode> nodes;
    nodes.reserve(kBoolSettings.size() + kFloatSettings.size() + kIntSettings.size() + 6);
    const auto append = [&](MenuNode value) { nodes.push_back(std::move(value)); };

    append(node("preset_performance", "Performance", "Quarter size blur, 8 samples. The lightest blur.", "presets", "choose_preset", "button"));
    append(node("preset_balanced", "Balanced: recommended", "Half size blur, 16 samples. The default setup.", "presets", "choose_preset", "button"));
    append(node("preset_quality", "High", "Full size blur, 28 samples. Smoothest and sharpest, highest GPU cost.", "presets", "choose_preset", "button"));
    append(node("reset_defaults", "Reset all settings", "Back to the default focus, blur and performance settings.", "presets", "reset_settings", "button"));

    for (const auto& setting : kBoolSettings)
        append(node(setting.key, setting.title, setting.description, setting.category, setting.section, "toggle", bstr(setting.initial),
                    bstr((c.*setting.value).load())));
    for (const auto& setting : kFloatSettings) {
        const std::string extra = ",\"min_value\":" + jsonQuote(fstr(setting.minimum)) + ",\"max_value\":" + jsonQuote(fstr(setting.maximum)) +
                                  ",\"step\":" + jsonQuote(fstr(setting.step)) + ",\"unit\":" + jsonQuote(setting.unit);
        append(node(setting.key, setting.title, setting.description, setting.category, setting.section, "slider_float", fstr(setting.initial),
                    fstr((c.*setting.value).load()), extra));
    }
    for (const auto& setting : kIntSettings) append(integerNode(setting, (c.*setting.value).load()));

    bool first = true;
    const auto emit = [&](const std::string& json) {
        if (!first) s += ',';
        first = false;
        s += json;
    };
    for (const auto& section : sections) {
        emit(node(section.id, section.title, "", section.category, "", "section").json);
        for (const auto& entry : nodes)
            if (entry.section == section.id) emit(entry.json);
    }
    return s + "]}";
}

constexpr auto kSliderSettle = std::chrono::milliseconds(400);
constexpr auto kQuickDelay = std::chrono::milliseconds(30);
constexpr auto kIdleSettle = std::chrono::milliseconds(1200);

std::mutex gPushMutex;
std::string gLastPushed;

void refreshSchema() {
    if (!gSetSchema) return;
    const std::string json = schemaJson();
    std::lock_guard<std::mutex> push(gPushMutex);
    if (json == gLastPushed) return;
    if (gSetSchema(kModule, json)) gLastPushed = json;
}

std::mutex gRefreshMutex;
std::condition_variable gRefreshCv;
std::thread gRefreshThread;
bool gRefreshRunning = false;
bool gRefreshStop = false;
std::uint64_t gRefreshRequested = 0;
std::uint64_t gRefreshDone = 0;
std::chrono::milliseconds gRefreshDelay = kQuickDelay;

void refreshWorker() {
    std::unique_lock<std::mutex> lock(gRefreshMutex);
    for (;;) {
        gRefreshCv.wait(lock, [] { return gRefreshStop || gRefreshRequested != gRefreshDone; });
        if (gRefreshStop) break;
        std::uint64_t seen = gRefreshRequested;
        while (!gRefreshStop && gRefreshCv.wait_for(lock, gRefreshDelay, [&] { return gRefreshStop || gRefreshRequested != seen; })) {
            seen = gRefreshRequested;
        }
        if (gRefreshStop) break;
        gRefreshDelay = kQuickDelay;
        lock.unlock();
        refreshSchema();
        lock.lock();
        gRefreshDone = seen;
    }
}

void startRefreshWorker() {
    std::lock_guard<std::mutex> lock(gRefreshMutex);
    if (gRefreshRunning) return;
    gRefreshStop = false;
    gRefreshRequested = gRefreshDone = 0;
    gRefreshDelay = kQuickDelay;
    gLastPushed.clear();
    gRefreshThread = std::thread(refreshWorker);
    gRefreshRunning = true;
}

void stopRefreshWorker() {
    {
        std::lock_guard<std::mutex> lock(gRefreshMutex);
        if (!gRefreshRunning) return;
        gRefreshStop = true;
        gRefreshCv.notify_one();
    }
    if (gRefreshThread.joinable()) gRefreshThread.join();
    std::lock_guard<std::mutex> lock(gRefreshMutex);
    gRefreshRunning = false;
    gRefreshStop = false;
}

enum class Refresh { Quick, Slider, Idle };

void scheduleSchemaRefresh(Refresh kind) {
    std::lock_guard<std::mutex> lock(gRefreshMutex);
    if (!gRefreshRunning) return;
    const auto delay = kind == Refresh::Slider ? kSliderSettle : kind == Refresh::Idle ? kIdleSettle : kQuickDelay;
    if (delay > gRefreshDelay || gRefreshRequested == gRefreshDone) gRefreshDelay = delay;
    ++gRefreshRequested;
    gRefreshCv.notify_one();
}

constexpr bool kPushSchemaInCallback = true;

void pushChangedValues() {
    {
        std::lock_guard<std::mutex> push(gPushMutex);
        gLastPushed.clear();
    }
    if (kPushSchemaInCallback) refreshSchema();
    else scheduleSchemaRefresh(Refresh::Quick);
}

void handleToggle(bool enabled) {
    config().enabled.store(enabled);
    touchConfig();
    scheduleSchemaRefresh(Refresh::Idle);
}

void handleConfigChanged(std::string_view key, std::string_view value) {
    auto& c = config();
    for (const auto& setting : kBoolSettings) {
        if (key != setting.key) continue;
        (c.*setting.value).store(parseBool(value, setting.initial));
        touchConfig();
        scheduleSchemaRefresh(Refresh::Idle);
        return;
    }
    for (const auto& setting : kFloatSettings) {
        if (key != setting.key) continue;
        (c.*setting.value).store(parseFloat(value, setting.initial, setting.minimum, setting.maximum));
        touchConfig();
        scheduleSchemaRefresh(Refresh::Idle);
        return;
    }
    for (const auto& setting : kIntSettings) {
        if (key != setting.key) continue;
        const float parsed = parseFloat(value, static_cast<float>(setting.initial), static_cast<float>(setting.minimum), static_cast<float>(setting.maximum));
        (c.*setting.value).store(std::clamp(static_cast<int>(std::lround(parsed)), setting.minimum, setting.maximum));
        touchConfig();
        scheduleSchemaRefresh(Refresh::Idle);
        return;
    }
    if (key == "preset_performance" || key == "preset_balanced" || key == "preset_quality") {
        applyQualityPreset(key == "preset_performance" ? 0 : key == "preset_balanced" ? 1 : 2);
        touchConfig();
        pushChangedValues();
    } else if (key == "reset_defaults") {
        resetConfigDefaults();
        touchConfig();
        pushChangedValues();
        if (gSetEnabled) gSetEnabled(kModule, config().enabled.load());
    }
}

}

void notifyConfigReloaded() {
    if (!gRegistered.load()) return;
    {
        std::lock_guard<std::mutex> push(gPushMutex);
        gLastPushed.clear();
    }
    scheduleSchemaRefresh(Refresh::Quick);
    if (gSetEnabled) gSetEnabled(kModule, config().enabled.load());
}

bool registerMenu() {
    gSetSchema = reinterpret_cast<SetSchemaFn>(dlsym(
        RTLD_DEFAULT, "_ZN2pl7modmenu19setConfigSchemaJsonENSt6__ndk117basic_string_viewIcNS1_11char_traitsIcEEEES5_"));
    gSetEnabled = reinterpret_cast<SetEnabledFn>(dlsym(
        RTLD_DEFAULT, "_ZN2pl7modmenu16setModuleEnabledENSt6__ndk117basic_string_viewIcNS1_11char_traitsIcEEEEb"));

    pl::modmenu::ModuleInfo m{};
    m.moduleId = std::string(kModule);
    m.displayName = "Depth of Field";
    m.description = "Depth of field blur";
    m.modId = std::string(kOwnerModId);
    m.defaultEnabled = config().enabled.load();
    m.hideInHudEditor = true;

    using pl::modmenu::ConfigType;
    for (const auto& setting : kBoolSettings)
        m.configs.push_back({setting.key, setting.title, ConfigType::Toggle, bstr((config().*setting.value).load()), "", "", ""});
    for (const auto& setting : kFloatSettings)
        m.configs.push_back({setting.key, setting.title, ConfigType::SliderFloat, fstr((config().*setting.value).load()), fstr(setting.minimum), fstr(setting.maximum), ""});
    for (const auto& setting : kIntSettings)
        m.configs.push_back({setting.key, setting.title, ConfigType::SliderInt, std::to_string((config().*setting.value).load()),
                             std::to_string(setting.minimum), std::to_string(setting.maximum), ""});
    for (const auto& action : std::array<std::pair<const char*, const char*>, 4>{{{"preset_performance", "Performance preset"},
                                                                                  {"preset_balanced", "Balanced preset"},
                                                                                  {"preset_quality", "High preset"},
                                                                                  {"reset_defaults", "Reset all settings"}}})
        m.configs.push_back({action.first, action.second, ConfigType::Button, "", "", "", ""});
    m.onToggle = [](std::string_view, bool enabled) { handleToggle(enabled); };
    m.onConfigChanged = [](std::string_view, std::string_view key, std::string_view value) { handleConfigChanged(key, value); };
    m.onKeybind = {};

    startRefreshWorker();
    const bool ok = pl::modmenu::registerModule(m);
    if (ok) {
        gRegistered.store(true);
        refreshSchema();
    }
    return ok;
}

void unregisterMenu() {
    gRegistered.store(false);
    stopRefreshWorker();
    pl::modmenu::unregisterModule(kModule);
    gSetSchema = nullptr;
    gSetEnabled = nullptr;
}

}
