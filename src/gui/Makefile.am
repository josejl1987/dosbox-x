AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/src/aviwriter "-DRESDIR=\"$(resdir)\"" -I$(top_srcdir)/src -I$(top_srcdir)/vs -I$(top_srcdir)/vs/imgui -I$(top_srcdir)/vs/imgui/backends -I$(top_srcdir)/vs/lua -I$(top_srcdir)/vs/lua/src -I$(top_srcdir)/src/luaengine -I$(top_srcdir)/third_party/lrdb/include -I$(top_srcdir)/third_party/lrdb/third_party/asio/asio/include -I$(top_srcdir)/third_party/lrdb/third_party/picojson -DASIO_STANDALONE -DWIN32_LEAN_AND_MEAN -D_WINSOCKAPI_=1 -DNOMINMAX -DSOL_NOEXCEPT_FUNCTION_TYPE=0

noinst_LIBRARIES = libgui.a
libgui_a_SOURCES = \
	sdlmain_linux.cpp \
	sdlmain.cpp sdl_mapper.cpp dosbox_logo.h \
	render.cpp render_scalers.cpp render_scalers.h \
	render_templates.h render_loops.h render_simple.h \
	render_templates_sai.h render_templates_hq.h \
	render_templates_hq2x.h render_templates_hq3x.h \
	midi.cpp midi_win32.h midi_oss.h midi_coreaudio.h \
	midi_alsa.h midi_coremidi.h sdl_gui.cpp dosbox_splash.h \
	menu.cpp menu_callback.cpp bitop.cpp ptrop.cpp zipcrc.c zipfile.cpp \
	imgui_window.cpp \
	$(top_srcdir)/vs/imgui/imgui.cpp \
	$(top_srcdir)/vs/imgui/imgui_demo.cpp \
	$(top_srcdir)/vs/imgui/imgui_draw.cpp \
	$(top_srcdir)/vs/imgui/imgui_tables.cpp \
	$(top_srcdir)/vs/imgui/imgui_widgets.cpp

if C_MT32
AM_CPPFLAGS += -I$(top_srcdir)/src/libs/mt32

libgui_a_SOURCES += \
	midi_mt32.h
endif

if C_SDL2
libgui_a_SOURCES += \
	$(top_srcdir)/vs/imgui/backends/imgui_impl_sdl2.cpp \
	$(top_srcdir)/vs/imgui/backends/imgui_impl_opengl3.cpp
endif

if MACOSX
libgui_a_SOURCES += \
	menu_macos.mm
endif
