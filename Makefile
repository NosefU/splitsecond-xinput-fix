CC_WIN32 ?= i686-w64-mingw32-gcc
CC_LINUX ?= cc
PYTHON   ?= python3
CFLAGS   ?= -O2 -Wall
DLLFLAGS  = -shared -static-libgcc -Wl,--enable-stdcall-fixup -Wl,--kill-at
DLLLIBS   = -lshell32
VERSION  ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)

BUILD = build
DIST  = dist
DIFFS = patch/prompt_bar.ssdf patch/press_start.ssdf
PKG   = splitsecond-xinput-fix-$(VERSION)

.PHONY: all dll diag installer-win installer-linux dist clean FORCE

all: dll installer-win installer-linux

dll: $(BUILD)/xinput1_3.dll

diag: $(BUILD)/diag/xinput1_3.dll

installer-win: $(BUILD)/win/ssfix-install.exe

installer-linux: $(BUILD)/linux/ssfix-install

$(BUILD)/xinput1_3.dll: src/xinput_fix.c src/xinput1_3.def
	@mkdir -p $(dir $@)
	$(CC_WIN32) $(CFLAGS) $(DLLFLAGS) -o $@ $^ $(DLLLIBS)

$(BUILD)/diag/xinput1_3.dll: src/xinput_fix.c src/xinput1_3.def
	@mkdir -p $(dir $@)
	$(CC_WIN32) $(CFLAGS) -DDIAG $(DLLFLAGS) -o $@ $^ $(DLLLIBS)

# rewritten only when the version string changes, so the installers pick it up
$(BUILD)/version: FORCE
	@mkdir -p $(dir $@)
	@echo $(VERSION) | cmp -s - $@ || echo $(VERSION) > $@

$(BUILD)/payload/payload.h: $(BUILD)/xinput1_3.dll $(DIFFS) tools/embed.py $(BUILD)/version
	@mkdir -p $(dir $@)
	$(PYTHON) tools/embed.py $(VERSION) $(BUILD)/xinput1_3.dll $@ $(DIFFS)

$(BUILD)/win/ssfix-install.exe: installer/ssfix_install.c $(BUILD)/payload/payload.h
	@mkdir -p $(dir $@)
	$(CC_WIN32) $(CFLAGS) -std=c99 -static -s -I$(BUILD)/payload -o $@ $<

$(BUILD)/linux/ssfix-install: installer/ssfix_install.c $(BUILD)/payload/payload.h
	@mkdir -p $(dir $@)
	$(CC_LINUX) $(CFLAGS) -std=c99 -D_FILE_OFFSET_BITS=64 -D_POSIX_C_SOURCE=200809L -static -s -I$(BUILD)/payload -o $@ $<

dist: all
	@rm -rf $(DIST)/$(PKG) && mkdir -p $(DIST)/$(PKG)
	cp $(BUILD)/win/ssfix-install.exe $(BUILD)/linux/ssfix-install $(BUILD)/xinput1_3.dll $(DIST)/$(PKG)/
	cp INSTALL.txt $(DIST)/$(PKG)/
	cd $(DIST) && rm -f $(PKG).zip && zip -qr $(PKG).zip $(PKG)
	@echo $(DIST)/$(PKG).zip

clean:
	rm -rf $(BUILD) $(DIST)
