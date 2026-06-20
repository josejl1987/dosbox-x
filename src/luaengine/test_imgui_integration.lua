-- Test script to verify ImGui UI integration is working properly
-- This script tests all the GUI overlay functions that were fixed

print("=== DOSBox-X Lua ImGui Integration Test ===")

-- Test 1: Basic overlay control
print("Test 1: Testing overlay control functions...")
if gui then
    print("✓ GUI table available")
    
    -- Test overlay enable/disable
    gui.enable_overlay(true)
    local enabled = gui.is_overlay_enabled()
    print("  Overlay enabled:", enabled)
    
    -- Test alpha control
    gui.set_alpha(0.8)
    local alpha = gui.get_alpha()
    print("  Alpha set to:", alpha)
    
    -- Test layer control
    gui.set_layer(1)
    local layer = gui.get_layer()
    print("  Current layer:", layer)
    
    print("✓ Basic overlay control functions working")
else
    print("❌ GUI table not available")
    return
end

-- Test 2: Drawing functions
print("\nTest 2: Testing drawing functions...")
local test_success = true

-- Test text drawing
local text_ok, text_err = pcall(function()
    gui.drawtext(10, 10, "Hello DOSBox-X!", 0xFFFFFFFF)
    gui.drawtext(10, 30, "Colored text", 0xFF00FFFF, "default")
end)
if text_ok then
    print("✓ Text drawing functions working")
else
    print("❌ Text drawing failed:", text_err)
    test_success = false
end

-- Test line drawing
local line_ok, line_err = pcall(function()
    gui.drawline(50, 50, 150, 50, 0xFF0000FF, 2.0)
    gui.drawline(50, 60, 150, 100, 0x00FF00FF)
end)
if line_ok then
    print("✓ Line drawing functions working")
else
    print("❌ Line drawing failed:", line_err)
    test_success = false
end

-- Test shape drawing
local shape_ok, shape_err = pcall(function()
    gui.drawbox(200, 50, 100, 50, 0xFFFFFFFF, 0x80808080)
    gui.drawcircle(300, 100, 25, 0xFF0000FF, 0x80FF0080)
    gui.drawrectangle(350, 75, 80, 40, 0x00FFFFFF)
end)
if shape_ok then
    print("✓ Shape drawing functions working")
else
    print("❌ Shape drawing failed:", shape_err)
    test_success = false
end

-- Test advanced drawing
local advanced_ok, advanced_err = pcall(function()
    gui.drawcrosshair(100, 150, 20, 0xFFFF00FF)
    gui.drawgrid(10, 200, 200, 100, 20, 20, 0x40404040)
    gui.drawpixel(250, 250, 0xFF00FFFF)
end)
if advanced_ok then
    print("✓ Advanced drawing functions working")
else
    print("❌ Advanced drawing failed:", advanced_err)
    test_success = false
end

-- Test polygon drawing
local poly_ok, poly_err = pcall(function()
    local triangle = {300, 200, 350, 250, 250, 250}
    gui.drawpolygon(triangle, 0xFF00FFFF, 0x80FF8080)
end)
if poly_ok then
    print("✓ Polygon drawing functions working")
else
    print("❌ Polygon drawing failed:", poly_err)
    test_success = false
end

-- Test 3: Utility functions
print("\nTest 3: Testing utility functions...")

-- Test screen size
local screen_ok, screen_err = pcall(function()
    local screen = gui.get_screen_size()
    print("  Screen size:", screen.width, "x", screen.height)
end)
if screen_ok then
    print("✓ Screen size function working")
else
    print("❌ Screen size failed:", screen_err)
    test_success = false
end

-- Test text size calculation
local textsize_ok, textsize_err = pcall(function()
    local size = gui.textsize("Test text")
    print("  Text size:", size.width, "x", size.height)
end)
if textsize_ok then
    print("✓ Text size function working")
else
    print("❌ Text size failed:", textsize_err)
    test_success = false
end

-- Test color utility
local color_ok, color_err = pcall(function()
    local red = gui.color(255, 0, 0, 255)
    local green = gui.color(0, 255, 0)
    print("  Color values:", string.format("0x%08X", red), string.format("0x%08X", green))
end)
if color_ok then
    print("✓ Color utility function working")
else
    print("❌ Color utility failed:", color_err)
    test_success = false
end

-- Test 4: Window functions
print("\nTest 4: Testing window functions...")

local window_ok, window_err = pcall(function()
    local created = gui.create_window("Test Window", 400, 300, 200, 150)
    print("  Window created:", created)
    
    if created then
        gui.show_window("Test Window", true)
        local visible = gui.is_window_visible("Test Window")
        print("  Window visible:", visible)
        
        -- Clean up
        gui.destroy_window("Test Window")
        print("  Window destroyed")
    end
end)
if window_ok then
    print("✓ Window functions working")
else
    print("❌ Window functions failed:", window_err)
    test_success = false
end

-- Test 5: Debug and statistics
print("\nTest 5: Testing debug functions...")

local debug_ok, debug_err = pcall(function()
    gui.set_debug_mode(true)
    local stats = gui.get_statistics()
    print("  Statistics:", stats:sub(1, 50) .. "...")
    gui.set_debug_mode(false)
end)
if debug_ok then
    print("✓ Debug functions working")
else
    print("❌ Debug functions failed:", debug_err)
    test_success = false
end

-- Test 6: Format utilities
print("\nTest 6: Testing format utilities...")

local format_ok, format_err = pcall(function()
    local hex = gui.tohex(0xDEADBEEF, 8)
    local dec = gui.todecimal(12345)
    print("  Hex format:", hex)
    print("  Decimal format:", dec)
end)
if format_ok then
    print("✓ Format utilities working")
else
    print("❌ Format utilities failed:", format_err)
    test_success = false
end

-- Final results
print("\n=== Test Results ===")
if test_success then
    print("✅ ALL TESTS PASSED - ImGui UI integration is working properly!")
    print("You should see various shapes, text, and graphics drawn on the screen.")
    print("The overlay system is fully functional.")
else
    print("❌ Some tests failed - check the error messages above")
end

-- Add some persistent visual elements for verification
gui.drawtext(10, 400, "ImGui Integration Test Complete", 0xFFFFFFFF)
gui.drawtext(10, 420, "If you can see this text, the GUI overlay is working!", 0x00FF00FF)
gui.drawbox(5, 395, 500, 50, 0xFFFFFFFF, 0x40000080)

print("\nTest script execution complete. Check the DOSBox-X window for visual confirmation.")
