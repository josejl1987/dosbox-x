-- Discover what GUI functions are actually available
print("=== Discovering GUI API ===")

-- Check what's in the gui table
if gui then
    print("GUI API is available. Functions:")
    for k, v in pairs(gui) do
        print("  gui." .. k .. " = " .. type(v))
    end
    
    print("\nTrying different GUI function calls...")
    
    -- Try different ways to call GUI functions
    local success, error = pcall(function()
        gui.drawText(10, 10, "Test text")
    end)
    if success then
        print("✓ gui.drawText(x, y, text) works")
    else
        print("❌ gui.drawText(x, y, text) failed:", error)
    end
    
    -- Try with color
    success, error = pcall(function()
        gui.drawText(10, 30, "Test text", "white")
    end)
    if success then
        print("✓ gui.drawText(x, y, text, color) works")
    else
        print("❌ gui.drawText(x, y, text, color) failed:", error)
    end
    
    -- Try text function with coordinates
    success, error = pcall(function()
        gui.text(10, 10, "Test text")
    end)
    if success then
        print("✓ gui.text(x, y, text) works")
    else
        print("❌ gui.text(x, y, text) failed:", error)
    end
    
    -- Try just text
    success, error = pcall(function()
        gui.text("Test text")
    end)
    if success then
        print("✓ gui.text(text) works")
    else
        print("❌ gui.text(text) failed:", error)
    end
    
else
    print("❌ GUI API not available")
end

-- Check window API
if window then
    print("\nWindow API is available. Functions:")
    for k, v in pairs(window) do
        print("  window." .. k .. " = " .. type(v))
    end
    
    -- Try to create a minimal window
    print("\nTrying to create minimal window...")
    local success, error = pcall(function()
        local test_window = {
            render = function()
                print("📍 Minimal window render called")
                -- Don't use any GUI functions that might fail
            end
        }
        local id = window.createLua("Minimal", test_window)
        print("✓ Minimal window created:", id)
    end)
    
    if not success then
        print("❌ Window creation failed:", error)
    end
else
    print("❌ Window API not available")
end

print("\n=== Discovery Complete ===")
print("Check the function signatures above to see what works")