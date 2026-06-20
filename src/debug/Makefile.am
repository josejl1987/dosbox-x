AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/vs -I$(top_srcdir)/vs/lua -I$(top_srcdir)/vs/imgui \
	-I$(top_srcdir)/third_party/lrdb/include \
	-I$(top_srcdir)/third_party/lrdb/third_party/asio/asio/include \
	-I$(top_srcdir)/third_party/lrdb/third_party/picojson -DASIO_STANDALONE

noinst_LIBRARIES = libdebug.a
libdebug_a_SOURCES = debug.cpp debug_gui.cpp debug_disasm.cpp debug_inc.h disasm_tables.h debug_win32.cpp