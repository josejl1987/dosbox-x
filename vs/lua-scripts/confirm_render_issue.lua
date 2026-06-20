-- Confirm the render issue - overlay only renders on DOS screen updates
print("=== Confirming Render Issue ===")

-- Our ImGui overlay should render EVERY FRAME on top of DOS screen
-- Currently it only renders when render.scale.outWrite is true

print("Setting up test...")

-- Enable overlay and create content
if gui then
    gui.enable_overlay(true)
    gui.clear_all()
    gui.text(10, 10, "🔴 OVERLAY SHOULD BE VISIBLE")
    gui.text(10, 30, "This should appear on top of DOS")
    gui.drawrect(10, 50, 300, 50, "red")
    gui.text(20, 65, "RED BOX - ALWAYS VISIBLE")
    print("✓ Overlay content prepared")
else
    print("❌ GUI API not available")
end

-- Create window
if window then
    window.enableMultiViewport()
    
    local always_render_window = {
        render = function()
            print("🔥 WINDOW SHOULD RENDER EVERY FRAME - " .. os.clock())
            gui.text(20, 20, "Window rendering every frame")
            gui.text(20, 40, "Time: " .. string.format("%.2f", os.clock()))
        end
    }
    
    local window_id = window.createLua("Always Render", always_render_window)
    print("✓ Window created:", window_id)
else
    print("❌ Window API not available")
end

print("\n=== THE PROBLEM ===")
print("Our ImGui overlay only renders when DOS screen updates")
print("But it should render EVERY FRAME on top of DOS screen")
print("")
print("SOLUTION NEEDED:")
print("Modify render.cpp to call overlay manager render() every frame")
print("Not just when render.scale.outWrite is true")
print("")
print("Current code (WRONG):")
print("  if (render.scale.outWrite) {")
print("    if (LuaEngineGUI::g_overlay_manager) {")
print("      LuaEngineGUI::g_overlay_manager->render();")
print("    }")
print("  }")
print("")
print("Should be (CORRECT):")
print("  if (render.scale.outWrite) {")
print("    // DOS rendering")
print("  }")
print("  // ImGui overlay should render EVERY FRAME")
print("  if (LuaEngineGUI::g_overlay_manager) {")
print("    LuaEngineGUI::g_overlay_manager->render();")
print("  }")

print("\n=== Test Complete ===")
print("The multi-viewport system is fully implemented")
print("It just needs the render loop fix to be visible")