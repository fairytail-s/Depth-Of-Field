#include "Mod.hpp"

#include "Config.hpp"
#include "GlHooks.hpp"
#include "Menu.hpp"

namespace dof {

bool DepthOfFieldMod::load() {
    loadConfigFromDisk();
    setConfigReloadedCallback(&notifyConfigReloaded);
    startConfigAutosave();
    return true;
}

bool DepthOfFieldMod::enable() {
    menu_ = registerMenu();

    if (!installGlHooks()) return menu_;
    hooked_ = true;
    return true;
}

bool DepthOfFieldMod::disable() {
    if (menu_) {
        unregisterMenu();
        menu_ = false;
    }
    if (hooked_) {
        removeGlHooks();
        hooked_ = false;
    }
    return true;
}

bool DepthOfFieldMod::unload() {
    disable();
    stopConfigAutosave();
    return true;
}

}
