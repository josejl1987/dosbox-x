-- Debug if the main render loop is calling our overlay
print("=== Debugging Main Render Loop ===")

-- The issue is that our ImGui overlay should render on TOP of DOS screen
-- regardless of DOS activity. Let's check if the overlay manager exists
-- and if we can access it from Lua

print("Checking overlay manager accessibility...")

-- Check if we can access the overlay manager
local overlay_manager_found = false
if LuaEngineGUI then
    print("✓ LuaEngineGUI namespace exists")
    
    if LuaEngineGUI.g_overlay_manager then
        print("✓ g_overlay_manager exists")
        overlay_manager_found = true
        
        local manager = LuaEngineGUI.g_overlay_manager
        
        -- Check if we can call methods on it
        local methods_to_test = {
            "render",
            "setEnabled", 
            "isEnabled",
            "setScreenSize"
        }
        
        for _, method in ipairs(methods_to_test) do
            if manager[method] then
                print("✓ Manager has", method, "method")
            else
                print("❌ Manager missing", method, "method")
            end
        end
        
        -- Try to check if overlay is enabled
        local success, enabled = pcall(function()
            return manager:isEnabled()
        end)
        if success then
            print("Overlay manager enabled:", enabled)
            if not enabled then
                print("Trying to enable overlay manager...")
                local enable_success = pcall(function()
                    manager:setEnabled(true)
                end)
                if enable_success then
                    print("✓ Overlay manager enabled")
                else
                    print("❌ Failed to enable overlay manager")
                end
            end
        else
            print("Could not check overlay manager status")
        end
        
    else
        print("❌ g_overlay_manager not found in LuaEngineGUI")
    end
else
    print("❌ LuaEngineGUI namespace not found")
end

-- Check if the GUI API is working
print("\nTesting GUI API...")
if gui then
    print("✓ GUI API available")
    
    -- Test if overlay is enabled
    local overlay_enabled = gui.is_overlay_enabled()
    print("GUI overlay enabled:", overlay_enabled)
    
    if not overlay_enabled then
        print("Enabling GUI overlay...")
        gui.enable_overlay(true)
        overlay_enabled = gui.is_overlay_enabled()
        print("GUI overlay enabled after enable:", overlay_enabled)
    end
    
    -- Try to draw something
    print("Drawing test content...")
    gui.clear_all()
    gui.text(50, 50, "MAIN RENDER LOOP TEST")
    gui.drawrect(50, 70, 200, 30, "red")
    gui.text(60, 80, "This should be visible")
    
else
    print("❌ GUI API not available")
end

-- Test window creation
print("\nTesting window creation...")
if window then
    print("✓ Window API available")
    
    local debug_window = {
        render = function()
            print("🔥 DEBUG WINDOW RENDER - This should appear every frame!")
        end
    }
    
    local window_id = window.createLua("Debug Render", debug_window)
    print("✓ Debug window created:", window_id)
    
    -- Enable multi-viewport
    window.enableMultiViewport()
    print("✓ Multi-viewport enabled")
    
else
    print("❌ Window API not available")
end

print("\n=== Main Render Loop Debug Complete ===")
print("The ImGui overlay should render on top of DOS screen")
print("If you don't see render messages, the main loop integration is broken")
print("Check if render.cpp is calling LuaEngineGUI::g_overlay_manager->render()")