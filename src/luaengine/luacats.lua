---@meta
---@diagnostic disable: lowercase-global

----------------------------------------------------------------
-- Breakpoint API
----------------------------------------------------------------

---@class BreakpointInfo
---@field segment integer          -- CS
---@field offset integer           -- IP
---@field address integer          -- linear address
---@field name string|nil
---@field active boolean
---@field once boolean
---@field condition string|nil
---@field action string|nil

---@class BreakpointAPI
local breakpoint = {}

---Add a breakpoint or tracepoint.
---C++ signature: add(cs, ip, name?, condition?, action?, stop?)
---@param cs integer
---@param ip integer
---@param name? string         -- optional label
---@param condition? string    -- Lua expression; evaluated with `return`
---@param action? string       -- Lua chunk executed when hit; if it returns boolean, that overrides stop behavior
---@param stop? boolean        -- true = physical breakpoint (halts), false = trace-only
---@return boolean ok
function breakpoint.add(cs, ip, name, condition, action, stop) end

---List known *physical* breakpoints (those backed by CBreakpoint).
---@return BreakpointInfo[] list
function breakpoint.list() end

_G.breakpoint = breakpoint

----------------------------------------------------------------
-- Symbols API
----------------------------------------------------------------

---@class Symbol
---@field address integer
---@field name string
---@field module string
---@field type string
---@field size integer
---@field segment integer
---@field offset integer
---@field file_address integer
---@field segment_name string
---@field sourceline string|nil

---@class SegmentMapping
---@field segment_name string
---@field file_segment integer
---@field memory_base integer
---@field size integer
---@field enabled boolean

---@class SymbolicBreakpoint
---@field symbol string
---@field offset integer
---@field enabled boolean
---@field address integer
---@field description string

---@class SymbolsAPI
local symbols = {}

----------------------------------------------------------------
-- Symbol file management
----------------------------------------------------------------

---Load symbols from a file (MAP/LST/etc).
---@param filename string
---@return boolean ok
---@return string|nil err   -- present only when ok==false
function symbols.loadSymbolFile(filename) end

---Clear all loaded symbols and re-resolve symbolic breakpoints.
function symbols.clearSymbols() end

---Whether any symbols are currently loaded.
---@return boolean has_symbols
function symbols.hasSymbols() end

---Number of loaded symbols.
---@return integer count
function symbols.getSymbolCount() end

---Basename of the last successfully loaded symbol file, or empty string.
---@return string filename
function symbols.getLoadedFileName() end

---Format of the loaded file: "MASM_MAP", "MASM_LST", "WATCOM_MAP",
---"BORLAND_MAP", "GNU_MAP", "COFF_SYMBOLS", or "UNKNOWN".
---@return string format
function symbols.getLoadedFormat() end

----------------------------------------------------------------
-- Symbol lookups
----------------------------------------------------------------

---Get a display name for an address.
---@param address integer           -- linear address
---@param include_offset? boolean   -- default true
---@return string name              -- empty string if none
function symbols.getSymbolName(address, include_offset) end

---Lookup an address by symbol name.
---@param name string
---@return integer address          -- 0 if not found
function symbols.getSymbolAddress(name) end

---Find the symbol that covers an address, if any.
---@param address integer
---@return Symbol|nil symbol
function symbols.findSymbol(address) end

---All loaded symbols.
---@return Symbol[] symbols
function symbols.getAllSymbols() end

---Symbols whose addresses lie within [start_addr, end_addr].
---@param start_addr integer
---@param end_addr integer
---@return Symbol[] symbols
function symbols.getSymbolsInRange(start_addr, end_addr) end

---Names of all loaded symbols.
---@return string[] names
function symbols.getSymbolNames() end

----------------------------------------------------------------
-- Segment mappings
----------------------------------------------------------------

---Add a segment mapping from Lua table.
---@param mapping SegmentMapping
function symbols.addSegmentMapping(mapping) end

---@param segment_name string
function symbols.removeSegmentMapping(segment_name) end

---@param segment_name string
---@param memory_base integer
function symbols.updateSegmentMapping(segment_name, memory_base) end

---@param segment_name string
---@param enabled boolean
function symbols.enableSegmentMapping(segment_name, enabled) end

---@return SegmentMapping[] mappings
function symbols.getSegmentMappings() end

---@param segment_name string
---@return SegmentMapping|nil mapping
function symbols.findSegmentMapping(segment_name) end

---Clear all segment mappings.
function symbols.clearSegmentMappings() end

---Try to auto-detect segment mappings based on loaded symbols.
function symbols.autoDetectSegments() end

---Re-apply mapping adjustments to all symbols.
function symbols.remapAllSymbols() end

----------------------------------------------------------------
-- Address conversion & mapping
----------------------------------------------------------------

---Convert segment:offset to linear address.
---@param segment integer
---@param offset integer
---@return integer linear
function symbols.segmentOffsetToLinear(segment, offset) end

---Convert linear address to segment:offset.
---@param linear integer
---@return integer segment
---@return integer offset
function symbols.linearToSegmentOffset(linear) end

---Map an address to emulator memory.
---@param segment integer
---@param offset integer
---@param segment_name? string
---@return integer mapped_address
function symbols.mapAddressToMemory(segment, offset, segment_name) end

----------------------------------------------------------------
-- Manual symbol management
----------------------------------------------------------------

---Add a single symbol.
---@param symbol Symbol   -- table with at least {address, name}
function symbols.addSymbol(symbol) end

---Bulk-add symbols relative to a base address.
---Each entry is {offset=integer, name=string} or {offset, name}.
---@param base_addr integer
---@param sym_table table
function symbols.addSymbolsBulk(base_addr, sym_table) end

---Remove a symbol by name or address.
---@param key string|integer
function symbols.removeSymbol(key) end

----------------------------------------------------------------
-- Symbolic breakpoints
----------------------------------------------------------------

---Get all symbolic breakpoints.
---@return SymbolicBreakpoint[] list
function symbols.getSymbolicBreakpoints() end

---Add a symbolic breakpoint on a symbol + offset.
---@param symbol_name string
---@param offset integer
---@param description? string
---@param enabled? boolean   -- default true
function symbols.addSymbolicBreakpoint(symbol_name, offset, description, enabled) end

---Remove a symbolic breakpoint by 1-based index.
---@param index integer      -- 1-based index into getSymbolicBreakpoints()
function symbols.removeSymbolicBreakpoint(index) end

---Toggle a symbolic breakpoint by index.
---@param index integer
---@param enabled boolean
function symbols.toggleSymbolicBreakpoint(index, enabled) end

---Save all symbolic breakpoints to a file.
---@param filename string
---@return boolean ok
---@return string|nil msg
function symbols.saveSymbolicBreakpoints(filename) end

---Load symbolic breakpoints from a file, replacing the current set.
---@param filename string
---@return integer count       -- number of breakpoints loaded
function symbols.loadSymbolicBreakpoints(filename) end

_G.symbols = symbols

----------------------------------------------------------------
-- Debug API (extends the standard Lua debug library)
----------------------------------------------------------------

---@class DebugAPI
local debug = debug or {}

---Note: Standard Lua debug functions (debug.getinfo, debug.sethook, etc.) remain available.

---Write a message to the DOSBox-X log.
---@param msg string
function debug.log(msg) end

---Alias for log().
---@param msg string
function debug.print(msg) end

---Return whether LRDB is active.
---@return boolean active
function debug.lrdb_is_active() end

---LRDB port configured for attach.
---@return integer port
function debug.lrdb_port() end

---Hex dump a memory range to the log.
---@param segment integer
---@param offset integer
---@param size integer
function debug.hexdump(segment, offset, size) end

---Get current CS:IP.
---@return table addr             -- {cs=integer, ip=integer}
function debug.get_current_address() end

---Enable/disable fast debug mode (cached tables, less allocation).
---@param enable boolean
function debug.set_fast_mode(enable) end

---Disassemble instructions starting at seg:offset.
---@param segment integer
---@param offset integer
---@param count? integer
---@return table instructions      -- array of {address, segment, mnemonic, size, bytes[]}
function debug.disassemble(segment, offset, count) end

---Get a single instruction at seg:offset.
---@param segment integer
---@param offset integer
---@return table instruction       -- {address, segment, mnemonic, size, bytes[]}
function debug.get_instruction_at(segment, offset) end

---Execution controls (currently stubs in C++).
function debug.step_into() end
function debug.step_over() end
function debug.run_until(segment, offset) end
function debug.is_execution_paused() end
function debug.pause_execution() end
function debug.resume_execution() end

---Memory watchpoints.
function debug.add_memory_watchpoint(address, size, type) end
function debug.remove_memory_watchpoint(address) end

---Map/symbol helpers (legacy).
function debug.load_map_file(filename) end
function debug.add_symbol(address, name) end
function debug.get_symbol(address) end
function debug.clear_symbols() end

---Show all debugger windows (if available).
function debug.show_all_windows() end

_G.debug = debug

----------------------------------------------------------------
-- Performance API
----------------------------------------------------------------

---@class PerformanceStats
---@field frame_calls integer
---@field total_lua_time_us integer
---@field breakpoint_hits integer
---@field script_executions integer
---@field memory_operations integer

---@class PerformanceAPI
local performance = {}

---Get runtime stats: frame calls, Lua time, breakpoint hits, etc.
---@return PerformanceStats stats
function performance.get_stats() end

---Enable or disable fast memory access.
---@param enable boolean
function performance.enable_fast_memory(enable) end

---Enable or disable minimal error checking.
---@param enable boolean
function performance.enable_minimal_error_checking(enable) end

---@return boolean enabled
function performance.is_fast_memory_enabled() end

---@return boolean enabled
function performance.is_minimal_error_checking_enabled() end

---Read bytes using linear/physical address (segment 0).
---@param address integer
---@param size integer
---@return integer[] bytes
function performance.read_bytes_fast(address, size) end

---Write bytes using linear/physical address (segment 0).
---@param address integer
---@param data integer[]
---@return boolean ok
function performance.write_bytes_fast(address, data) end

_G.performance = performance

----------------------------------------------------------------
-- Memory API – domain helpers
----------------------------------------------------------------

---@class MemoryDomainInfo
---@field name string
---@field display_name string
---@field start_address integer
---@field size integer
---@field end_address integer
---@field writable boolean
---@field available boolean

---@class MemoryAPI
local memory = memory or {}

---Read from a specific memory domain.
---If size is omitted, returns a single byte.
---If size is provided, returns a 1-based array of bytes.
---@param domain_name string
---@param address integer
---@param size? integer
---@return integer|integer[]|nil value
function memory.read_domain(domain_name, address, size) end

---Write a sequence of bytes into a specific memory domain.
---@param domain_name string
---@param address integer
---@param data integer[]   -- 1-based array of bytes
---@return boolean ok
function memory.write_domain(domain_name, address, data) end

---Write a single byte into a specific memory domain.
---@param domain_name string
---@param address integer
---@param value integer
---@return boolean ok
function memory.write_domain_byte(domain_name, address, value) end

---Get info for all available memory domains.
---@return MemoryDomainInfo[] domains
function memory.get_domains() end

---Get info for a single memory domain.
---@param domain_name string
---@return MemoryDomainInfo|nil info
function memory.get_domain_info(domain_name) end

_G.memory = memory
