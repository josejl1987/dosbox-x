-- Check what might be preventing overlay rendering
print("=== Checking Render Conditions ===")

-- The render.cpp code shows:
-- if (LuaEngineGUI::g_overlay_manager) {
--     LuaEngineGUI::g_overlay_manager->setScreenSize(render.src.width, render.src.height);
--     LuaEngineGUI::g_overlay_manager->render();
-- }

print("Checking if overlay manager exists...")

-- Try to access the overlay manager
if LuaEngineGUI then
    print("✓ LuaEngineGUI namespace exists")
    
    if LuaEngineGUI.g_overlay_manager then
        print("✓ g_overlay_manager exists")
        
        -- Try to get info about it
        local manager = LuaEngineGUI.g_overlay_manager
        print("Manager type:", type(manager))
        
        -- Try to call methods on it
        local success, error = pcall(function()
            manager:setScreenSize(640, 480)
            print("✓ setScreenSize called successfully")
        end)
        if not success then
            print("❌ setScreenSize failed:", error)
        end
        
        success, error = pcall(function()
            manager:render()
            print("✓ render called successfully")
        end)
        if not success then
            print("❌ render failed:", error)
        end
        
        -- Check if overlay is enabled on the manager
        success, error = pcall(function()
            local enabled = manager:isEnabled()
            print("Manager enabled:", enabled)
        end)
        if not success then
            print("Could not check manager enabled status:", error)
        end
        
    else
        print("❌ g_overlay_manager does not exist")
        print("Available in LuaEngineGUI:")
        for k, v in pairs(LuaEngineGUI) do
            print("  " .. k .. ":", type(v))
        end
    end
else
    print("❌ LuaEngineGUI namespace does not exist")
    
    -- Check what global namespaces do exist
    print("Available global namespaces:")
    for k, v in pairs(_G) do
        if type(v) == "table" and string.find(k, "Lua") then
            print("  " .. k .. ":", type(v))
        end
    end
end

-- Also check if the GUI overlay is actually being rendered but not visible
print("\nTesting overlay visibility...")
if gui then
    gui.enable_overlay(true)
    gui.clear_all()
    
    -- Draw something very obvious
    gui.drawrect(0, 0, 100, 100, "red")
    gui.text(10, 10, "BIG RED BOX")
    
    -- Try different positions
    gui.drawrect(200, 200, 100, 100, "yellow")
    gui.text(210, 210, "YELLOW BOX")
    
    print("✓ Drew obvious overlay elements")
    print("If you don't see red and yellow boxes, overlay isn't rendering")
end

print("\n=== Render Condition Check Complete ===")
print("This should help identify why overlay isn't visible")