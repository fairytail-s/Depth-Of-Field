#pragma once
namespace dof {

class DepthOfFieldMod {
public:
    bool load();
    bool enable();
    bool disable();
    bool unload();

private:
    bool hooked_{};
    bool menu_{};
};

}
