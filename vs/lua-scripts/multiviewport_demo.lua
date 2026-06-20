-- Multi-Viewport Demo - Final Test
print("=== Multi-Viewport Demo (vs/lua-scripts) ===")

-- Enable multi-viewport support
window.enableMultiViewport()
print("✓ Multi-viewport enabled:", window.isMultiViewportEnabled())

-- Get main viewport info
local main_pos = window.getMainViewportPos()
local main_size = window.getMainViewportSize()
print(string.format("Main viewport: %dx%d at (%d,%d)", 
    main_size.width, main_size.height, main_pos.x, main_pos.y))

-- Create multiple detachable windows for professional workflow

-- 1. Memory Monitor Window
local memory_window = {
    title = "Memory Monitor",
    render = function()
        gui.text("🔍 Memory Monitor")
        gui.separator()
        
        -- Show some memory values
        for i = 0, 7 do
            local addr = 0x0040 + i
            local value = memory.readbyte(addr) or 0
            gui.text(string.format("0x%04X: 0x%02X (%d)", addr, value, value))
        end
        
        gui.separator()
        gui.text("Drag to secondary monitor →")
    end
}

-- 2. CPU State Window  
local cpu_window = {
    title = "CPU State",
    render = function()
        gui.text("⚙️ CPU Registers")
        gui.separator()
        
        local regs = cpu.getregs()
        gui.text("AX: " .. string.format("0x%04X", regs.ax))
        gui.text("BX: " .. string.format("0x%04X", regs.bx))
        gui.text("CX: " .. string.format("0x%04X", regs.cx))
        gui.text("DX: " .. string.format("0x%04X", regs.dx))
        
        gui.separator()
        gui.text("CS:IP: " .. string.format("%04X:%04X", regs.cs, regs.ip))
    end
}

-- 3. Performance Monitor Window
local perf_window = {
    title = "Performance Monitor", 
    render = function()
        gui.text("📊 Performance Stats")
        gui.separator()
        
        if performance and performance.get_stats then
            local stats = performance.get_stats()
            gui.text("Memory ops: " .. (stats.memory_operations or 0))
            gui.text("Script execs: " .. (stats.script_executions or 0))
        end
        
        gui.separator()
        if gui.button("Enable Fast Memory") then
            if performance and performance.enable_fast_memory then
                performance.enable_fast_memory(true)
                print("✓ Fast memory enabled")
            end
        end
        
        if gui.button("Test Fast Read") then
            if performance and performance.read_bytes_fast then
                local start = os.clock()
                local data = performance.read_bytes_fast(0x1000, 512)
                local elapsed = (os.clock() - start) * 1000
                print(string.format("Read %d bytes in %.3f ms", #data, elapsed))
            end
        end
    end
}

-- 4. Control Panel Window
local control_window = {
    title = "Multi-Viewport Control",
    render = function()
        gui.text("🎛️ Multi-Viewport Control Panel")
        gui.separator()
        
        gui.text("Status: " .. (window.isMultiViewportEnabled() and "✅ ENABLED" or "❌ DISABLED"))
        
        if gui.button("Toggle Multi-Viewport") then
            if window.isMultiViewportEnabled() then
                window.disableMultiViewport()
                print("Multi-viewport disabled")
            else
                window.enableMultiViewport()
                print("Multi-viewport enabled")
            end
        end
        
        gui.separator()
        gui.text("Instructions:")
        gui.text("1. Drag any window outside main area")
        gui.text("2. Window becomes separate OS window")
        gui.text("3. Move between monitors freely")
        gui.text("4. Perfect for multi-monitor setups!")
        
        gui.separator()
        local pos = window.getMainViewportPos()
        local size = window.getMainViewportSize()
        gui.text(string.format("Main: %dx%d at (%d,%d)", size.width, size.height, pos.x, pos.y))
    end
}

-- Create all windows
local windows = {}
windows.memory = window.createLua("Memory Monitor", memory_window)
windows.cpu = window.createLua("CPU State", cpu_window)
windows.performance = window.createLua("Performance Monitor", perf_window)
windows.control = window.createLua("Multi-Viewport Control", control_window)

print("✅ Created 4 detachable windows:")
for name, id in pairs(windows) do
    print("  " .. name .. ": " .. id)
end

print("\n🎯 INSTRUCTIONS:")
print("1. Look for 4 windows on your DOSBox-X screen")
print("2. Drag any window outside the main DOSBox-X window")
print("3. Watch it become a separate OS window!")
print("4. Perfect for multi-monitor development workflows")

print("\n🎉 Multi-viewport demo ready!")
print("Professional windowing system is now active!")