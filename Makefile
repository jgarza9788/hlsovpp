CXX ?= g++

EXTRA_FLAGS =
LUA_PKG ?= $(shell pkg-config --exists 'lua5.4 >= 5.4' && echo lua5.4 || echo lua)
VERSION_HEADER = .build/PluginVersion.hpp
VERSION_SCRIPT = scripts/generate-plugin-version.sh

ifeq ($(CXX),g++)
    EXTRA_FLAGS += -fno-gnu-unique
endif

.PHONY: all clean test FORCE

all: $(VERSION_HEADER)
	$(CXX) -shared -fPIC $(EXTRA_FLAGS) -I.build main.cpp Config.cpp DropIndicator.cpp NativeDrag.cpp OverviewGesture.cpp OverviewManager.cpp OverviewPassElement.cpp OverviewRender.cpp Window.cpp Motion.cpp TiltPassElement.cpp scrollOverview.cpp -o scrolloverview.so -g `pkg-config --cflags pixman-1 libdrm hyprland pangocairo libinput libudev wayland-server xkbcommon '$(LUA_PKG) >= 5.4'` -std=c++2b -Wno-narrowing

$(VERSION_HEADER): FORCE $(VERSION_SCRIPT)
	sh $(VERSION_SCRIPT) $@ .

FORCE:

clean:
	rm -f ./scrolloverview.so $(VERSION_HEADER)

# hlsovpp: Motion unit tests (no Hyprland needed) under ASan + UBSan, then the
# panel model and installer tests. `grep -v` only hides PASS lines; pipefail
# keeps a failing test failing.
test: SHELL := /bin/bash
test: .SHELLFLAGS := -o pipefail -c
test: .build/motion_test
	@.build/motion_test | grep -v '^PASS'
	@node tests/model.test.js | grep -v '^PASS'
	@bash tests/setup.test.sh | grep -v '^PASS'

.build/motion_test: Motion.cpp Motion.hpp tests/motion_test.cpp
	@mkdir -p .build
	$(CXX) -std=c++23 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all Motion.cpp tests/motion_test.cpp -o $@
