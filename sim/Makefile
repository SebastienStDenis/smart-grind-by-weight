# macOS simulator for the grinder firmware.
#
#   make            build sim/build/grinder-sim
#   make run        build and launch
#   make clean      drop the build directory
#
# Requires: brew install sdl2 cjson
# LVGL comes from the PlatformIO checkout; override with LVGL_DIR=... if needed.

SIM_DIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
REPO_DIR := $(patsubst %/,%,$(dir $(SIM_DIR)))

# In a git worktree the PlatformIO checkout lives in the main working tree.
GIT_COMMON_DIR := $(shell git -C $(REPO_DIR) rev-parse --path-format=absolute --git-common-dir 2>/dev/null)
MAIN_WORKTREE := $(patsubst %/.git,%,$(GIT_COMMON_DIR))

LVGL_DIR ?= $(firstword $(wildcard $(REPO_DIR)/.pio/libdeps/*/lvgl $(MAIN_WORKTREE)/.pio/libdeps/*/lvgl))

BUILD_DIR := $(SIM_DIR)/build
TARGET := $(BUILD_DIR)/grinder-sim

BREW_PREFIX := $(shell brew --prefix 2>/dev/null || echo /opt/homebrew)

INCLUDES := \
	-I$(SIM_DIR)/include \
	-I$(SIM_DIR)/src \
	-I$(REPO_DIR)/src \
	-I$(REPO_DIR)/include \
	-I$(REPO_DIR)/components/delta/include \
	-I$(LVGL_DIR) \
	-I$(LVGL_DIR)/src \
	-I$(BREW_PREFIX)/include

DEFINES := \
	-DLV_CONF_PATH='"$(SIM_DIR)/lv_conf.h"' \
	-DLV_LVGL_H_INCLUDE_SIMPLE \
	-DMOCK_BUILD \
	-DDEBUG_ENABLE_LOADCELL_MOCK=1 \
	-DSIM_BUILD=1

# DEBUG_ENABLE_GRINDER_BACKGROUND_INDICATOR is deliberately left off. The -mock
# PlatformIO env sets it to tint the whole screen dark green for mock hardware
# and dark yellow while the motor runs, which is useful on a bench device but
# makes the simulator's colours look wrong. Add it here if you want it back.

WARNINGS := -Wall -Wno-unused-parameter -Wno-unused-variable -Wno-deprecated-declarations
OPT ?= -O1 -g

CFLAGS := $(OPT) $(WARNINGS) $(INCLUDES) $(DEFINES)
CXXFLAGS := -std=gnu++17 $(CFLAGS)
LDFLAGS := -L$(BREW_PREFIX)/lib -lSDL2 -lcjson -lcurl

# Firmware sources. The hardware drivers for the panel, touch controller and
# HX711 are replaced by sim/src equivalents, and the BLE stack is stubbed.
FIRMWARE_SRCS := \
	$(REPO_DIR)/src/main.cpp \
	$(wildcard $(REPO_DIR)/src/ui/*.cpp) \
	$(wildcard $(REPO_DIR)/src/ui/components/*.cpp) \
	$(wildcard $(REPO_DIR)/src/ui/controllers/*.cpp) \
	$(wildcard $(REPO_DIR)/src/ui/screens/*.cpp) \
	$(wildcard $(REPO_DIR)/src/controllers/*.cpp) \
	$(wildcard $(REPO_DIR)/src/system/*.cpp) \
	$(wildcard $(REPO_DIR)/src/logging/*.cpp) \
	$(wildcard $(REPO_DIR)/src/tasks/*.cpp) \
	$(wildcard $(REPO_DIR)/src/network/*.cpp) \
	$(wildcard $(REPO_DIR)/src/hardware/circular_buffer_math/*.cpp) \
	$(REPO_DIR)/src/hardware/hardware_manager.cpp \
	$(REPO_DIR)/src/hardware/WeightSensor.cpp \
	$(REPO_DIR)/src/hardware/grinder.cpp \
	$(REPO_DIR)/src/hardware/mock_hx711_driver.cpp

SIM_SRCS := $(wildcard $(SIM_DIR)/src/*.cpp)
FONT_SRCS := $(wildcard $(REPO_DIR)/src/font/*.c)
LVGL_SRCS := $(shell find $(LVGL_DIR)/src -name '*.c' 2>/dev/null)

CXX_SRCS := $(FIRMWARE_SRCS) $(SIM_SRCS)
C_SRCS := $(FONT_SRCS) $(LVGL_SRCS)

CXX_OBJS := $(CXX_SRCS:%=$(BUILD_DIR)/obj%.o)
C_OBJS := $(C_SRCS:%=$(BUILD_DIR)/obj%.o)
ALL_OBJS := $(CXX_OBJS) $(C_OBJS)

GIT_INFO := $(REPO_DIR)/include/git_info.h

.PHONY: all run clean check-deps

all: check-deps $(TARGET)

check-deps:
ifeq ($(LVGL_DIR),)
	$(error LVGL not found. Run 'pio pkg install' in the repo, or pass LVGL_DIR=/path/to/lvgl)
endif
	@test -f $(BREW_PREFIX)/include/SDL2/SDL.h || { echo "SDL2 headers missing - run: brew install sdl2"; exit 1; }
	@test -f $(BREW_PREFIX)/include/cjson/cJSON.h || { echo "cJSON headers missing - run: brew install cjson"; exit 1; }

# PlatformIO regenerates this on every firmware build; the simulator only fills
# it in when it is absent, so the shared build counter is left alone.
$(GIT_INFO):
	@mkdir -p $(dir $@)
	@printf '#pragma once\n\n#define GIT_COMMIT_ID "%s"\n#define GIT_BRANCH "%s"\n#define BUILD_NUMBER 0\n#define BUILD_TIMESTAMP "simulator"\n' \
		"$$(git -C $(REPO_DIR) rev-parse --short HEAD 2>/dev/null || echo unknown)" \
		"$$(git -C $(REPO_DIR) rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)" > $@
	@echo "Generated $@ for the simulator"

$(TARGET): $(GIT_INFO) $(ALL_OBJS)
	@mkdir -p $(dir $@)
	@echo "  LD  $(notdir $@)"
	@$(CXX) $(ALL_OBJS) $(LDFLAGS) -o $@
	@echo "Built $@"

$(BUILD_DIR)/obj%.cpp.o: %.cpp $(GIT_INFO)
	@mkdir -p $(dir $@)
	@echo "  CXX $(notdir $<)"
	@$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/obj%.c.o: %.c $(GIT_INFO)
	@mkdir -p $(dir $@)
	@echo "  CC  $(notdir $<)"
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

run: all
	cd $(SIM_DIR) && ./build/grinder-sim

clean:
	rm -rf $(BUILD_DIR)

-include $(ALL_OBJS:.o=.d)
