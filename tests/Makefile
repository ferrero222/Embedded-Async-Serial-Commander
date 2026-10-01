##############################################################################
# Product: Makefile for ET (embedded test) for C CUT on the HOST
# GitHub: https://github.com/QuantumLeaps/ET
#
#                    Q u a n t u m  L e a P s
#                    ------------------------
#                    Modern Embedded Software
#
# Copyright (C) 2005 Quantum Leaps, <state-machine.com>.
#
# SPDX-License-Identifier: MIT
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
##############################################################################
#
# NOTE:
# To use this Makefile on Windows, you will need the GNU make utility, which
# is included in the QTools collection for Windows, see:
#    https://github.com/QuantumLeaps/qtools
#

#-----------------------------------------------------------------------------
# project name:
PROJECT := simcom_tact
ET_DIR  := ../libs/embedded-test/et

# list of all source directories used by this project
VPATH := . \
	../libs/o1heap/o1heap \
	../chain \
	../port \
	../core \
	../modules/simcom/general \
	../modules/simcom/sms \
	../modules/simcom/gprs \
	../modules/simcom/mqtt \
	../libs/ringslice/src \
	$(ET_DIR)

# list of all include directories needed by this project
INCLUDES := -I. \
	-I../libs/o1heap/o1heap \
	-I../chain \
	-I../core \
	-I../port \
	-I../modules/simcom/general \
	-I../modules/simcom/sms \
	-I../modules/simcom/gprs \
	-I../modules/simcom/mqtt \
	-I../libs/ringslice/src \
	-I../libs/ringslice/src/config \
	-I$(ET_DIR)

#-----------------------------------------------------------------------------
# project files:
#

# C source files...
C_SRCS := \
	o1heap.c \
	tact_chain.c \
	tact_core.c \
	tact_mdl_general.c \
	tact_mdl_sms.c \
	tact_mdl_gprs.c \
	tact_mdl_gprs_server.c \
	tact_mdl_mqtt.c \
	ringslice.c \
	ringslice_scanf.c \
	test.c \
	tact_port.c \
	et.c \
	et_host.c

# C++ source files...
CPP_SRCS :=

LIB_DIRS :=
LIBS     :=

# defines...
# Use a larger pool for host tests because 64-bit hosts have wider pointers
# and therefore larger queued descriptors than the embedded 32-bit targets.
DEFINES  := -DTACT_TEST -DTACT_MEMORY_POOL_SIZE=8192

#============================================================================
# Typically you should not need to change anything below this line

#-----------------------------------------------------------------------------
# GNU toolset:
#
# NOTE:
# GNU toolset (MinGW) is included in the QTools collection for Windows, see:
#     https://www.state-machine.com/qtools
# It is assumed that %QTOOLS%\bin directory is added to the PATH
#
CC    = gcc
CPP   = g++
LINK  = $(CC) # for C programs
#LINK  := g++   # for C++ programs

#-----------------------------------------------------------------------------
# basic utilities (depends on the OS this Makefile runs on):
#
ifeq ($(OS),Windows_NT)
	MKDIR      := mkdir
	RM         := rm
	TARGET_EXT := .exe
else ifeq ($(OSTYPE),cygwin)
	MKDIR      := mkdir -p
	RM         := rm -f
	TARGET_EXT := .exe
else
	MKDIR      := mkdir -p
	RM         := rm -f
	TARGET_EXT :=
endif

#-----------------------------------------------------------------------------
# build options...

BIN_DIR ?= build

CFLAGS  ?= -g -O0 -fno-pie -std=c11 -pedantic -Wall -Wextra -W
ARCH_FLAGS ?=
C_BUILD_FLAGS = $(CFLAGS) $(ARCH_FLAGS) $(INCLUDES) $(DEFINES) -DQ_HOST

CPPFLAGS ?= -g -O0 -fno-pie -std=c++11 -pedantic -Wall -Wextra -fno-rtti -fno-exceptions
CPP_BUILD_FLAGS = $(CPPFLAGS) $(ARCH_FLAGS) $(INCLUDES) $(DEFINES) -DQ_HOST

ifndef GCC_OLD
	LINKFLAGS ?= -no-pie
endif

ifdef GCOV
	CFLAGS    += -fprofile-arcs -ftest-coverage
	CPPFLAGS  += -fprofile-arcs -ftest-coverage
	LINKFLAGS += -lgcov --coverage
endif

#-----------------------------------------------------------------------------
C_OBJS       := $(patsubst %.c,%.o,   $(C_SRCS))
CPP_OBJS     := $(patsubst %.cpp,%.o, $(CPP_SRCS))

TARGET_EXE   := $(BIN_DIR)/$(PROJECT)$(TARGET_EXT)
C_OBJS_EXT   := $(addprefix $(BIN_DIR)/, $(C_OBJS))
C_DEPS_EXT   := $(patsubst %.o,%.d, $(C_OBJS_EXT))
CPP_OBJS_EXT := $(addprefix $(BIN_DIR)/, $(CPP_OBJS))
CPP_DEPS_EXT := $(patsubst %.o,%.d, $(CPP_OBJS_EXT))

#-----------------------------------------------------------------------------
# rules
#

.PHONY : all run norun check test clean show

all : check
norun : $(TARGET_EXE)

$(TARGET_EXE) : $(C_OBJS_EXT) $(CPP_OBJS_EXT)
	$(LINK) $(LINKFLAGS) $(ARCH_FLAGS) $(LIB_DIRS) -o $@ $^ $(LIBS)

run : $(TARGET_EXE)
	$(TARGET_EXE)

check test : $(TARGET_EXE)
	$(TARGET_EXE)

$(BIN_DIR)/%.d : %.cpp
	$(CPP) -MM -MT $(@:.d=.o) $(CPP_BUILD_FLAGS) $< > $@

$(BIN_DIR)/%.d : %.c
	$(CC) -MM -MT $(@:.d=.o) $(C_BUILD_FLAGS) $< > $@

$(BIN_DIR)/%.o : %.c
	$(CC) -c $(C_BUILD_FLAGS) $< -o $@

$(BIN_DIR)/%.o : %.cpp
	$(CPP) -c $(CPP_BUILD_FLAGS) $< -o $@

# create BIN_DIR and include dependencies only if needed
ifneq ($(MAKECMDGOALS),clean)
  ifneq ($(MAKECMDGOALS),show)
     ifneq ($(MAKECMDGOALS),debug)
ifeq ("$(wildcard $(BIN_DIR))","")
$(shell $(MKDIR) $(BIN_DIR))
endif
-include $(C_DEPS_EXT) $(CPP_DEPS_EXT)
     endif
  endif
endif

clean :
	-$(RM) $(C_OBJS_EXT) $(C_DEPS_EXT) $(CPP_OBJS_EXT) $(CPP_DEPS_EXT) $(TARGET_EXE)

show :
	@echo PROJECT      = $(PROJECT)
	@echo CC           = $(CC)
	@echo ARCH_FLAGS   = $(ARCH_FLAGS)
	@echo CFLAGS       = $(C_BUILD_FLAGS)
	@echo TARGET_EXE   = $(TARGET_EXE)
	@echo VPATH        = $(VPATH)
	@echo C_SRCS       = $(C_SRCS)
	@echo CPP_SRCS     = $(CPP_SRCS)
	@echo C_DEPS_EXT   = $(C_DEPS_EXT)
	@echo C_OBJS_EXT   = $(C_OBJS_EXT)
	@echo C_DEPS_EXT   = $(C_DEPS_EXT)
	@echo CPP_DEPS_EXT = $(CPP_DEPS_EXT)
	@echo CPP_OBJS_EXT = $(CPP_OBJS_EXT)
	@echo LIB_DIRS     = $(LIB_DIRS)
	@echo LIBS         = $(LIBS)
	@echo DEFINES      = $(DEFINES)

