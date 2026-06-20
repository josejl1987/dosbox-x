AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/src "-DRESDIR=\"$(resdir)\"" \
	-I$(top_srcdir)/vs -I$(top_srcdir)/vs/lua -I$(top_srcdir)/vs/imgui \
	-I$(top_srcdir)/third_party/lrdb/include \
	-I$(top_srcdir)/third_party/lrdb/third_party/asio/asio/include \
	-I$(top_srcdir)/third_party/lrdb/third_party/picojson -DASIO_STANDALONE

resdir = $(datarootdir)/dosbox-x

noinst_LIBRARIES = libmisc.a
libmisc_a_SOURCES = clipboard.cpp cross.cpp ethernet.cpp ethernet_pcap.cpp ethernet_slirp.cpp ethernet_nothing.cpp mem_capture.cpp messages.cpp programs.cpp setup.cpp support.cpp regionalloctracking.cpp savestates.cpp shiftjis.cpp iconvpp.cpp mkdir_p.cpp
