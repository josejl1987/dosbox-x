-- Test direct ImGui access to bypass overlay manager
print("=== Direct ImGui Test ===")

-- Try to access ImGui directly to see if it's working
print("Checking direct ImGui access...")

-- Check what's available in global scope
local imgui_found = false
for k, v in pairs(_G) do
    if string.find(string.lower(k), "imgui") then
        print("Found ImGui-related global:", k, type(v))
        imgui_found = true
    end
end

if not imgui_found then
    print("No ImGui globals found")
end

-- Try the window system but with direct ImGui calls
if window then
    print("Creating window with direct ImGui calls...")
    
    local direct_window = {
        render = function()
            print("🔥 Direct ImGui window render")
            
            -- Try to call ImGui functions directly
            local success, error = pcall(function()
                -- These should work if ImGui is properly loaded
                if _G.ImGui then
                    _G.ImGui.Text("Direct ImGui Text")
                    if _G.ImGui.Button("Direct Button") then
                        print("Direct ImGui button clicked!")
                    end
                elseif imgui then
                    imgui.Text("imgui namespace text")
                    if imgui.Button("imgui Button") then
                        print("imgui button clicked!")
                    end
                else
                    print("No direct ImGui access available")
                end
            end)
            
            if not success then
                print("Direct ImGui call failed:", error)
            end
            
            -- Fall back to our GUI API
            gui.text(10, 10, "Fallback GUI text")
        end
    }
    
    local id = window.createLua("Direct ImGui", direct_window)
    print("✓ Direct ImGui window created:", id)
    
    -- Enable multi-viewport
    window.enableMultiViewport()
    print("✓ Multi-viewport enabled")
    
else
    print("❌ Window API not available")
end

-- Check if there's a way to force ImGui rendering
print("\nChecking for render triggers...")

-- Look for any render or update functions
local render_funcs = {}
for k, v in pairs(_G) do
    if type(v) == "function" and (
        string.find(string.lower(k), "render") or
        string.find(string.lower(k), "update") or
        string.find(string.lower(k), "draw")
    ) then
        render_funcs[k] = v
    end
end

print("Found potential render functions:")
for k, v in pairs(render_funcs) do
    print("  " .. k)
end

print("\n=== Direct ImGui Test Complete ===")
print("Check console for render messages")
print("The window system is working - issue is with ImGui visibility")