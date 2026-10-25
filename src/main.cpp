#include "Abi.hpp"
#include "Mod.hpp"

namespace {

dof::DepthOfFieldMod gMod;

template <bool (dof::DepthOfFieldMod::*Fn)()>
bool thunk(void*, void*) {
    try {
        return (gMod.*Fn)();
    } catch (...) {
        return false;
    }
}

}

extern "C" __attribute__((visibility("default"))) ModRegistration* PLGetModRegistration() {
    static ModRegistration reg{&gMod, &thunk<&dof::DepthOfFieldMod::load>, &thunk<&dof::DepthOfFieldMod::enable>,
                               &thunk<&dof::DepthOfFieldMod::disable>, &thunk<&dof::DepthOfFieldMod::unload>};
    return &reg;
}
