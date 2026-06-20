-- Simple Memory Monitor Example
-- =============================
-- This script demonstrates basic memory monitoring for DOS games.
-- It shows how to watch specific memory locations and react to changes.

debug.log("=== Simple Memory Monitor Example ===")

-- Configuration: Adjust these addresses for your specific game
local PLAYER_HEALTH_SEG = 0x1000
local PLAYER_HEALTH_OFFSET = 0x0100
local PLAYER_LIVES_SEG = 0x1000
local PLAYER_LIVES_OFFSET = 0x0200
local PLAYER_SCORE_SEG = 0x1000
local PLAYER_SCORE_OFFSET = 0x0300

-- Variables to track previous values
local last_health = 0
local last_lives = 0
local last_score = 0

-- Function to read and monitor game values
function monitor_game_state()
    -- Read current values
    local current_health = memory.readbyte(PLAYER_HEALTH_SEG, PLAYER_HEALTH_OFFSET) or 0
    local current_lives = memory.readbyte(PLAYER_LIVES_SEG, PLAYER_LIVES_OFFSET) or 0
    local current_score = memory.readword(PLAYER_SCORE_SEG, PLAYER_SCORE_OFFSET) or 0
    
    -- Check for changes and log them
    if current_health ~= last_health then
        debug.log("Health changed: " .. last_health .. " -> " .. current_health)
        last_health = current_health
        
        -- Warning for low health
        if current_health < 20 then
            debug.log("WARNING: Low health! (" .. current_health .. ")")
        end
    end
    
    if current_lives ~= last_lives then
        debug.log("Lives changed: " .. last_lives .. " -> " .. current_lives)
        last_lives = current_lives
        
        -- Alert for losing lives
        if current_lives < last_lives then
            debug.log("Life lost! Remaining: " .. current_lives)
        end
    end
    
    if current_score ~= last_score then
        debug.log("Score changed: " .. last_score .. " -> " .. current_score)
        last_score = current_score
    end
end

-- Set up automatic monitoring using breakpoints
-- Note: You'll need to find actual code addresses in your game where these values are accessed
breakpoint.add(0x2000, 0x0100, "HealthCheck", "", "monitor_game_state()")

-- Alternative: Manual monitoring function you can call from console
function check_game_state()
    debug.log("=== Current Game State ===")
    local health = memory.readbyte(PLAYER_HEALTH_SEG, PLAYER_HEALTH_OFFSET) or 0
    local lives = memory.readbyte(PLAYER_LIVES_SEG, PLAYER_LIVES_OFFSET) or 0
    local score = memory.readword(PLAYER_SCORE_SEG, PLAYER_SCORE_OFFSET) or 0
    
    debug.log("Health: " .. health)
    debug.log("Lives: " .. lives)
    debug.log("Score: " .. score)
    debug.log("========================")
end

-- Quick cheat functions
function restore_health()
    memory.writebyte(PLAYER_HEALTH_SEG, PLAYER_HEALTH_OFFSET, 100)
    debug.log("Health restored to 100")
end

function add_life()
    local current_lives = memory.readbyte(PLAYER_LIVES_SEG, PLAYER_LIVES_OFFSET) or 0
    memory.writebyte(PLAYER_LIVES_SEG, PLAYER_LIVES_OFFSET, current_lives + 1)
    debug.log("Added 1 life (total: " .. (current_lives + 1) .. ")")
end

function add_score(points)
    points = points or 1000
    local current_score = memory.readword(PLAYER_SCORE_SEG, PLAYER_SCORE_OFFSET) or 0
    memory.writeword(PLAYER_SCORE_SEG, PLAYER_SCORE_OFFSET, current_score + points)
    debug.log("Added " .. points .. " points (total: " .. (current_score + points) .. ")")
end

debug.log("Simple Memory Monitor loaded successfully!")
debug.log("Available functions:")
debug.log("  check_game_state() - Show current values")
debug.log("  restore_health() - Set health to 100")
debug.log("  add_life() - Add one life")
debug.log("  add_score(points) - Add points to score")
debug.log("")
debug.log("Remember to adjust the memory addresses for your specific game!")