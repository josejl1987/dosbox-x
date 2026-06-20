-- Cheat System Example
-- =====================
-- This script provides a comprehensive cheat system for DOS games.
-- It includes common cheats, cheat code detection, and safe cheat management.

debug.log("=== Cheat System Example ===")

-- Cheat system state
local cheats_enabled = false
local active_cheats = {}
local cheat_history = {}

-- Configuration: Adjust these addresses for your specific game
local GAME_ADDRESSES = {
    player_health = {seg = 0x1000, offset = 0x0100},
    player_lives = {seg = 0x1000, offset = 0x0200},
    player_score = {seg = 0x1000, offset = 0x0300},
    player_ammo = {seg = 0x1000, offset = 0x0400},
    level_number = {seg = 0x1000, offset = 0x0500},
    player_weapon = {seg = 0x1000, offset = 0x0600},
    game_time = {seg = 0x1000, offset = 0x0700}
}

-- Cheat definitions
local CHEATS = {
    infinite_health = {
        name = "Infinite Health",
        description = "Sets health to maximum when it gets low",
        target_address = GAME_ADDRESSES.player_health,
        condition = function()
            local health = memory.readbyte(GAME_ADDRESSES.player_health.seg, 
                                         GAME_ADDRESSES.player_health.offset) or 0
            return health < 50 -- Activate when health drops below 50
        end,
        action = function()
            memory.writebyte(GAME_ADDRESSES.player_health.seg, 
                           GAME_ADDRESSES.player_health.offset, 100)
            debug.log("🩺 Health restored to 100")
        end
    },
    
    infinite_lives = {
        name = "Infinite Lives", 
        description = "Maintains 9 lives at all times",
        target_address = GAME_ADDRESSES.player_lives,
        condition = function()
            local lives = memory.readbyte(GAME_ADDRESSES.player_lives.seg,
                                        GAME_ADDRESSES.player_lives.offset) or 0
            return lives < 9
        end,
        action = function()
            memory.writebyte(GAME_ADDRESSES.player_lives.seg,
                           GAME_ADDRESSES.player_lives.offset, 9)
            debug.log("❤️ Lives set to 9")
        end
    },
    
    infinite_ammo = {
        name = "Infinite Ammo",
        description = "Keeps ammo at maximum",
        target_address = GAME_ADDRESSES.player_ammo,
        condition = function()
            local ammo = memory.readbyte(GAME_ADDRESSES.player_ammo.seg,
                                       GAME_ADDRESSES.player_ammo.offset) or 0
            return ammo < 99
        end,
        action = function()
            memory.writebyte(GAME_ADDRESSES.player_ammo.seg,
                           GAME_ADDRESSES.player_ammo.offset, 99)
            debug.log("🔫 Ammo refilled to 99")
        end
    },
    
    super_speed = {
        name = "Super Speed",
        description = "Increases game speed temporarily",
        is_toggle = true,
        action = function()
            -- This would need DOSBox-X integration to change CPU cycles
            debug.log("⚡ Super speed activated (not implemented in this example)")
        end,
        deactivate = function()
            debug.log("🐌 Normal speed restored")
        end
    }
}

-- Function to enable cheat system
function enable_cheats()
    if cheats_enabled then
        debug.log("Cheats are already enabled")
        return
    end
    
    cheats_enabled = true
    debug.log("🎮 CHEAT SYSTEM ENABLED 🎮")
    debug.log("Remember: Cheating may affect game balance and achievements!")
    
    -- Create auto-save before enabling cheats
    savestate.quick_save()
    debug.log("💾 Auto-save created before enabling cheats")
end

-- Function to disable cheat system
function disable_cheats()
    if not cheats_enabled then
        debug.log("Cheats are already disabled")
        return
    end
    
    cheats_enabled = false
    
    -- Deactivate all active cheats
    for cheat_name, _ in pairs(active_cheats) do
        deactivate_cheat(cheat_name)
    end
    
    debug.log("🚫 Cheat system disabled")
end

-- Function to activate a specific cheat
function activate_cheat(cheat_name)
    if not cheats_enabled then
        debug.log("Error: Cheat system not enabled. Use enable_cheats() first.")
        return false
    end
    
    local cheat = CHEATS[cheat_name]
    if not cheat then
        debug.log("Error: Unknown cheat '" .. cheat_name .. "'")
        return false
    end
    
    if active_cheats[cheat_name] then
        debug.log("Cheat '" .. cheat.name .. "' is already active")
        return false
    end
    
    active_cheats[cheat_name] = true
    cheat_history[#cheat_history + 1] = {
        name = cheat_name,
        activated_at = os.time(),
        action = "activated"
    }
    
    debug.log("✅ Activated: " .. cheat.name)
    debug.log("   " .. cheat.description)
    
    -- For toggle cheats, execute immediately
    if cheat.is_toggle then
        cheat.action()
    else
        -- For condition-based cheats, set up monitoring
        setup_cheat_monitoring(cheat_name)
    end
    
    return true
end

-- Function to deactivate a specific cheat
function deactivate_cheat(cheat_name)
    local cheat = CHEATS[cheat_name]
    if not cheat or not active_cheats[cheat_name] then
        debug.log("Cheat '" .. cheat_name .. "' is not active")
        return false
    end
    
    active_cheats[cheat_name] = nil
    cheat_history[#cheat_history + 1] = {
        name = cheat_name,
        activated_at = os.time(),
        action = "deactivated"
    }
    
    -- Remove monitoring breakpoint
    if cheat.target_address then
        breakpoint.remove(cheat.target_address.seg, cheat.target_address.offset)
    end
    
    -- Call deactivate function if it exists
    if cheat.deactivate then
        cheat.deactivate()
    end
    
    debug.log("❌ Deactivated: " .. cheat.name)
    return true
end

-- Function to set up monitoring for condition-based cheats
function setup_cheat_monitoring(cheat_name)
    local cheat = CHEATS[cheat_name]
    if not cheat.target_address or not cheat.condition then
        return
    end
    
    local addr = cheat.target_address
    breakpoint.add(addr.seg, addr.offset, "Cheat_" .. cheat_name,
                  "", -- No condition in breakpoint, we'll check in action
                  string.format([[
                      local cheat = CHEATS['%s']
                      if cheat and cheat.condition() then
                          cheat.action()
                      end
                  ]], cheat_name))
end

-- Function to list available cheats
function list_cheats()
    debug.log("=== Available Cheats ===")
    local count = 0
    for cheat_name, cheat in pairs(CHEATS) do
        count = count + 1
        local status = active_cheats[cheat_name] and "🟢 ACTIVE" or "⚪ inactive"
        debug.log(string.format("%d. %s - %s", count, cheat.name, status))
        debug.log("   " .. cheat.description)
    end
    debug.log("Total cheats available: " .. count)
end

-- Function to show cheat status
function cheat_status()
    debug.log("=== Cheat System Status ===")
    debug.log("System enabled: " .. (cheats_enabled and "YES" or "NO"))
    
    local active_count = 0
    for _ in pairs(active_cheats) do
        active_count = active_count + 1
    end
    debug.log("Active cheats: " .. active_count)
    
    if active_count > 0 then
        debug.log("Currently active:")
        for cheat_name, _ in pairs(active_cheats) do
            debug.log("  • " .. CHEATS[cheat_name].name)
        end
    end
end

-- Function to show cheat history
function cheat_history_log()
    debug.log("=== Cheat History ===")
    if #cheat_history == 0 then
        debug.log("No cheat activity recorded")
        return
    end
    
    for i = #cheat_history, math.max(1, #cheat_history - 9), -1 do
        local entry = cheat_history[i]
        local time_str = os.date("%H:%M:%S", entry.activated_at)
        debug.log(string.format("[%s] %s %s", time_str, 
                               CHEATS[entry.name].name, entry.action))
    end
end

-- Quick cheat functions
function god_mode()
    activate_cheat("infinite_health")
    activate_cheat("infinite_lives")
    debug.log("🛡️ GOD MODE ACTIVATED")
end

function weapon_cheats()
    activate_cheat("infinite_ammo")
    debug.log("🔫 WEAPON CHEATS ACTIVATED")
end

function all_cheats()
    for cheat_name, _ in pairs(CHEATS) do
        activate_cheat(cheat_name)
    end
    debug.log("🎮 ALL CHEATS ACTIVATED!")
end

function no_cheats()
    for cheat_name, _ in pairs(active_cheats) do
        deactivate_cheat(cheat_name)
    end
    debug.log("🚫 ALL CHEATS DEACTIVATED")
end

-- Cheat code detection system
local cheat_codes = {
    ["IDDQD"] = god_mode,
    ["IDKFA"] = weapon_cheats,
    ["NOCLIP"] = function() debug.log("🚪 No-clip cheat (not implemented)") end
}

local input_buffer = ""
local max_buffer_length = 10

function process_cheat_input(key)
    -- This would need integration with DOSBox-X keyboard handling
    -- For now, this is a placeholder showing how it could work
    input_buffer = input_buffer .. key
    
    if #input_buffer > max_buffer_length then
        input_buffer = string.sub(input_buffer, 2)
    end
    
    for code, func in pairs(cheat_codes) do
        if string.find(input_buffer:upper(), code) then
            debug.log("🎮 Cheat code detected: " .. code)
            func()
            input_buffer = "" -- Clear buffer after successful code
            break
        end
    end
end

-- Safe cheat management
function safe_cheat_mode()
    debug.log("🛡️ SAFE CHEAT MODE")
    debug.log("Only activating non-game-breaking cheats...")
    
    -- Only enable "safe" cheats that don't break game progression
    activate_cheat("infinite_health")
    debug.log("Safe cheat mode activated")
end

debug.log("Cheat System loaded successfully!")
debug.log("")
debug.log("⚠️  CHEAT SYSTEM USAGE WARNING ⚠️")
debug.log("Cheats may affect game balance, achievements, and save files.")
debug.log("An auto-save will be created when cheats are first enabled.")
debug.log("")
debug.log("Available commands:")
debug.log("  enable_cheats() - Enable the cheat system")
debug.log("  disable_cheats() - Disable all cheats")
debug.log("  list_cheats() - Show available cheats")
debug.log("  activate_cheat(name) - Activate specific cheat")
debug.log("  deactivate_cheat(name) - Deactivate specific cheat")
debug.log("  cheat_status() - Show current status")
debug.log("  cheat_history_log() - Show usage history")
debug.log("")
debug.log("Quick functions:")
debug.log("  god_mode() - Infinite health + lives")
debug.log("  weapon_cheats() - Infinite ammo")
debug.log("  all_cheats() - Activate everything")
debug.log("  no_cheats() - Deactivate everything")
debug.log("  safe_cheat_mode() - Only safe cheats")
debug.log("")
debug.log("Remember to configure addresses for your specific game!")