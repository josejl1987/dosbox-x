AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/vs -I$(top_srcdir)/vs/lua -I$(top_srcdir)/vs/imgui \
	-I$(top_srcdir)/third_party/lrdb/include \
	-I$(top_srcdir)/third_party/lrdb/third_party/asio/asio/include \
	-I$(top_srcdir)/third_party/lrdb/third_party/picojson -DASIO_STANDALONE

noinst_LIBRARIES = libmemory.a
libmemory_a_SOURCES = memory_domains.cpp

EXTRA_DIST = 