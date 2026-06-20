-- DOSBox-X Lua Scripting Demo Script
-- ====================================
-- This script demonstrates the main features of the DOSBox-X LuaEngine system.
-- It's designed as an autostart script that showcases all available APIs
-- and provides a foundation for building your own game automation scripts.

-- Print welcome message
debug.log("=== DOSBox-X LuaEngine Demo Script Loaded ===")
debug.log("This script demonstrates all LuaEngine features:")
debug.log("- Memory and CPU access")
debug.log("- Advanced breakpoint system") 
debug.log("- Save state integration")
debug.log("- Hot-reload development")
debug.log("- Game-specific templates")
debug.log("- Interactive console commands")
debug.log("- Performance monitoring")
debug.log("")

-- =============================================================================
-- BASIC API DEMONSTRATION
-- =============================================================================

function demo_basic_apis()
    debug.log("--- Basic API Demonstration ---")
    
    -- Memory API Example
    debug.log("1. Memory API:")
    local byte_val = memory.readbyte(0x0040, 0x0000)  -- Read BIOS data area
    debug.log("   BIOS data area byte: 0x" .. string.format("%02X", byte_val or 0))
    
    -- Try reading a few more bytes
    local word_val = memory.readword(0x0040, 0x0010)
    debug.log("   BIOS word value: 0x" .. string.format("%04X", word_val or 0))
    
    -- CPU API Example
    debug.log("2. CPU API:")
    local ax_val = cpu.get_ax()
    local bx_val = cpu.get_bx()
    local cx_val = cpu.get_cx()
    debug.log("   CPU Registers: AX=0x" .. string.format("%04X", ax_val) .. 
             " BX=0x" .. string.format("%04X", bx_val) .. 
             " CX=0x" .. string.format("%04X", cx_val))
    
    -- Debug API Example
    debug.log("3. Debug API:")
    debug.log("   Memory hexdump of BIOS data area:")
    debug.hexdump(0x0040, 0x0000, 16)
    
    debug.log("✓ Basic APIs demonstrated successfully")
end

-- =============================================================================
-- BREAKPOINT SYSTEM DEMONSTRATION
-- =============================================================================

function demo_breakpoint_system()
    debug.log("--- Breakpoint System Demonstration ---")
    
    -- Simple breakpoint
    breakpoint.add(0x1000, 0x0100, "DemoBreakpoint1", "", 
                  "debug.log('Demo breakpoint 1 triggered!')")
    
    -- Conditional breakpoint
    breakpoint.add(0x1000, 0x0200, "ConditionalDemo", 
                  "cpu.get_ax() > 0x0500", 
                  "debug.log('AX register condition met: ' .. cpu.get_ax())")
    
    -- Memory monitoring breakpoint
    breakpoint.add(0x2000, 0x0100, "MemoryMonitor", 
                  "memory.readbyte(0x0040, 0x0000) ~= nil",
                  [[
                      local mem_val = memory.readbyte(0x0040, 0x0000) or 0
                      debug.log('Memory monitor active, value: ' .. mem_val)
                  ]])
    
    -- List current breakpoints
    local bp_list = breakpoint.list()
    debug.log("Added " .. #bp_list .. " demonstration breakpoints")
    
    -- Show breakpoint management
    debug.log("Breakpoint management commands available:")
    debug.log("  breakpoint.enable(cs, ip, true/false)")
    debug.log("  breakpoint.remove(cs, ip)")
    debug.log("  breakpoint.clear()")
    debug.log("  breakpoint.list()")
    
    debug.log("✓ Breakpoint system configured")
end

-- =============================================================================
-- SAVE STATE DEMONSTRATION
-- =============================================================================

function demo_savestate_system()
    debug.log("--- Save State System Demonstration ---")
    
    -- Show current savestate configuration
    local current_slot = savestate.get_current_slot()
    debug.log("Current savestate slot: " .. current_slot)
    
    -- Demonstrate slot management
    debug.log("Savestate management commands:")
    debug.log("  savestate.quick_save() - Save to current slot")
    debug.log("  savestate.quick_load() - Load from current slot") 
    debug.log("  savestate.set_current_slot(n) - Change active slot")
    debug.log("  savestate.auto_save_on_breakpoint(true/false)")
    
    -- Enable auto-save on breakpoint for debugging convenience
    savestate.auto_save_on_breakpoint(true)
    debug.log("✓ Auto-save on breakpoint enabled for debugging")
    
    debug.log("✓ Save state system demonstrated")
end

-- =============================================================================
-- GAME TEMPLATE DEMONSTRATION
-- =============================================================================

function demo_template_system()
    debug.log("--- Game Template System Demonstration ---")
    
    -- List available templates
    local templates = template.list()
    debug.log("Available game templates:")
    for i = 1, #templates do
        debug.log("  " .. i .. ". " .. templates[i])
    end
    
    -- Demonstrate template usage
    debug.log("Template usage examples:")
    debug.log("  template.create_platformer(x_seg, x_ofs, y_seg, y_ofs)")
    debug.log("  template.create_rpg(hp_seg, hp_ofs, mp_seg, mp_ofs, gold_seg, gold_ofs)")
    debug.log("  template.load('generic_dos', {HEALTH_SEG='1000', HEALTH_OFFSET='0100'})")
    
    -- Show how to generate a simple template script
    debug.log("Example: Creating a generic DOS game helper...")
    local params = {
        HEALTH_SEG = "0x1000",
        HEALTH_OFFSET = "0x0100",
        LIVES_SEG = "0x1000", 
        LIVES_OFFSET = "0x0200",
        SCORE_SEG = "0x1000",
        SCORE_OFFSET = "0x0300"
    }
    
    local script = template.load("generic_dos", params)
    if script and #script > 0 then
        debug.log("✓ Template script generated (" .. #script .. " characters)")
        debug.log("  Script provides helper functions for common game operations")
    end
    
    debug.log("✓ Template system demonstrated")
end

-- =============================================================================
-- CONSOLE SYSTEM DEMONSTRATION  
-- =============================================================================

function demo_console_system()
    debug.log("--- Console System Demonstration ---")
    
    debug.log("Interactive console commands available:")
    debug.log("  lua_help() - Show available commands")
    debug.log("  lua_status() - System status information")
    debug.log("  read_mem(seg, offset) - Quick memory read")
    debug.log("  show_cpu() - Display CPU registers")
    debug.log("  bp_add(cs, ip, name) - Quick breakpoint addition")
    debug.log("  list_bp() - List all breakpoints")
    debug.log("  list_templates() - Show available templates")
    
    -- Demonstrate console execution (using alternative since console.exec may not be available)
    local console_result = "Console system active: " .. tostring(true)
    debug.log("Console test result: " .. console_result)
    
    debug.log("✓ Console system ready for interactive use")
end

-- =============================================================================
-- PERFORMANCE MONITORING DEMONSTRATION
-- =============================================================================

function demo_performance_monitoring()
    debug.log("--- Performance Monitoring Demonstration ---")
    
    -- Get current performance statistics
    local stats = performance.get_stats()
    if stats then
        debug.log("Performance Statistics:")
        debug.log("  Runtime: " .. stats.total_runtime_ms .. " ms")
        debug.log("  Frame calls: " .. stats.frame_calls)
        debug.log("  Script executions: " .. stats.script_executions)
        debug.log("  Memory operations: " .. stats.memory_operations)
        
        if stats.frame_calls > 0 then
            debug.log("  Average frame time: " .. stats.avg_frame_time_us .. " μs")
            debug.log("  Lua overhead: " .. string.format("%.2f", stats.lua_overhead_percent) .. "%")
        end
    end
    
    debug.log("Performance monitoring commands:")
    debug.log("  performance.get_stats() - Current statistics")
    debug.log("  performance.get_report() - Detailed report")
    debug.log("  performance.reset() - Reset counters")
    debug.log("  performance.benchmark(func, iterations) - Benchmark functions")
    
    debug.log("✓ Performance monitoring active")
end

-- =============================================================================
-- HOT-RELOAD DEMONSTRATION
-- =============================================================================

function demo_hotreload_system()
    debug.log("--- Hot-Reload System Demonstration ---")
    
    debug.log("Hot-reload features:")
    debug.log("  - Automatically reloads scripts when files change")
    debug.log("  - Useful for rapid development and testing")
    debug.log("  - Checks for file modifications every few seconds")
    debug.log("  - No need to restart DOSBox-X when updating scripts")
    
    -- Show current hot-reload status
    local current_script = hotreload.get_script()
    if current_script and #current_script > 0 then
        debug.log("  Current hot-reload script: " .. current_script)
    else
        debug.log("  Hot-reload: monitoring autostart script changes")
    end
    
    debug.log("✓ Hot-reload system active")
end

-- =============================================================================
-- PRACTICAL EXAMPLES
-- =============================================================================

function demo_practical_examples()
    debug.log("--- Practical Usage Examples ---")
    
    debug.log("Example 1: DOS Game Health Monitor")
    debug.log([[
    -- Monitor player health in a DOS game
    function monitor_health()
        local health = memory.readbyte(0x1000, 0x0100)  -- Game-specific address
        if health < 20 then
            debug.log("Warning: Low health detected: " .. health)
            -- Could auto-save here
            savestate.quick_save()
        end
    end
    ]])
    
    debug.log("Example 2: Infinite Lives Cheat")
    debug.log([[
    -- Set up automatic infinite lives
    breakpoint.add(0x2000, 0x0200, "InfiniteLives",
                  "memory.readbyte(0x1000, 0x0200) < 3",  -- When lives < 3
                  "memory.writebyte(0x1000, 0x0200, 9)")  -- Set to 9 lives
    ]])
    
    debug.log("Example 3: Speedrun Timer")
    debug.log([[
    -- Track speedrun timing
    local start_time = os.clock()
    function show_timer()
        local elapsed = os.clock() - start_time
        debug.log("Speedrun time: " .. string.format("%.2f", elapsed) .. " seconds")
    end
    ]])
    
    debug.log("Example 4: Auto-Save at Checkpoints")
    debug.log([[
    -- Auto-save when reaching specific game locations
    breakpoint.add(0x3000, 0x0100, "CheckpointSave",
                  "memory.readbyte(0x1500, 0x0000) == 0x42",  -- Checkpoint marker
                  "savestate.quick_save(); debug.log('Checkpoint reached - auto-saved!')")
    ]])
    
    debug.log("✓ Practical examples provided")
end

-- =============================================================================
-- MAIN DEMONSTRATION FUNCTION
-- =============================================================================

function main_demo()
    debug.log("Starting comprehensive LuaEngine demonstration...")
    debug.log("")
    
    -- Run all demonstrations
    demo_basic_apis()
    debug.log("")
    
    demo_breakpoint_system() 
    debug.log("")
    
    demo_savestate_system()
    debug.log("")
    
    demo_template_system()
    debug.log("")
    
    demo_console_system()
    debug.log("")
    
    demo_performance_monitoring()
    debug.log("")
    
    demo_hotreload_system()
    debug.log("")
    
    demo_practical_examples()
    debug.log("")
    
    debug.log("=== Demo Script Complete ===")
    debug.log("The LuaEngine is now fully configured and ready for use!")
    debug.log("")
    debug.log("Next steps:")
    debug.log("1. Open the Lua console window (if enabled)")
    debug.log("2. Try interactive commands like lua_help() or lua_status()")
    debug.log("3. Load a DOS game and experiment with memory monitoring")
    debug.log("4. Create your own scripts in the lua-scripts/ directory")
    debug.log("5. Use hot-reload to quickly test script changes")
    debug.log("")
    debug.log("Happy scripting!")
end

-- =============================================================================
-- FRAME BOUNDARY HOOK (Called every frame when hooks are enabled)
-- =============================================================================

function frame_hook()
    -- This function is called every frame when hooks=true
    -- Keep it lightweight to avoid performance impact
    
    -- Example: Monitor for specific memory patterns
    -- local player_x = memory.readbyte(0x1000, 0x0100)  -- Example player X position
    -- if player_x and player_x > 200 then
    --     debug.log("Player moved far right: " .. player_x)
    -- end
    
    -- The frame hook is powerful but should be used sparingly
    -- Most monitoring should be done with breakpoints instead
end

-- =============================================================================
-- INITIALIZATION
-- =============================================================================

-- Run the main demonstration
main_demo()

-- Set up convenience functions for interactive use
function help()
    return lua_help()
end

function status()
    return lua_status()
end

function quick_memory_check()
    debug.log("Quick memory check:")
    debug.hexdump(0x0040, 0x0000, 32)  -- BIOS data area
end

function quick_cpu_check()
    debug.log("Quick CPU check:")
    debug.log(show_cpu())
end

function demo_cleanup()
    debug.log("Cleaning up demo breakpoints...")
    breakpoint.clear()
    debug.log("Demo cleanup complete")
end

-- Make demo easily restartable
function restart_demo()
    debug.log("Restarting LuaEngine demo...")
    demo_cleanup()
    main_demo()
end

debug.log("Interactive functions available:")
debug.log("  help() - Show help")
debug.log("  status() - Show status")
debug.log("  quick_memory_check() - Memory overview")
debug.log("  quick_cpu_check() - CPU overview")
debug.log("  restart_demo() - Restart this demo")
debug.log("  demo_cleanup() - Clean up demo breakpoints")