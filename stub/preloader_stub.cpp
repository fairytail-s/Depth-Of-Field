#include <pl/ModMenu.hpp>
#include <pl/memory/Hook.hpp>

namespace pl::memory {
int hook(FuncPtr, FuncPtr, FuncPtr*, HookPriority) { return -1; }
bool unhook(FuncPtr, FuncPtr) { return false; }
}

namespace pl::modmenu {
bool registerModule(const ModuleInfo&) { return false; }
void unregisterModule(std::string_view) {}
}
