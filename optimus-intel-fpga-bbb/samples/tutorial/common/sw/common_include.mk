##
## Common sw build rules
##

COPT     ?= -g -std=c99
CPPFLAGS ?= -std=c++11
CXX      ?= g++
LDFLAGS  ?=

ifeq (,$(CFLAGS))
CFLAGS = $(COPT)
endif

ifneq (,$(ndebug))
else
CPPFLAGS += -DENABLE_DEBUG=1
endif
ifneq (,$(nassert))
else
CPPFLAGS += -DENABLE_ASSERT=1
endif

# stack execution protection
LDFLAGS +=-z noexecstack

# data relocation and projection
LDFLAGS +=-z relro -z now

# add by gefei
LDFLAGS += -L/usr/local/lib64

# stack buffer overrun detection
# Note that CentOS 7 has gcc 4.8 by default.  When we switch
# to a system with gcc 4.9 or newer this should be changed to
# CFLAGS="-fstack-protector-strong"
CFLAGS +=-fstack-protector

# Position independent execution
CFLAGS +=-fPIE -fPIC
LDFLAGS +=-pie

# fortify source
#CFLAGS +=-D_FORTIFY_SOURCE=2

# format string vulnerabilities
CFLAGS +=-Wformat -Wformat-security

ifeq (,$(DESTDIR))
ifeq (,$(prefix))
prefix = /usr/local
endif
CFLAGS += -I$(prefix)/include
CPPFLAGS += -I$(prefix)/include
LDFLAGS  += -L$(prefix)/lib -Wl,-rpath-link -Wl,$(prefix)/lib -Wl,-rpath -Wl,$(prefix)/lib \
            -L$(prefix)/lib64 -Wl,-rpath-link -Wl,$(prefix)/lib64 -Wl,-rpath -Wl,$(prefix)/lib64
else
ifeq (,$(prefix))
prefix = /usr/local
endif
CFLAGS += -I$(DESTDIR)$(prefix)/include
CPPFLAGS += -I$(DESTDIR)$(prefix)/include
LDFLAGS  += -L$(DESTDIR)$(prefix)/lib -Wl,-rpath-link -Wl,$(prefix)/lib -Wl,-rpath -Wl,$(DESTDIR)$(prefix)/lib \
            -L$(DESTDIR)$(prefix)/lib64 -Wl,-rpath-link -Wl,$(prefix)/lib64 -Wl,-rpath -Wl,$(DESTDIR)$(prefix)/lib64
endif

LDFLAGS += -luuid

FPGA_LIBS = -lopae-c
FPGA_VAI_LIBS += -lvai-c
ASE_LIBS += -lopae-c-ase
VAI_ASE_LIBS = -lvai-c-ase
