AM_CPPFLAGS = -I$(top_srcdir)/vs/lua/src

noinst_LIBRARIES = liblua.a

liblua_a_SOURCES = \
	src/lapi.c src/lapi.h \
	src/lauxlib.c src/lauxlib.h \
	src/lbaselib.c \
	src/lcode.c src/lcode.h \
	src/lcorolib.c \
	src/lctype.c src/lctype.h \
	src/ldebug.c src/ldebug.h \
	src/ldo.c src/ldo.h \
	src/ldump.c \
	src/lfunc.c src/lfunc.h \
	src/lgc.c src/lgc.h \
	src/linit.c \
	src/liolib.c \
	src/llex.c src/llex.h \
	src/lmathlib.c \
	src/lmem.c src/lmem.h \
	src/loadlib.c \
	src/lobject.c src/lobject.h \
	src/lopcodes.c src/lopcodes.h \
	src/loslib.c \
	src/lparser.c src/lparser.h \
	src/lstate.c src/lstate.h \
	src/lstring.c src/lstring.h \
	src/lstrlib.c \
	src/ltable.c src/ltable.h \
	src/ltablib.c \
	src/ltm.c src/ltm.h \
	src/lundump.c src/lundump.h \
	src/lutf8lib.c \
	src/lvm.c src/lvm.h \
	src/lzio.c src/lzio.h

EXTRA_DIST = \
	src/lua.c src/luac.c \
	src/lprefix.h src/lopnames.h src/ljumptab.h \
	sol/sol.hpp sol/config.hpp sol/forward.hpp
