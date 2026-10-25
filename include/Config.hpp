#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <string>

namespace dof {

constexpr float kDefaultFocus = 12.0f, kMinFocus = 1.0f, kMaxFocus = 512.0f;
constexpr float kDefaultZone = 6.0f, kMinZone = 0.0f, kMaxZone = 64.0f;
constexpr float kDefaultFalloff = 32.0f, kMinFalloff = 2.0f, kMaxFalloff = 256.0f;
constexpr float kDefaultMaxBlur = 12.0f, kMinMaxBlur = 0.5f, kMaxMaxBlur = 40.0f;
constexpr float kDefaultEdgeBlur = 0.6f, kMinEdgeBlur = 0.0f, kMaxEdgeBlur = 1.0f;
constexpr float kDefaultEdgeClear = 0.4f, kMinEdgeClear = 0.0f, kMaxEdgeClear = 0.95f;
constexpr int kDefaultQuality = 16, kMinQuality = 6, kMaxQuality = 48;
constexpr float kDefaultFocusSpeed = 6.0f, kMinFocusSpeed = 0.5f, kMaxFocusSpeed = 30.0f;
constexpr float kDefaultFarPlane = 256.0f, kMinFarPlane = 32.0f, kMaxFarPlane = 1024.0f;
constexpr int kDefaultDownscale = 2, kMinDownscale = 1, kMaxDownscale = 4;
constexpr int kDefaultMinDraws = 1, kMinMinDraws = 1, kMaxMinDraws = 100000;

struct RuntimeConfig {
    std::atomic_bool enabled{true};
    std::atomic_bool autoFocus{false};
    std::atomic_bool nearBlur{true};
    std::atomic_bool blurSky{true};
    std::atomic_bool reversedDepth{false};
    std::atomic_bool debugView{false};
    std::atomic_bool directSampling{true};
    std::atomic<float> focus{kDefaultFocus};
    std::atomic<float> zone{kDefaultZone};
    std::atomic<float> falloff{kDefaultFalloff};
    std::atomic<float> maxBlur{kDefaultMaxBlur};
    std::atomic<float> edgeBlur{kDefaultEdgeBlur};
    std::atomic<float> edgeClear{kDefaultEdgeClear};
    std::atomic<float> focusSpeed{kDefaultFocusSpeed};
    std::atomic<float> farPlane{kDefaultFarPlane};
    std::atomic<int> quality{kDefaultQuality};
    std::atomic<int> downscale{kDefaultDownscale};
    std::atomic<int> minDraws{kDefaultMinDraws};
};

struct BoolSetting {
    const char *key, *title, *description, *category, *section;
    std::atomic_bool RuntimeConfig::*value;
    bool initial;
};
struct FloatSetting {
    const char *key, *title, *description, *category, *section;
    std::atomic<float> RuntimeConfig::*value;
    float initial, minimum, maximum, step;
    const char* unit;
};
struct IntSetting {
    const char *key, *title, *description, *category, *section;
    std::atomic<int> RuntimeConfig::*value;
    int initial, minimum, maximum, step;
};

inline constexpr std::array kBoolSettings{
    BoolSetting{"auto_focus", "Auto focus", "Focus on whatever is in the middle of the screen instead of the focus distance.", "focus", "focus_point", &RuntimeConfig::autoFocus, false},
    BoolSetting{"near_blur", "Blur near things", "Also blur things closer than the sharp zone.", "focus", "sharp_range", &RuntimeConfig::nearBlur, true},
    BoolSetting{"blur_sky", "Blur sky", "The sky counts as far away and is blurred.", "blur", "blur_look", &RuntimeConfig::blurSky, true},
    BoolSetting{"direct_sampling", "Avoid extra scene copies", "Read the game's own colour and depth buffers instead of copying them every frame (faster). Other targets are copied automatically.", "performance", "blur_cost", &RuntimeConfig::directSampling, true},
    BoolSetting{"reversed_depth", "Reversed depth", "Turn on if the blur sits on the wrong things (near and far swapped).", "advanced", "depth_setup", &RuntimeConfig::reversedDepth, false},
    BoolSetting{"debug_view", "Show blur amount", "Tints the picture red where it is blurred. Useful while tuning.", "advanced", "tools", &RuntimeConfig::debugView, false}
};

inline constexpr std::array kFloatSettings{
    FloatSetting{"focus_distance", "Focus distance", "Distance that is sharp. Not used while Auto focus is on.", "focus", "focus_point", &RuntimeConfig::focus, kDefaultFocus, kMinFocus, kMaxFocus, 1.0f, "blocks"},
    FloatSetting{"focus_speed", "Auto focus speed", "How fast Auto focus follows what you look at.", "focus", "focus_point", &RuntimeConfig::focusSpeed, kDefaultFocusSpeed, kMinFocusSpeed, kMaxFocusSpeed, 0.5f, ""},
    FloatSetting{"focus_zone", "Sharp zone", "Blocks in front of and behind the focus distance that stay sharp.", "focus", "sharp_range", &RuntimeConfig::zone, kDefaultZone, kMinZone, kMaxZone, 1.0f, "blocks"},
    FloatSetting{"blur_falloff", "Blur falloff", "Blocks behind the sharp zone until the blur is at its strongest. Smaller values blur faster.", "focus", "sharp_range", &RuntimeConfig::falloff, kDefaultFalloff, kMinFalloff, kMaxFalloff, 2.0f, "blocks"},
    FloatSetting{"max_blur", "Blur strength", "Largest blur size, in pixels on a 1080p screen.", "blur", "blur_look", &RuntimeConfig::maxBlur, kDefaultMaxBlur, kMinMaxBlur, kMaxMaxBlur, 0.5f, "px"},
    FloatSetting{"edge_blur", "Edge blur", "Blurs the left and right (and all outer) parts of the screen, leaving a sharp circle in the middle. 0 = off, 1 = as strong as Blur strength.", "blur", "blur_look", &RuntimeConfig::edgeBlur, kDefaultEdgeBlur, kMinEdgeBlur, kMaxEdgeBlur, 0.05f, ""},
    FloatSetting{"edge_clear", "Sharp centre size", "Size of the sharp circle in the middle (0 = tiny, 1 = almost the whole screen). Only used when Edge blur is above 0.", "blur", "blur_look", &RuntimeConfig::edgeClear, kDefaultEdgeClear, kMinEdgeClear, kMaxEdgeClear, 0.05f, ""},
    FloatSetting{"far_plane", "Far distance", "Far clip distance in blocks (render distance times 16). Used to turn depth into blocks.", "advanced", "depth_setup", &RuntimeConfig::farPlane, kDefaultFarPlane, kMinFarPlane, kMaxFarPlane, 16.0f, "blocks"}
};

inline constexpr std::array kIntSettings{
    IntSetting{"blur_downscale", "Blur size", "The blur is made at a fraction of the screen size. Smaller is faster and a little softer.", "performance", "blur_cost", &RuntimeConfig::downscale, kDefaultDownscale, kMinDownscale, kMaxDownscale, 1},
    IntSetting{"quality", "Blur quality", "Most samples per pixel (large blurs use up to this many, small ones fewer). More is smoother but slower.", "performance", "blur_cost", &RuntimeConfig::quality, kDefaultQuality, kMinQuality, kMaxQuality, 2}
};

RuntimeConfig& config();
void resetConfigDefaults();
void applyQualityPreset(int preset);
void touchConfig();
void setConfigReloadedCallback(void (*callback)());
bool loadConfigFromDisk();
void startConfigAutosave();
void stopConfigAutosave();
const std::string& configFilePath();

}
