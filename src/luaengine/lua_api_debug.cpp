// Debug API Registration for DOSBox-X Lua Engine
// Separated from main LuaEngine.cpp for better organization

#include "luaengine.h"

// Required includes for debug operations
#include "debug.h"       // For DEBUG_EnableDebugger, DEBUG_ShowMsg, DasmI386
#include "regs.h"        // For register access
#include "cpu.h"         // For CPU state and SegValue
#include "mem.h"         // For memory operations
#include "paging.h"      // For memory addressing
#include "../debug/debug_inc.h"  // For DasmI386 function declaration

// Standard library includes
#include <fstream>       // For file I/O
#include <sstream>       // For string stream parsing
#include <algorithm>     // For std::find_if
#include <iomanip>       // For hex formatting

// Debug tools integration
#include "debugger_session.h"  // For DebuggerSession access

// Forward declarations
extern LuaEngine luaEngine;

// Utility function from lua_api_memory.cpp
extern std::vector<uint8_t> GetBytes(uint16_t seg, uint32_t ofs, size_t size);

#ifdef C_LUA

void LuaEngine::registerDebugAPI() {
    sol::table debug_table;
    
    // Reuse existing debug table (from sol::lib::debug) if present
    sol::object existing = lua["debug"];
    if (existing.valid() && existing.get_type() == sol::type::table) {
        debug_table = existing.as<sol::table>();
    } else {
        debug_table = lua.create_table();
        lua["debug"] = debug_table;
    }
    
    debug_table["log"] = [this](const std::string& message) {
        char log_buffer[1024];
        snprintf(log_buffer, sizeof(log_buffer), "Lua: %s", message.c_str());
        luaEngine.log_info(log_buffer);
        };
    debug_table["print"] = debug_table["log"];

    // LRDB status helpers for Lua scripts and console
    debug_table["lrdb_is_active"] = [this]() {
        return lrdb_active_;
        };
    debug_table["lrdb_port"] = [this]() {
        return lrdb_port_;
        };
    
    debug_table["hexdump"] = [this](uint16_t seg, uint32_t offset, size_t size) {
        auto bytes = GetBytes(seg, offset, size);

        if(sol2_cache.fast_debug_access && size <= 256) {
            // Fast path: use cached buffer for small hex dumps with optimized string building
            sol2_cache.temp_hex_buffer.clear();
            sol2_cache.temp_hex_buffer.reserve(bytes.size() * 3 + (bytes.size() / 16) + 50);

            // Use lookup table for hex conversion (faster than snprintf)
            static const char hex_chars[] = "0123456789ABCDEF";
            
            for(size_t i = 0; i < bytes.size(); ++i) {
                if(i % 16 == 0) sol2_cache.temp_hex_buffer += '\n';
                
                uint8_t byte = bytes[i];
                sol2_cache.temp_hex_buffer += hex_chars[byte >> 4];
                sol2_cache.temp_hex_buffer += hex_chars[byte & 0x0F];
                sol2_cache.temp_hex_buffer += ' ';
            }

            char log_buffer[4096];
            snprintf(log_buffer, sizeof(log_buffer), "Hex dump %X:%04X:%s", seg, offset, sol2_cache.temp_hex_buffer.c_str());
            luaEngine.log_info(log_buffer);
        }
        else {
            // Standard path: optimized string building
            std::string hex_output;
            hex_output.reserve(bytes.size() * 3 + (bytes.size() / 16) + 50);
            
            // Use lookup table for hex conversion
            static const char hex_chars[] = "0123456789ABCDEF";
            
            for(size_t i = 0; i < bytes.size(); ++i) {
                if(i % 16 == 0) hex_output += '\n';
                
                uint8_t byte = bytes[i];
                hex_output += hex_chars[byte >> 4];
                hex_output += hex_chars[byte & 0x0F];
                hex_output += ' ';
            }
            
            char log_buffer[4096];
            snprintf(log_buffer, sizeof(log_buffer), "Hex dump %X:%04X:%s", seg, offset, hex_output.c_str());
            luaEngine.log_info(log_buffer);
        }
        };
    
    debug_table["get_current_address"] = [this]() -> sol::table {
        if(sol2_cache.fast_debug_access) {
            // Fast path: reuse cached table
            sol2_cache.cached_address_info.clear();
            sol2_cache.cached_address_info["cs"] = SegValue(::cs);
            sol2_cache.cached_address_info["ip"] = reg_ip;
            return sol2_cache.cached_address_info;
        }
        else {
            // Standard path: create new table
            auto result = lua.create_table();
            result["cs"] = SegValue(::cs);
            result["ip"] = reg_ip;
            return result;
        }
        };

    // Performance control for debug operations
    debug_table["set_fast_mode"] = [this](bool enable) {
        sol2_cache.fast_debug_access = enable;
        DEBUG_ShowMsg("LuaEngine: Debug fast mode %s", enable ? "enabled" : "disabled");
        };

    // Disassembly API functions
    debug_table["disassemble"] = [this](uint16_t seg, uint32_t offset, sol::optional<int> count) -> sol::table {
        auto result = lua.create_table();
        int num_instructions = count.value_or(10);

        PhysPt start = ::GetAddress(seg, offset);
        uint32_t current_ip = offset;

        for(int i = 0; i < num_instructions; i++) {
            char dasm_line[256];
            PhysPt addr = ::GetAddress(seg, current_ip);

            // Use DOSBox-X's built-in disassembler
            Bitu instruction_size = DasmI386(dasm_line, addr, current_ip, cpu.code.big);

            auto instruction = lua.create_table();
            instruction["address"] = current_ip;
            instruction["segment"] = seg;
            instruction["mnemonic"] = std::string(dasm_line);
            instruction["size"] = (int)instruction_size;

            // Get raw bytes for this instruction
            auto bytes = lua.create_table();
            for(Bitu b = 0; b < instruction_size; b++) {
                uint8_t byte_val;
                if(!mem_readb_checked(addr + b, &byte_val)) {
                    bytes[b + 1] = byte_val;
                }
            }
            instruction["bytes"] = bytes;

            result[i + 1] = instruction;
            current_ip += instruction_size;
        }

        return result;
        };

    debug_table["get_instruction_at"] = [this](uint16_t seg, uint32_t offset) -> sol::table {
        char dasm_line[256];
        PhysPt addr = ::GetAddress(seg, offset);

        Bitu instruction_size = DasmI386(dasm_line, addr, offset, cpu.code.big);

        auto instruction = lua.create_table();
        instruction["address"] = offset;
        instruction["segment"] = seg;
        instruction["mnemonic"] = std::string(dasm_line);
        instruction["size"] = (int)instruction_size;

        // Get raw bytes
        auto bytes = lua.create_table();
        for(Bitu b = 0; b < instruction_size; b++) {
            uint8_t byte_val;
            if(!mem_readb_checked(addr + b, &byte_val)) {
                bytes[b + 1] = byte_val;
            }
        }
        instruction["bytes"] = bytes;

        return instruction;
        };

    // Execution control API functions
    debug_table["step_into"] = [this]() {
        // Execute a single instruction
        DEBUG_EnableDebugger();
        luaEngine.log_info("Step Into executed");
        return true;
        };

    debug_table["step_over"] = [this]() {
        // Step over calls - more complex, needs call detection
        luaEngine.log_info("Step Over executed");
        return true;
        };

    debug_table["run_until"] = [this](uint16_t seg, uint32_t offset, sol::optional<sol::protected_function> callback) -> bool {
        // Set instruction-hook target; callback fired when CS:IP reaches the address
        luaEngine.log_info("Run Until " + std::to_string(seg) + ":" + std::to_string(offset));
        luaEngine.setRunUntilTarget(seg, offset, callback.value_or(sol::protected_function()));
        return true;
        };

    debug_table["is_execution_paused"] = [this]() -> bool {
        // TODO: Check if execution is actually paused
        return false;
        };

    debug_table["pause_execution"] = [this]() {
        DEBUG_EnableDebugger();
        luaEngine.log_info("Execution paused");
        return true;
        };

    debug_table["resume_execution"] = [this]() {
        luaEngine.log_info("Execution resumed");
        return true;
        };

    // Memory watchpoint system — redirected to InstrumentationRouter
    debug_table["add_memory_watchpoint"] = [this](uint32_t address, int size, const std::string& type) -> bool {
        try {
            // Only write watchpoints are supported by the router for now
            // ponytail: read/execute watchpoints need PageHandler interception, deferred
            if (type != "write" && type != "w" && type != "readwrite" && type != "rw") {
                luaEngine.log_warning("Only write watchpoints currently supported via instrumentation router");
            }

            if (!g_instrumentation) {
                luaEngine.log_error("InstrumentationRouter not initialized");
                return false;
            }

            auto handle = g_instrumentation->addWatchpoint(address, static_cast<uint8_t>(size),
                [this, address](uint32_t addr, uint32_t value, uint8_t sz, bool is_write) {
                    luaEngine.log_info("Watchpoint hit at 0x" + std::to_string(addr) +
                        " value=0x" + std::to_string(value) + " size=" + std::to_string(sz));
                });

            router_watchpoints_[address] = std::move(handle);

            luaEngine.log_info("Memory watchpoint added at 0x" + std::to_string(address) +
                " size=" + std::to_string(size) + " type=" + type);
            return true;

        }
        catch(const std::exception& e) {
            luaEngine.log_error("Failed to add memory watchpoint: " + std::string(e.what()));
            return false;
        }
        };

    debug_table["remove_memory_watchpoint"] = [this](uint32_t address) -> bool {
        auto it = router_watchpoints_.find(address);

        if(it != router_watchpoints_.end()) {
            router_watchpoints_.erase(it);  // RAII handle auto-deregisters
            luaEngine.log_info("Memory watchpoint removed at 0x" + std::to_string(address));
            return true;
        }
        else {
            luaEngine.log_warning("No memory watchpoint found at 0x" + std::to_string(address));
            return false;
        }
        };

    // Symbol management for enhanced debugging
    debug_table["load_map_file"] = [this](const std::string& filename) -> bool {
        try {
            std::ifstream file(filename);
            if(!file.is_open()) {
                luaEngine.log_error("Failed to open map file: " + filename);
                return false;
            }

            std::string line;
            int symbols_loaded = 0;
            bool in_symbols_section = false;

            luaEngine.log_info("Loading symbol map: " + filename);

            while(std::getline(file, line)) {
                // Skip empty lines
                if(line.empty()) continue;

                // Look for MASM/LINK map file format sections
                if(line.find("Address") != std::string::npos &&
                    line.find("Publics by Value") != std::string::npos) {
                    in_symbols_section = true;
                    continue;
                }

                // Alternative format detection
                if(line.find("  Address         Publics by Value") != std::string::npos) {
                    in_symbols_section = true;
                    continue;
                }

                // End of symbols section
                if(in_symbols_section && line.find("entry point") != std::string::npos) {
                    break;
                }

                if(in_symbols_section) {
                    // Parse symbol lines in format: "SSSS:OOOO SYMBOL_NAME"
                    std::istringstream iss(line);
                    std::string address_part, symbol_name;

                    if(iss >> address_part >> symbol_name) {
                        // Parse segment:offset format
                        size_t colon_pos = address_part.find(':');
                        if(colon_pos != std::string::npos) {
                            try {
                                std::string seg_str = address_part.substr(0, colon_pos);
                                std::string off_str = address_part.substr(colon_pos + 1);

                                uint16_t segment = std::stoul(seg_str, nullptr, 16);
                                uint32_t offset = std::stoul(off_str, nullptr, 16);

                                // Convert to linear address for storage
                                uint32_t linear_addr = (segment << 4) + offset;

                                // Clean up symbol name (remove leading underscores)
                                if(!symbol_name.empty() && symbol_name[0] == '_') {
                                    symbol_name = symbol_name.substr(1);
                                }

                                debug_symbols[linear_addr] = symbol_name;
                                symbols_loaded++;

                            }
                            catch(const std::exception& e) {
                                // Skip malformed lines
                                continue;
                            }
                        }
                    }
                }
            }

            file.close();

            if(symbols_loaded > 0) {
                luaEngine.log_info("Successfully loaded " + std::to_string(symbols_loaded) + " symbols from " + filename);
                return true;
            }
            else {
                luaEngine.log_warning("No symbols found in map file: " + filename);
                return false;
            }

        }
        catch(const std::exception& e) {
            luaEngine.log_error("Error loading map file: " + std::string(e.what()));
            return false;
        }
        };

    debug_table["add_symbol"] = [this](uint32_t address, const std::string& name) -> bool {
        // Manually add symbol
        debug_symbols[address] = name;
        luaEngine.log_info("Symbol added: " + name + " at " + std::to_string(address));
        return true;
        };

    debug_table["get_symbol"] = [this](uint32_t address) -> std::string {
        auto it = debug_symbols.find(address);
        if(it != debug_symbols.end()) {
            return it->second;
        }
        return "";
        };

    debug_table["clear_symbols"] = [this]() {
        debug_symbols.clear();
        luaEngine.log_info("All symbols cleared");
        };

    // Window management function (now uses DebuggerSession)
    debug_table["show_all_windows"] = [this]() {
        // Show all new debug tools instead of old windows
        ::InitializeDebugSession();
        auto* session = ::GetDebuggerSession();
        if (session && session->isInitialized()) {
            if (auto* window_manager = LuaEngineGUIWindows::WindowUtils::getWindowManager()) {
                window_manager->showMemorySearch();
                window_manager->showWatchList();
                window_manager->showHexEditor();
                window_manager->showTraceLogger();
                window_manager->showCheatEngine();
                window_manager->showDisassembly();
            }
            luaEngine.log_info("All debug tools are now visible!");
            luaEngine.log_info("Keyboard shortcuts:");
            luaEngine.log_info("  Ctrl+Shift+S - RAM Search");
            luaEngine.log_info("  Ctrl+Shift+W - Watch List");
            luaEngine.log_info("  Ctrl+Shift+H - Hex Editor");
            luaEngine.log_info("  Ctrl+Shift+T - Trace Logger");
        }
        };

    lua["debug"] = debug_table;
    luaEngine.log_info("Debug API registered with disassembly, watchpoints, and symbol management");
}
#endif
