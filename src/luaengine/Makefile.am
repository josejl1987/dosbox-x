AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/vs -I$(top_srcdir)/vs/lua \
	-I$(top_srcdir)/third_party/lrdb/include \
	-I$(top_srcdir)/third_party/lrdb/third_party/asio/asio/include \
	-I$(top_srcdir)/third_party/lrdb/third_party/picojson \
	-DASIO_STANDALONE -DWIN32_LEAN_AND_MEAN -D_WINSOCKAPI_=1 -DNOMINMAX

if C_LUA
noinst_LIBRARIES = libluaengine.a
libluaengine_a_SOURCES = LuaEngine.cpp luaengine.h event_manager.cpp event_manager.h lua_memory_domains.cpp lua_memory_domains.h debug_utils.h debug_window_base.h savestate_manager.cpp savestate_manager.h savestate_shim.cpp frame_control.cpp frame_control.h gui_windows.cpp gui_windows.h lua_api_memory.cpp lua_api_cpu.cpp lua_api_debug.cpp lua_re_hooks.cpp lua_re_hooks.h \
	debugger_session.cpp debugger_session.h \
	watch_list.cpp watch_list.h \
	hex_editor_window.cpp hex_editor_window.h \
	trace_logger.cpp trace_logger.h \
	trace_logger_window.cpp trace_logger_window.h \
	core_debug_interface.cpp core_debug_interface.h \
	debug_logger.cpp debug_logger.h \
	debug_config.cpp debug_config.h \
	symbol_manager.cpp symbol_manager.h \
	symbolic_breakpoints.cpp symbolic_breakpoints.h \
	lua_symbol_bindings.cpp lua_symbol_bindings.h \
	lua_api_breakpoint.cpp \
	autocomplete.cpp autocomplete.h \
	disassembly_window.cpp disassembly_window.h \
	execution_toolbar.cpp execution_toolbar.h \
	memory_reference_analyzer.cpp memory_reference_analyzer.h \
	ram_search_engine.cpp ram_search_engine.h \
	ram_search_window.cpp ram_search_window.h \
	watch_window.cpp watch_window.h \
	cheat_engine.cpp cheat_engine.h \
	cheat_window.cpp cheat_window.h \
	instrumentation_router.cpp instrumentation_router.h \
	else
noinst_LIBRARIES = 
endif

EXTRA_DIST = 
