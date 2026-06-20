-- Check overlay key bindings and try to enable overlay
print("=== Checking Overlay Key Bindings ===")

print("Common DOSBox-X overlay keys to try:")
print("  Ctrl+F1  - Mapper (should show ImGui)")
print("  Ctrl+F5  - Screenshot")
print("  Ctrl+F11 - Speed control")
print("  Ctrl+F12 - Speed control")
print("  F11      - Fullscreen toggle")
print("  F12      - Screenshot")

print("\nTry pressing these keys to see if ImGui overlays appear!")
print("If any of these show ImGui windows, then ImGui is working")
print("and we just need to enable our custom overlay.")

-- Create a simple test that should definitely work
print("\nCreating simple overlay test...")

-- Force overlay to be visible
gui.text("OVERLAY TEST - PRESS CTRL+F1 TO SEE IF IMGUI WORKS")

-- Create a window that logs when it renders
local test_window = {
    render = function()
        print("⚡ Window render called at", os.clock())
        gui.text("Window is rendering!")
        gui.text("Time: " .. os.clock())
    end
}

local window_id = window.createLua("Render Test", test_window)
print("✓ Test window created:", window_id)

print("\n=== Instructions ===")
print("1. Try pressing Ctrl+F1 in DOSBox-X")
print("2. If you see the mapper, ImGui is working")
print("3. Look for 'Window render called' messages in console")
print("4. If render messages appear, windows work but aren't visible")

-- Also try to access the overlay manager directly
print("\nTrying to access overlay manager...")
if _G.LuaEngineGUI then
    print("Found LuaEngineGUI global")
    if _G.LuaEngineGUI.g_overlay_manager then
        print("Found overlay manager")
        -- Try to enable it
        _G.LuaEngineGUI.g_overlay_manager.setEnabled(true)
        print("Attempted to enable overlay")
    end
end

print("\n=== Test Complete ===")
print("Check console for render messages and try overlay keys!")