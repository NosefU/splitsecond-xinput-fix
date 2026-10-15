CC_WIN32 ?= i686-w64-mingw32-gcc
CFLAGS   ?= -O2 -Wall
DLLFLAGS  = -shared -static-libgcc -Wl,--enable-stdcall-fixup -Wl,--kill-at

BUILD = build

.PHONY: all dll diag clean

all: dll

dll: $(BUILD)/xinput1_3.dll

diag: $(BUILD)/diag/xinput1_3.dll

$(BUILD)/xinput1_3.dll: src/xinput_fix.c src/xinput1_3.def
	@mkdir -p $(dir $@)
	$(CC_WIN32) $(CFLAGS) $(DLLFLAGS) -o $@ $^

$(BUILD)/diag/xinput1_3.dll: src/xinput_fix.c src/xinput1_3.def
	@mkdir -p $(dir $@)
	$(CC_WIN32) $(CFLAGS) -DDIAG $(DLLFLAGS) -o $@ $^

clean:
	rm -rf $(BUILD)
