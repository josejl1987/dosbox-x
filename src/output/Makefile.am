AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/src \
	-I$(top_srcdir)/vs -I$(top_srcdir)/vs/lua -I$(top_srcdir)/vs/imgui \
	-I$(top_srcdir)/third_party/lrdb/include \
	-I$(top_srcdir)/third_party/lrdb/third_party/asio/asio/include \
	-I$(top_srcdir)/third_party/lrdb/third_party/picojson -DASIO_STANDALONE

SUBDIRS =

if C_DIRECT3D
SUBDIRS += direct3d
endif

noinst_LIBRARIES = liboutput.a
liboutput_a_SOURCES = output_direct3d.cpp output_opengl.cpp output_surface.cpp output_tools.cpp output_tools_xbrz.cpp output_ttf.cpp

if C_GAMELINK
liboutput_a_SOURCES += output_gamelink.cpp
endif
