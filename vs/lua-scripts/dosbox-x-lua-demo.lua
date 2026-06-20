-- ========================================================================
-- DOSBox-X Lua Engine Comprehensive Demo Script (refined)
-- ========================================================================

local Demo = {}

-- ------------------------------------------------------------------------
-- CONFIGURATION
-- ------------------------------------------------------------------------

local config = {
    enable_memory_demo      = true,
    enable_cpu_demo         = true,
    enable_debug_demo       = true,
    enable_event_demo       = true,
    enable_gui_demo         = true,
    enable_tas_demo         = true,
    enable_savestate_demo   = true,
    enable_breakpoint_demo  = true,
    enable_performance_demo = true,
    enable_hotreload_demo   = true,
    enable_advanced_demo    = true,

    -- Logging
    log_every_n_frames      = 60,
    gui_update_divisor      = 4,

    -- Safety / intrusiveness
    touch_real_memory       = true,   -- if false, skip writes and callbacks that alter state
    install_breakpoints     = true,
    install_tas_watches     = true,
}

-- ------------------------------------------------------------------------
-- CAPABILITY DETECTION
-- ------------------------------------------------------------------------

local caps = {
    memory      = type(memory)      == "table",
    cpu         = type(cpu)         == "table",
    debug       = type(debug)       == "table",
    event       = type(event)       == "table",
    gui         = type(gui)         == "table",
    tas         = type(tas)         == "table",
    savestate   = type(savestate)   == "table",
    breakpoint  = type(breakpoint)  == "table",
    performance = type(performance) == "table",
    frame       = type(frame)       == "table",
    hotreload   = type(hotreload)   == "table",
    console     = type(console)     == "table",
    template    = type(template)    == "table",
    network     = type(network)     == "table",
    sound       = type(sound)       == "table",
}

-- ------------------------------------------------------------------------
-- GLOBAL DEMO STATE
-- ------------------------------------------------------------------------

local state = {
    frame_count      = 0,
    events_registered = {},
    breakpoints       = {},
    tas_recording     = false,
}

-- ------------------------------------------------------------------------
-- LOGGING HELPERS
-- ------------------------------------------------------------------------

local function raw_log(msg)
    if caps.debug and debug.log then
        debug.log(msg)
    else
        print(msg)
    end
end

local function log(level, message)
    level = level or "info"
    raw_log(string.format("[LuaDemo][%s][Frame %d] %s", level, state.frame_count, message))
end

local function info(message)  log("info",  message) end
local function warn(message)  log("warn",  message) end
local function errorf(message) log("error", message) end

local function format_address(seg, offset)
    return string.format("%04X:%04X", seg or 0, offset or 0)
end

local function format_registers()
    if not caps.cpu then
        return "<CPU API not available>"
    end
    return string.format(
        "AX=%04X BX=%04X CX=%04X DX=%04X CS=%04X IP=%04X",
        cpu.get_ax(), cpu.get_bx(), cpu.get_cx(), cpu.get_dx(),
        cpu.get_cs(), cpu.get_ip()
    )
end

local function rgb(r, g, b, a)
    if not caps.gui or not gui.color_from_rgb then
        return nil
    end
    return gui.color_from_rgb(r, g, b, a or 255)
end

-- ------------------------------------------------------------------------
-- FAST MODE INITIALIZATION
-- ------------------------------------------------------------------------

local function enable_fast_modes()
    if caps.memory and memory.set_fast_mode then memory.set_fast_mode(true) end
    if caps.cpu    and cpu.set_fast_mode    then cpu.set_fast_mode(true)    end
    if caps.debug  and debug.set_fast_mode  then debug.set_fast_mode(true)  end
    if caps.breakpoint and breakpoint.set_fast_mode then breakpoint.set_fast_mode(true) end
    if caps.event  and event.setperformancemode then event.setperformancemode(true) end
end

-- ------------------------------------------------------------------------
-- DEMO SECTIONS  (each checks caps + config and fails soft)
-- ------------------------------------------------------------------------

local function demo_memory_api()
    if not (config.enable_memory_demo and caps.memory and caps.debug) then
        info("Skipping MEMORY demo (API or config disabled)")
        return
    end

    info("=== MEMORY API DEMO ===")
    local seg, ofs = 0x0040, 0x0000   -- BIOS data area

    local byte_value = memory.readbyte(seg, ofs)
    if byte_value then
        info(string.format("Read byte from %s: 0x%02X", format_address(seg, ofs), byte_value))
        if config.touch_real_memory and memory.writebyte then
            if memory.writebyte(seg, ofs + 1, 0xAB) then
                local written = memory.readbyte(seg, ofs + 1) or 0
                info(string.format("Wrote 0xAB, read back: 0x%02X", written))
            end
        end
    end

    if memory.readword then
        local w = memory.readword(seg, ofs)
        if w then
            info(string.format("Read word from %s: 0x%04X", format_address(seg, ofs), w))
        end
    end

    if memory.read then
        local bulk = memory.read(seg, ofs, 16)
        if bulk then
            local hex = {}
            for i = 1, math.min(8, #bulk) do
                hex[#hex + 1] = string.format("%02X", bulk[i])
            end
            info("Bulk read (8 bytes): " .. table.concat(hex, " "))
        end
    end

    if debug.hexdump then
        debug.hexdump(seg, ofs, 32)
    end

    if caps.event and event.onmemoryread and config.touch_real_memory then
        local id = event.onmemoryread(0x40000, 0x40010, function(ev)
            if ev and ev.address then
                info(string.format("Memory read at 0x%08X: value=0x%02X", ev.address, ev.value or 0))
            end
        end)
        table.insert(state.events_registered, id)
        info(string.format("Registered memory read callback (ID: %d)", id))
    end
end

local function demo_cpu_api()
    if not (config.enable_cpu_demo and caps.cpu) then
        info("Skipping CPU demo (API or config disabled)")
        return
    end

    info("=== CPU API DEMO ===")
    info("16-bit Registers:")
    info(format_registers())

    if cpu.get_al then
        info(string.format(
            "8-bit Registers: AL=%02X AH=%02X BL=%02X BH=%02X CL=%02X CH=%02X DL=%02X DH=%02X",
            cpu.get_al(), cpu.get_ah(), cpu.get_bl(), cpu.get_bh(),
            cpu.get_cl(), cpu.get_ch(), cpu.get_dl(), cpu.get_dh()
        ))
    end

    if cpu.get_ds then
        info(string.format(
            "Segment Registers: CS=%04X DS=%04X ES=%04X SS=%04X FS=%04X GS=%04X",
            cpu.get_cs(), cpu.get_ds(), cpu.get_es(), cpu.get_ss(),
            cpu.get_fs and cpu.get_fs() or 0,
            cpu.get_gs and cpu.get_gs() or 0
        ))
    end

    if caps.debug and debug.get_current_address then
        local addr = debug.get_current_address()
        info(string.format("Current Address: CS=%04X IP=%04X",
            addr.cs or 0, addr.ip or 0))
    end

    if cpu.set_dx and config.touch_real_memory then
        local orig_dx = cpu.get_dx()
        cpu.set_dx(0x1234)
        info(string.format("Set DX=0x1234, verify: DX=%04X", cpu.get_dx()))
        cpu.set_dx(orig_dx)
    end

    if cpu.get_flags then
        info(string.format("CPU Flags: 0x%04X", cpu.get_flags()))
    end
end

local function demo_debug_api()
    if not (config.enable_debug_demo and caps.debug) then
        info("Skipping DEBUG demo (API or config disabled)")
        return
    end

    info("=== DEBUG API DEMO ===")
    debug.log("This is a standard debug message")
    if debug.print then debug.print("This is a print statement (alias for log)") end

    if debug.get_current_address then
        local addr = debug.get_current_address()
        info(string.format("Current execution: %04X:%04X", addr.cs or 0, addr.ip or 0))
        if debug.hexdump and addr.cs and addr.ip then
            info("Memory around current IP:")
            debug.hexdump(addr.cs, addr.ip, 16)
        end
    end

    if debug.set_fast_mode then
        debug.set_fast_mode(true)
        info("Debug fast mode enabled for better performance")
    end
end

-- (Event/GUI/TAS/etc. sections would be refactored in the same pattern:
--  check config + caps at the top, then do guarded work.)

-- ------------------------------------------------------------------------
-- CLEANUP
-- ------------------------------------------------------------------------

function Demo.cleanup()
    info("=== CLEANUP ===")

    if caps.event and event.unregister then
        for _, id in ipairs(state.events_registered) do
            event.unregister(id)
        end
        info(string.format("Unregistered %d event callbacks", #state.events_registered))
    end

    if caps.breakpoint and breakpoint.remove then
        for _, bp in ipairs(state.breakpoints) do
            breakpoint.remove(bp.cs, bp.ip)
        end
        info(string.format("Removed %d demo breakpoints", #state.breakpoints))
    end

    if state.tas_recording and caps.tas and tas.stoprecording then
        tas.stoprecording()
        info("Stopped TAS recording")
    end

    if caps.gui and gui.clear_all then
        gui.clear_all()
    end

    if caps.event and event.flushmemoryevents then
        event.flushmemoryevents()
    end

    info("Demo cleanup completed")
end

-- ------------------------------------------------------------------------
-- MAIN ENTRY
-- ------------------------------------------------------------------------

function Demo.run()
    print("========================================================================")
    print("DOSBox-X Lua Engine Comprehensive Demo (refined)")
    print("========================================================================")

    enable_fast_modes()

    if config.enable_memory_demo      then demo_memory_api()      end
    if config.enable_cpu_demo         then demo_cpu_api()         end
    if config.enable_debug_demo       then demo_debug_api()       end
    -- call the other demo_* functions here once they are refactored similarly

  -- Open disassembly view via WindowManager
	if window and window.showDisassembly then
        window.showDisassembly()
        log("Opened disassembly view")
	else
        log("Disassembly window API not available; skipping")
	end


    info("All enabled demo sections completed")
end

-- Hook script exit if available
if caps.event and event.onscriptexit then
    event.onscriptexit(function() Demo.cleanup() end)
end

Demo.run()




_G.dosbox_demo = {
    state   = state,
    config  = config,
    caps    = caps,
    cleanup = Demo.cleanup,
}

return Demo
