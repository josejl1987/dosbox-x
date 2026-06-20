#ifndef EXECUTION_TOOLBAR_H
#define EXECUTION_TOOLBAR_H

#include "core_debug_interface.h"

namespace LuaEngineDebugger {

// Shared execution toolbar rendered inside the dockspace.
// State-aware: Resume when paused, Pause when running. Step controls disabled when CPU running.
class ExecutionToolbar {
public:
    explicit ExecutionToolbar(LuaEngineDebug::CoreDebugInterface* debug);
    void render();

private:
    LuaEngineDebug::CoreDebugInterface* debug_;
};

} // namespace LuaEngineDebugger

#endif // EXECUTION_TOOLBAR_H
