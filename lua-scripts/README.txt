DOSBox-X Lua Scripts Directory
==============================

This directory contains sample Lua scripts for the DOSBox-X LuaEngine system.
These scripts demonstrate various features and provide starting points for
creating your own game automation and debugging scripts.

DIRECTORY STRUCTURE:
-------------------
lua-scripts/
├── README.txt                    # This file
├── demo.lua                      # Main demonstration script (autostarted)
└── examples/
    ├── simple-memory-monitor.lua  # Basic memory monitoring
    ├── speedrun-helper.lua        # Speedrunning tools and timing
    └── cheat-system.lua          # Comprehensive cheat system

GETTING STARTED:
---------------
1. Copy dosbox-lua-sample.conf to dosbox-x.conf
2. Start DOSBox-X - the demo.lua script will run automatically
3. Open the Lua console window (if enabled in config)
4. Try interactive commands like lua_help() or lua_status()

CONFIGURATION:
-------------
The Lua system is configured in the [lua] section of dosbox-x.conf:

[lua]
enabled = true                    # Enable Lua scripting
hooks = true                      # Enable CPU monitoring
console = true                    # Enable interactive console
autostart = lua-scripts/demo.lua  # Script to run at startup

SCRIPT DESCRIPTIONS:
-------------------

demo.lua:
  The main demonstration script that showcases all LuaEngine features.
  It demonstrates:
  - Basic API usage (memory, CPU, debug)
  - Breakpoint system with conditions and actions
  - Save state integration
  - Game templates for rapid development
  - Interactive console commands
  - Performance monitoring
  - Hot-reload development workflow
  
  This script is suitable as an autostart script and provides a comprehensive
  overview of the LuaEngine capabilities.

examples/simple-memory-monitor.lua:
  A basic memory monitoring script for DOS games. Shows how to:
  - Monitor specific memory addresses for changes
  - Track player health, lives, and score
  - Create simple cheat functions
  - Set up automatic monitoring with breakpoints
  
  Good starting point for game-specific scripts.

examples/speedrun-helper.lua:
  Tools for speedrunning DOS games. Includes:
  - Precision timing and lap tracking
  - Checkpoint system with automatic detection
  - Personal best tracking
  - Save state management for practice
  - Input recording (framework)
  - Frame counting and timing analysis
  
  Useful for competitive gaming and time attacks.

examples/cheat-system.lua:
  A comprehensive cheat system with safety features. Provides:
  - Multiple cheat types (infinite health, lives, ammo, etc.)
  - Condition-based automatic cheats
  - Cheat code detection system
  - Safe cheat mode for non-game-breaking cheats
  - Cheat history and management
  - Toggle and permanent cheat types
  
  Includes safety features like auto-save before enabling cheats.

CREATING YOUR OWN SCRIPTS:
-------------------------
1. Study the example scripts to understand the API
2. Create a new .lua file in this directory
3. Configure memory addresses for your specific game
4. Test your script using the hot-reload feature
5. Use the interactive console for development and debugging

MEMORY ADDRESS DISCOVERY:
------------------------
To create effective scripts, you need to find memory addresses in your game:

1. Use debug.hexdump(seg, offset, size) to examine memory
2. Use memory.readbyte(seg, offset) to monitor specific locations
3. Look for patterns when game values change
4. Use breakpoints to catch memory access points
5. Try common address ranges:
   - 0x0040:0x0000-0x00FF (BIOS data area)
   - 0x1000:0x0000-0xFFFF (common game data area)
   - Use game-specific knowledge or memory trainers

DEBUGGING TIPS:
--------------
- Use debug.log() for output instead of print()
- Enable the console for interactive testing
- Use hot-reload to quickly test script changes
- Monitor performance with performance.get_stats()
- Create save states before testing destructive operations
- Use breakpoint conditions to filter events

API REFERENCE SUMMARY:
---------------------

Memory API:
  memory.readbyte(seg, offset)
  memory.writebyte(seg, offset, value)
  memory.readword(seg, offset)
  memory.writeword(seg, offset, value)

CPU API:
  cpu.get_ax(), cpu.get_bx(), cpu.get_cx(), cpu.get_dx()
  cpu.set_ax(value), cpu.set_bx(value), etc.
  cpu.get_cs(), cpu.get_ds(), cpu.get_es(), cpu.get_ss()

Debug API:
  debug.log(message)
  debug.hexdump(seg, offset, size)

Breakpoint API:
  breakpoint.add(cs, ip, name, condition, action)
  breakpoint.remove(cs, ip)
  breakpoint.enable(cs, ip, enabled)
  breakpoint.list()
  breakpoint.clear()

Savestate API:
  savestate.quick_save()
  savestate.quick_load()
  savestate.set_current_slot(slot)
  savestate.get_current_slot()

Template API:
  template.list()
  template.load(name, params)
  template.create_platformer(x_seg, x_ofs, y_seg, y_ofs)
  template.create_rpg(hp_seg, hp_ofs, mp_seg, mp_ofs, gold_seg, gold_ofs)

Console API:
  console.help(), console.status()
  console.memory(seg, offset)
  console.cpu(), console.exec(code)

Performance API:
  performance.get_stats()
  performance.get_report()
  performance.benchmark(func, iterations)

Hot-reload API:
  hotreload.get_script()
  hotreload.set_script(path)

SAFETY GUIDELINES:
-----------------
- Always backup save files before using new scripts
- Test scripts on backup copies of games first
- Be careful with memory.write* functions
- Use conditions in breakpoints to avoid excessive triggering
- Monitor performance impact with performance monitoring
- Keep scripts simple and focused for better reliability

COMMUNITY AND SHARING:
---------------------
When sharing scripts:
- Document the game and version they're designed for
- Include memory address comments
- Provide setup instructions
- Test on clean installations
- Consider different game configurations

TROUBLESHOOTING:
---------------
If scripts don't work:
1. Check that enabled=true in [lua] section
2. Verify script syntax with console.exec()
3. Use debug.log() to trace execution
4. Check memory addresses are correct for your game version
5. Monitor console for error messages
6. Use performance monitoring to check for issues

ADVANCED FEATURES:
-----------------
- Hot-reload: Automatically reloads scripts when files change
- Performance monitoring: Track script impact on emulation
- Game templates: Pre-built scripts for common game types
- Interactive console: Real-time script development and testing
- Conditional breakpoints: Smart triggering based on game state

For more information, see the comprehensive test scripts and
the DOSBox-X documentation.

Happy scripting!