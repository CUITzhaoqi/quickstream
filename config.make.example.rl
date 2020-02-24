
PREFIX = /usr/local/encap/quickstream-@PACKAGE_VERSION@

# Set CPPFLAGS for debug build options:
#
#
# SPEW_LEVEL_* controls macros in lib/debug.h
#
# The following may be defined; defining them turns on the following CPP
# macro functions that are in debug.h which is source for
# libquickstream.so
#
#
# DEBUG             -->  DASSERT()
#
# SPEW_LEVEL_DEBUG  -->  DSPEW() INFO() NOTICE() WARN() ERROR()
# SPEW_LEVEL_INFO   -->  INFO() NOTICE() WARN() ERROR()
# SPEW_LEVEL_NOTICE -->  NOTICE() WARN() ERROR()
# SPEW_LEVEL_WARN   -->  WARN() ERROR()
# SPEW_LEVEL_ERROR  -->  ERROR()
#
# always on is      -->  ASSERT()


# Example:
#CPPFLAGS := -DSPEW_LEVEL_NOTICE
CPPFLAGS := -DDEBUG -DSPEW_LEVEL_DEBUG


# C compiler option flags
CFLAGS := -g -Wall -Werror
