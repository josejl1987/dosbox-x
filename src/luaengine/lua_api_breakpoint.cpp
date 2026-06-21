// Breakpoint API Registration for DOSBox-X Lua Engine
// Separated from main LuaEngine.cpp for better organization

#include "luaengine.h"

// Required includes for breakpoint operations
#include "logging.h"      // For DEBUG_ShowMsg
#include "debug_bridge.h"

#ifdef C_LUA

void LuaEngine::registerBreakpointAPI() {
    auto breakpoint_table = lua.create_table();

    // Basic breakpoint operations using DOSBox-X's CBreakpoint system
    breakpoint_table["add"] = [this](uint16_t cs, uint16_t ip, sol::optional<std::string> name,
                                   sol::optional<std::string> condition, sol::optional<std::string> action,
                                   sol::optional<bool> stop) -> bool {
        
        bool should_stop = stop.value_or(true);
        std::string bp_name = name.value_or("");
        std::string bp_cond = condition.value_or("");
        std::string bp_act = action.value_or("");

        // Add to Lua engine's internal map (checked by LuaInstructionHook)
        luaEngine.addLuaBreakpoint(cs, ip, bp_name, bp_cond, bp_act, should_stop);

        // If we want to stop, we CAN add a physical breakpoint for optimization
        // But for tracepoints (should_stop=false), we MUST NOT add a CBreakpoint
        // because CBreakpoint always hard-pauses the loop
        if (should_stop) {
            DebugBreakpointHandle bp = DebugBridge::AddBreakpoint(cs, ip, false);
            if (bp) {
                DebugBridge::Activate(bp, true);

                // Store additional info if provided (thread-safe)
                {
                    std::lock_guard<std::mutex> lock(breakpoint_metadata_mutex_);
                    if (name.has_value()) {
                        // Store name in a map for later retrieval
                        breakpoint_names_[bp] = name.value();
                    }
                    if (condition.has_value()) {
                        // Store Lua condition for evaluation
                        breakpoint_conditions_[bp] = condition.value();
                    }
                    if (action.has_value()) {
                        // Store Lua action to execute when hit
                        breakpoint_actions_[bp] = action.value();
                    }
                }

                DEBUG_ShowMsg("LuaEngine: Breakpoint added at %04X:%04X", cs, ip);
            } else {
                DEBUG_ShowMsg("LuaEngine: Failed to add physical breakpoint at %04X:%04X", cs, ip);
            }
            DEBUG_ShowMsg("LuaEngine: Breakpoint added at %04X:%04X", cs, ip);
            return true;
        } else {
            // Ensure no physical breakpoint exists that would force a stop
            DebugBridge::DeleteBreakpoint(cs, ip);

            DEBUG_ShowMsg("LuaEngine: Tracepoint added at %04X:%04X", cs, ip);
            return true;
        }
        };

    breakpoint_table["remove"] = [this](uint16_t cs, uint16_t ip) -> bool {
        // Remove from Lua engine's internal map (handles both breakpoints and tracepoints)
        luaEngine.removeLuaBreakpoint(cs, ip);

        // Also remove physical breakpoint if it exists
        DebugBreakpointHandle bp = DebugBridge::FindPhysBreakpoint(cs, ip, false);
        if (bp) {
            DebugBridge::Activate(bp, false);

            // Clean up our additional data (thread-safe)
            {
                std::lock_guard<std::mutex> lock(breakpoint_metadata_mutex_);
                breakpoint_names_.erase(bp);
                breakpoint_conditions_.erase(bp);
                breakpoint_actions_.erase(bp);
            }

            // Remove the physical breakpoint entry to avoid stale instances
            DebugBridge::DeleteBreakpoint(cs, ip);
        }

        DEBUG_ShowMsg("LuaEngine: Breakpoint/Tracepoint removed at %04X:%04X", cs, ip);
        return true;
        };

    breakpoint_table["enable"] = [this](uint16_t cs, uint16_t ip, bool enabled) -> bool {
        // Enable/disable in Lua engine's internal map (handles both breakpoints and tracepoints)
        luaEngine.enableLuaBreakpoint(cs, ip, enabled);

        // Also enable/disable physical breakpoint if it exists
        DebugBreakpointHandle bp = DebugBridge::FindPhysBreakpoint(cs, ip, false);
        if (bp) {
            DebugBridge::Activate(bp, enabled);
        }

        DEBUG_ShowMsg("LuaEngine: Breakpoint/Tracepoint %s at %04X:%04X", enabled ? "enabled" : "disabled", cs, ip);
        return true;
        };

    breakpoint_table["list"] = [this]() -> sol::table {
        // Return list of breakpoints with their metadata (thread-safe)
        auto result = lua.create_table();
        int index = 1;
        
        std::lock_guard<std::mutex> lock(breakpoint_metadata_mutex_);
        
        // Iterate through our tracked breakpoints
        for (const auto& [bp, name] : breakpoint_names_) {
            if (DebugBridge::IsActive(bp)) {
                auto bp_info = lua.create_table();
#if 0
                // ponytail: CBreakpoint instance method, needs full bridge
                bp_info["segment"] = bp->GetSegment();
                bp_info["offset"] = bp->GetOffset();
                bp_info["address"] = bp->GetLocation();
                bp_info["once"] = bp->GetOnce();
#endif
                bp_info["name"] = name;
                bp_info["active"] = DebugBridge::IsActive(bp);
                
                // Add condition if present
                auto cond_it = breakpoint_conditions_.find(bp);
                if (cond_it != breakpoint_conditions_.end()) {
                    bp_info["condition"] = cond_it->second;
                }
                
                // Add action if present
                auto action_it = breakpoint_actions_.find(bp);
                if (action_it != breakpoint_actions_.end()) {
                    bp_info["action"] = action_it->second;
                }
                
                result[index++] = bp_info;
            }
        }
        
        return result;
        };

    // INT and memory breakpoint helpers for reverse-engineering
    breakpoint_table["add_int"] = [this](uint8_t intNum,
                                         sol::optional<uint16_t> ah,
                                         sol::optional<uint16_t> al,
                                         sol::optional<std::string> name,
                                         sol::optional<std::string> condition,
                                         sol::optional<std::string> action,
                                         sol::optional<bool> stop) -> bool {
        uint16_t ah_val = ah.value_or(DEBUG_BRIDGE_BPINT_ALL);
        uint16_t al_val = al.value_or(DEBUG_BRIDGE_BPINT_ALL);
        bool should_stop = stop.value_or(true);

        DebugBreakpointHandle bp = DebugBridge::AddIntBreakpoint(intNum, ah_val, al_val, false);
        if (!bp) {
            DEBUG_ShowMsg("LuaEngine: Failed to add INT breakpoint on 0x%02X", intNum);
            return false;
        }

        std::string bp_name = name.value_or("");
        std::string bp_cond = condition.value_or("");
        std::string bp_act = action.value_or("");

        {
            std::lock_guard<std::mutex> lock(breakpoint_metadata_mutex_);
            if (!bp_name.empty()) breakpoint_names_[bp] = bp_name;
            if (!bp_cond.empty()) breakpoint_conditions_[bp] = bp_cond;
            if (!bp_act.empty()) breakpoint_actions_[bp] = bp_act;
        }

        DEBUG_ShowMsg("LuaEngine: INT 0x%02X breakpoint added (AH=0x%04X, AL=0x%04X)",
                      intNum, ah_val, al_val);
        (void)should_stop; // core INT breakpoint always pauses right now
        return true;
        };

    breakpoint_table["add_mem"] = [this](uint16_t seg, uint32_t off,
                                          sol::optional<std::string> name) -> bool {
        DebugBreakpointHandle bp = DebugBridge::AddMemBreakpoint(seg, off);
        if (!bp) {
            DEBUG_ShowMsg("LuaEngine: Failed to add memory breakpoint at %04X:%04X", seg, off);
            return false;
        }
        if (name.has_value()) {
            std::lock_guard<std::mutex> lock(breakpoint_metadata_mutex_);
            breakpoint_names_[bp] = name.value();
        }
        DEBUG_ShowMsg("LuaEngine: Memory breakpoint added at %04X:%04X", seg, off);
        return true;
        };

    // Performance control
    breakpoint_table["set_fast_mode"] = [this](bool enable) {
        // For now, just log the setting since breakpoint fast mode isn't implemented yet
        DEBUG_ShowMsg("LuaEngine: Breakpoint fast mode %s", enable ? "enabled" : "disabled");
        };

    lua["breakpoint"] = breakpoint_table;
    luaEngine.log_info("Breakpoint API registered with DOSBox-X integration");
}
#endif
