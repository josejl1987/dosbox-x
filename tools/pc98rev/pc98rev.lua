-- Minimal Lua orchestration bridge for DOSBox-X lua-re-hooks.
-- This script dumps reverse-engineering events and memory regions so the
-- host-side pc98rev Python tool can ingest them.

local incoming_dir = "tools/pc98rev/incoming/"
local modules_dir = "tools/pc98rev/incoming/modules/"

local function ensure_dirs()
    -- Lua has os.execute; we rely on the host to create directories for now.
end

local event_log = nil

local function event_path()
    return incoming_dir .. "events_" .. os.time() .. ".jsonl"
end

local function open_log()
    if event_log then
        event_log:close()
    end
    ensure_dirs()
    event_log = io.open(event_path(), "a")
end

local function log_event(ev)
    if not event_log then open_log() end
    local json = "{"
    local first = true
    for k, v in pairs(ev) do
        if not first then json = json .. "," end
        first = false
        if type(v) == "string" then
            -- minimal escaping, not JSON-correct for all inputs
            json = json .. '"' .. k .. '":"' .. v:gsub('"', '\\"') .. '"'
        else
            json = json .. '"' .. k .. '":' .. tostring(v)
        end
    end
    json = json .. "}\n"
    event_log:write(json)
    event_log:flush()
end

-- File operation event logging
local function file_event(name, ev)
    ev.type = name
    log_event(ev)
end

local function hex(n, width)
    width = width or 4
    return string.format("%0" .. width .. "X", n or 0)
end

local function dump_buffer(path, seg, off, len)
    if len <= 0 or len >= 0x10000 then return end
    local bytes = re.dump_range(seg, off, len)
    if not bytes or #bytes == 0 then return end
    local f = io.open(path, "wb")
    if not f then return end
    for i = 1, #bytes do
        f:write(string.char(bytes[i]))
    end
    f:close()
end

-- KHD archive tracking: KHD/KLB entry name lookup is done Python-side
-- during ingestion, because runtime callbacks are deferred and the
-- destination buffer gets overwritten before we can read it.
-- See pc98rev/khd_parser.py and the ingest command.

re.on_file_open_complete(function(ev)
    file_event("file_open_complete", ev)
end)

re.on_file_read_complete(function(ev)
    file_event("file_read_complete", ev)
    -- For large reads into conventional memory, dump the destination region
    if ev.actual and ev.actual > 0 and ev.actual < 0x10000 then
        local dump_path = modules_dir .. "read_" .. ev.handle .. "_" .. ev.file_offset .. ".bin"
        dump_buffer(dump_path, ev.destination_segment, ev.destination_offset, ev.actual)
    end
end)

re.on_file_write_complete(function(ev)
    file_event("file_write_complete", ev)
end)

re.on_file_seek_complete(function(ev)
    file_event("file_seek_complete", ev)
end)

re.on_file_close(function(ev)
    file_event("file_close", ev)
end)

-- Targeted tracking: queue INT 21h AH=3Fh reads so we can break right after
-- the DOS call returns to the game. This gives us the game-side return
-- address and lets us capture what the decompressor does immediately after.
local pending_reads = {} -- keyed by DOS handle (BX)

-- Active write captures waiting for a frame boundary to complete.
local active_captures = {}
local capture_seq = 0

-- CDL session setup: begin collecting instruction-level evidence now.
local cdl_path = "tools/pc98rev/incoming/session-live.p98cdl"
local cdl_autosave_interval = 1800  -- ~30 seconds at 60fps
local cdl_finalize_path = cdl_path  -- also saved by C++ shutdown hook on close
local cdl_ok, cdl_err = pcall(function()
    cdl.create("brandish")

    -- Catch-all for conventional memory (registered first so specific
    -- modules registered later take priority via modules_by_addr_).
    cdl.register_module({ id = "conventional", base = 0, size = 0xA0000 })

    -- BR1.EXE: PSP at 0x0913, code at 0x0923, overlay at 0x0BA3.
    -- Covers the full EXE image including decompressor stubs.
    cdl.register_module({ id = "BR1.EXE", base = 0x09130, size = 0x3000,
                          source_file = "BR1.EXE" })

    -- Data/stack segment (DS=SS=CS+0x1000, SP=0x800).
    -- Verified from UTY.BZH entry point at offset 0x40 — identical to BR2.
    -- Stack at DS:0800 growing down; variables/BSS above; game data
    -- loaded at DS:4000 by BIOSCODE BZ_EXEC_LOAD.
    cdl.register_module({ id = "BR1_DATA", base = 0x19230, size = 0x10000 })

    -- KLB/KHD staging buffer at 0x2BA3:0000 (compressed data landing zone).
    -- Located in the gap between DS (end 0x29230) and BASE_SEG (0x31230).
    cdl.register_module({ id = "KLB_STAGE", base = 0x2BA30, size = 0x10000 })

    -- Game data segments (all CS-relative, verified from entry point):
    -- BASE_SEG = OVL_SEG = CS+0x2800, size 0x8000 (32KB)
    cdl.register_module({ id = "BASE_SEG", base = 0x31230, size = 0x8000 })
    -- BACK_SEG = CS+0x3000, size 0xC000 (48KB)
    cdl.register_module({ id = "BACK_SEG", base = 0x39230, size = 0xC000 })
    -- PLYR_SEG = CS+0x3C00, size 0xC000 (48KB)
    cdl.register_module({ id = "PLYR_SEG", base = 0x45230, size = 0xC000 })
    -- MON1_SEG = CS+0x4800, size 0x8000 (32KB)
    cdl.register_module({ id = "MON1_SEG", base = 0x51230, size = 0x8000 })
    -- MON2_SEG = CS+0x5000, size 0x8000 (32KB)
    cdl.register_module({ id = "MON2_SEG", base = 0x59230, size = 0x8000 })
    -- MON3_SEG = CS+0x5800, size 0x8000 (32KB)
    cdl.register_module({ id = "MON3_SEG", base = 0x61230, size = 0x8000 })
    -- ITEM_SEG = CS+0x6000, size 0xC000 (48KB)
    cdl.register_module({ id = "ITEM_SEG", base = 0x69230, size = 0xC000 })
    -- ICON_SEG = CS+0x6C00, size 0xC000 (48KB)
    cdl.register_module({ id = "ICON_SEG", base = 0x75230, size = 0xC000 })
    -- PAT_SEG = CS+0x7800, size 0xC000 (48KB)
    cdl.register_module({ id = "PAT_SEG", base = 0x81230, size = 0xC000 })

    -- Decompressor code at 0x3BA3 (entry 3BA3:00C5).
    -- Registered AFTER data segments so it takes priority over BACK_SEG
    -- for the 0x3BA30-0x3BC2F range (decompressor is code, not data).
    cdl.register_module({ id = "DECOMP", base = 0x3BA30, size = 0x200,
                          source_file = "BR1.EXE" })

    -- Decompressor output area near top of conventional RAM.
    cdl.register_module({ id = "DECOMP_OUT", base = 0x90000, size = 0xA000 })

    cdl.start()
    -- Register a C++ shutdown fallback so closing the window still flushes
    -- the CDL to disk even if the Lua destructor path is skipped.
    if re.set_cdl_finalize_path then
        re.set_cdl_finalize_path(cdl_finalize_path)
    end
end)
if cdl_ok then
    log_event({ type = "cdl_status", message = "CDL session active with game modules" })
else
    log_event({ type = "cdl_error", message = tostring(cdl_err) })
end

-- Called when the INT 21h read returns to the game code.
local function on_read_return(pending, ev_for_filename)
    -- Dump what the read just stashed in the buffer, tagged with the game-side
    -- return address. This is the raw (likely compressed) chunk.
    local prefix = modules_dir .. "klb_" .. pending.handle .. "_" .. pending.file_offset
                    .. "_" .. hex(pending.ret_cs) .. "_" .. hex(pending.ret_ip)
    dump_buffer(prefix .. "_buffer.bin", pending.ds, pending.dx, pending.cx)

    log_event({
        type = "klb_read_return",
        filename = ev_for_filename,
        handle = pending.handle,
        file_offset = pending.file_offset,
        ret_cs = pending.ret_cs,
        ret_ip = pending.ret_ip,
        buffer_seg = pending.ds,
        buffer_off = pending.dx,
        requested = pending.cx
    })

    -- Note a marker for the decompressor entry point.
    local sum_path = prefix .. "_return.txt"
    local f = io.open(sum_path, "w")
    if f then
        f:write(string.format("Return after INT 21h AH=3Fh read\n"))
        f:write(string.format("Game CS:IP = %04X:%04X\n", pending.ret_cs, pending.ret_ip))
        f:write(string.format("Buffer DS:DX = %04X:%04X  len=0x%X\n", pending.ds, pending.dx, pending.cx))
        f:close()
    end

    -- Start tracking writes to conventional memory for two frames. This captures
    -- where the decompressor writes its output.
    local cid = re.capture_begin(0, 0, 0xA0000)
    capture_seq = capture_seq + 1
    table.insert(active_captures, {
        id = capture_seq,
        capture_id = cid,
        frames_left = 2,
        prefix = prefix,
        handle = pending.handle,
        file_offset = pending.file_offset,
        filename = ev_for_filename
    })
end

-- Frame boundary handler: finish pending write captures.
local cdl_frames = 0
re.on_frame(function(ev)
    cdl_frames = cdl_frames + 1
    if cdl_frames % 600 == 0 then
        local stats_ok, stats = pcall(function() return cdl.stats() end)
        if stats_ok then
            log_event({ type = "cdl_stats", frames = cdl_frames, stats = tostring(stats) })
        else
            log_event({ type = "cdl_stats_error", frames = cdl_frames, message = tostring(stats) })
        end
    end
    if cdl_frames % cdl_autosave_interval == 0 then
        local save_ok, save_err = pcall(function() cdl.save(cdl_path) end)
        if save_ok then
            log_event({ type = "cdl_autosave", frames = cdl_frames, path = cdl_path })
        else
            log_event({ type = "cdl_autosave_error", frames = cdl_frames, message = tostring(save_err) })
        end
    end

    local i = 1
    while i <= #active_captures do
        local cap = active_captures[i]
        cap.frames_left = cap.frames_left - 1
        if cap.frames_left <= 0 then
            local ranges = re.capture_end(cap.capture_id)
            local written = {}
            if ranges then
                for j = 1, #ranges do
                    table.insert(written, {
                        start_linear = ranges[j].start_linear,
                        length = ranges[j].length
                    })
                    -- Feed each written range into the CDL as DataWrite evidence.
                    pcall(function()
                        cdl.data_write(ranges[j].start_linear, ranges[j].length)
                    end)
                end
            end
            log_event({
                type = "klb_write_ranges",
                filename = cap.filename,
                handle = cap.handle,
                file_offset = cap.file_offset,
                ranges = #written
            })
            local wf = io.open(cap.prefix .. "_writes.txt", "w")
            if wf then
                for j = 1, #written do
                    local r = written[j]
                    wf:write(string.format("0x%05X-0x%05X\n", r.start_linear, r.start_linear + r.length - 1))
                end
                wf:close()
            end
            table.remove(active_captures, i)
        else
            i = i + 1
        end
    end
end)

-- INT 21h file/EXEC hook.
re.on_interrupt(function(ev)
    if ev.num == 0x21 then
        if ev.ax >= 0x3F00 and ev.ax < 0x4000 then
            -- DOS file read (AH=3Fh): queue the return point.
            -- BX=file handle, CX=bytes to read, DS:DX=buffer.
            pending_reads[ev.bx] = {
                handle = ev.bx,
                ds = ev.ds,
                dx = ev.dx,
                cx = ev.cx,
                ret_cs = ev.cs,
                ret_ip = ev.return_ip
            }
            log_event({ type = "interrupt", num = ev.num, ax = ev.ax, bx = ev.bx, cx = ev.cx,
                        cs = ev.cs, ret_ip = ev.return_ip })
        else
            log_event({ type = "interrupt", num = ev.num, ax = ev.ax, cs = ev.cs, ip = ev.ip })
        end
    end
end)

-- When a KLB read completes from the DOS file hook, set a run-until
-- breakpoint at the game's CS:IP right after the INT 21h instruction.
re.on_file_read_complete(function(ev)
    local pending = pending_reads[ev.handle]
    if pending then
        pending.file_offset = ev.file_offset
        local is_target = ev.filename and (string.match(ev.filename, "KLB$")
                                          or string.match(ev.filename, "KHD$"))
        if is_target then
            print("[pc98rev] KLB read pending, setting run_until "
                  .. hex(pending.ret_cs) .. ":" .. hex(pending.ret_ip))
            local ok = debug.run_until(pending.ret_cs, pending.ret_ip, function(cs, ip)
                print("[pc98rev] KLB return hit at " .. hex(cs) .. ":" .. hex(ip))
                on_read_return(pending, ev.filename)
            end)
            print("[pc98rev] debug.run_until returned " .. tostring(ok))
        end
        pending_reads[ev.handle] = nil
    end
end)

print("[pc98rev] Lua bridge loaded.")
print("[pc98rev] Events written to " .. incoming_dir)

-- Save everything on emulator exit while the Lua state is still valid.
re.on_exit(function(ev)
    print("[pc98rev] Exit hook: saving CDL and event log")
    if event_log then
        event_log:flush()
    end
    local ok, err = pcall(function() cdl.save(cdl_path) end)
    if ok then
        print("[pc98rev] CDL saved on exit to " .. cdl_path)
    else
        print("[pc98rev] CDL save on exit failed: " .. tostring(err))
    end
end)

-- Test helpers exposed to the console
_G.pc98rev = {
    log = log_event,
    save_cdl = function(path)
        path = path or (cdl_path .. ".manual.p98cdl")
        local ok, err = pcall(function() cdl.save(path) end)
        if ok then
            print("[pc98rev] CDL saved to " .. path)
            print("[pc98rev] stats: " .. cdl.stats())
        else
            print("[pc98rev] CDL save failed: " .. tostring(err))
        end
    end
}
