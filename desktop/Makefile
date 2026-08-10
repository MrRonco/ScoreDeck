# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Franco Raso
# Makefile — builds ScoreDeck's UI as a macOS app. See README.md.
#
# Plain make on purpose: the only thing you need installed is SDL2, and the
# LVGL source already on disk for the Arduino build is reused as-is. No cmake,
# no second copy of LVGL, no submodule.

LVGL    ?= $(HOME)/Documents/Arduino/libraries/lvgl
SRC      = ../firmware/ScoreDeck/src
BUILD    = build
BIN      = scoredeck-ui

SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS   := $(shell sdl2-config --libs)

# LV_CONF_PATH points LVGL at the firmware's lv_conf.h VERBATIM — same colour
# depth, same LV_INV_BUF_SIZE, same allocator. It asks for two ESP-only symbols
# (ps_malloc, millis); shim/ supplies both, which is cheaper than a second
# lv_conf that would drift out of sync and quietly stop showing what the panel
# shows.
CPPFLAGS = -DLV_CONF_PATH=$(CURDIR)/../firmware/lv_conf.h \
           -I$(LVGL) -I$(LVGL)/src -Ishim -I$(SRC) $(SDL_CFLAGS)
# -MMD -MP so editing a shim header actually rebuilds what included it.
DEPFLAGS = -MMD -MP
WARN     = -Wall -Wno-unused-parameter -Wno-unused-variable -Wno-missing-field-initializers
CFLAGS   = -O1 -g $(WARN) $(DEPFLAGS)
CXXFLAGS = -O1 -g -std=gnu++17 $(WARN) $(DEPFLAGS)
LDLIBS   = $(SDL_LIBS)

# Firmware sources that are pure UI or pure logic. Anything touching the
# network, flash or the panel is faked in fakes.cpp instead.
FW_CXX = $(SRC)/ui/ui_board.cpp $(SRC)/ui/ui_idle.cpp $(SRC)/ui/ui_game.cpp \
         $(SRC)/ui/ui_standings.cpp $(SRC)/ui/ui_news.cpp $(SRC)/ui/ui_lineup.cpp \
         $(SRC)/ui/ui_alert.cpp $(SRC)/ui/ui_setup.cpp $(SRC)/ui/ui_focus.cpp $(SRC)/ui/ui_settings.cpp \
         $(SRC)/ui/theme.cpp \
         $(SRC)/core/state.cpp
FW_C   = $(wildcard $(SRC)/assets/*.c)
LV_C   = $(shell find $(LVGL)/src -name '*.c')
OWN    = main.cpp fakes.cpp scenarios.cpp lint_fonts.cpp

OBJ = $(patsubst %,$(BUILD)/%.o,$(notdir $(OWN)) $(notdir $(FW_CXX)) $(notdir $(FW_C))) \
      $(patsubst $(LVGL)/%,$(BUILD)/lv/%.o,$(LV_C))

VPATH = .:$(SRC)/ui:$(SRC)/core:$(SRC)/assets

all: $(BIN)

$(BIN): $(OBJ)
	@echo "  LD  $@"
	@$(CXX) $(OBJ) $(LDLIBS) -o $@

$(BUILD)/%.cpp.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "  CXX $<"
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD)/%.c.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC  $<"
	@$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/lv/%.c.o: $(LVGL)/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Render every scenario to a BMP without a display attached.
shots: $(BIN)
	@mkdir -p shots
	@i=0; while [ $$i -lt 10 ]; do ./$(BIN) --shot shots/scn$$i.bmp --scenario $$i; i=$$((i+1)); done

# Every secondary screen, on the typical board. These are unreachable from a
# scenario alone: each one clears its state in Open() and waits on a fetch.
SCREENS = board idle game standings news lineup player alert banner setup settings
screens: $(BIN)
	@mkdir -p shots
	@for s in $(SCREENS); do ./$(BIN) --shot shots/scr-$$s.bmp --screen $$s; done

lint: $(BIN)
	@./$(BIN) --lint

clean:
	@rm -rf $(BUILD) $(BIN) shots shot.bmp

-include $(OBJ:.o=.d)

.PHONY: all clean shots screens lint
