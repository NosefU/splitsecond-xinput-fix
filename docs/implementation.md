# How the fix works

**English** | [Русский](implementation.ru.md)

The fix consists of an `xinput1_3.dll` proxy placed next to `SplitSecond.exe`, changes to two UI
movies inside `data/resident.ark`, and an installer that applies and removes all of it.

Four builds of `SplitSecond.exe` are supported, all protected by SecuROM:

| Build | `TimeDateStamp` | `SizeOfImage` | PE header |
|---|---|---|---|
| boxed (retail) | `0x4BE3EA74` | `0x1A64000` | `0xD40` |
| Steam: English, German, French, Italian, Spanish (with an extra Steam DRM section `.bind`) | `0x4BE13C66` | `0x1DB7000` | `0xD20` |
| Steam: Russian (the retail exe with the Steam DRM wrapper) | `0x4BE3EA74` | `0x1DBF000` | `0xD20` |
| Steam: Polish | `0x4BE13CC6` | `0x1DBC000` | `0xD20` |

The code is the same, only moved: in the Western and Polish Steam builds `.data` is `0x1000` lower
and the functions are a few hundred bytes lower; the Russian Steam build uses the retail
addresses. In Steam the exe and `data/resident.ark` come with the language depot: the five
Western languages share both files (switching between them only rewrites `ssversion.ini`, and the
fix stays), while Russian and Polish have their own, so switching to or from them replaces the
files and the installer has to be run again. The addresses below are for the retail build; the Steam ones are in
the `builds` table in `src/xinput_fix.c`. They were found with `tools/port_addrs.py` on memory
dumps of both builds (see [tools](tooling.md)). With any other exe the proxy changes nothing and
only forwards XInput calls. The two UI movies are byte-identical in all of them.

## XInput proxy (`src/xinput_fix.c`)

The game imports only `XInputSetState` (rumble) from `xinput1_3.dll`; it reads the gamepad itself
through DirectInput (device "Joy 0"). The proxy exports the same functions with the same ordinals
(`src/xinput1_3.def`), forwards them to the system XInput (`xinput1_3`, falling back to
`xinput1_4` or `xinput9_1_0`) and starts a thread that does the rest.

### Menu navigation

The thread polls the gamepad 100 times a second. While the game is in a menu (front-end, pause,
results), button presses are turned into key presses with `SendInput`:

| Gamepad | In menus | In races |
|---|---|---|
| D-pad / left stick | arrow keys (with auto-repeat) | — |
| A / B | Enter / Esc | — |
| X / Y | Space / Tab | — |
| Start | Esc | Esc (pause) |

A button held while the game switches between menu and race only acts after it is released. In
races the proxy stays out of the way: sticks, triggers and buttons reach the game through
DirectInput as bound in its options. The game pauses only on Esc, so Start → Esc is always active.

### Xbox 360 pads

The game sorts DirectInput devices by `guidProduct` (function `0x40DB10`): the Xbox 360 pad
(`045E:028E`) and the wireless receiver (`045E:02A1`) get type 0, several Logitech, Saitek and
other devices get their own types. With an Xbox 360 pad the game navigates its menus by itself, so
key emulation would make every press count twice. The proxy enumerates DirectInput game
controllers every 3 s and switches key emulation off while such a pad is attached.

On Steam Deck this is what Proton Experimental does for the Steam version (app 297860): it sets
`PROTON_SPOOF_STEAMINPUT_VIDPID=1`, which gives the Steam Input virtual pad the Xbox 360 IDs.

### DirectInput fallback

Under Proton 11.0 the Steam version sees no pad through XInput at all (all slots return
`ERROR_DEVICE_NOT_CONNECTED`), while DirectInput — and so the game in races — sees it. When XInput
slot 0 is empty, the proxy reads the first DirectInput game controller instead, with the layout
the game assumes for an Xbox pad: buttons 0–7 are A, B, X, Y, LB, RB, Back, Start, the POV hat is
the d-pad, the X/Y axes are the left stick. It uses its own small data format (stick, hat,
8 buttons) instead of `c_dfDIJoystick`, which would pull in `dxguid`. Devices are re-enumerated
every 3 s together with the Xbox 360 check.

### Menu and race detection

Menus and races are told apart by two globals in the exe's `.data`, found by comparing memory
snapshots taken in different game states:

- `[0xD4456C] == 0` — not in a race (front-end);
- `[0xD8C104] != 0` — the race is paused or the results are shown.

### Button icons in race hints

Hint texts are stored with tokens: `PRESS *ACTION_THROTTLE* TO ACCELERATE`. The function at
`0x562350` (`cdecl (const char *action, String *out)`) expands a token into the name of the key
or axis bound for the device the game considers active (`[[0xD65E08] + 0x6C]`). If it returns
`UNKNOWN`, the token stays in the text as is; such tokens (`*BTN_RT*`, `*BLUE_PP*`, …) are turned
into images by the HUD movie `Hud.gfx` through `setImageSubstitutions`.

The proxy hooks that function (a jump to its own function; the first 7 bytes move to a trampoline).
For the listed actions it reads the binding from the joystick profile and returns an image token:

| Binding | Token |
|---|---|
| axis Z− / Z+ (triggers) | `*BTN_RT*` / `*BTN_LT*` |
| axes X/Y (left stick) | `*BTN_LS*` |
| buttons 0 / 1 / 8 | `*BTN_A*` / `*BTN_B*` / `*BTN_L3*` |
| X, Y, LB, RB, Back, Start, right stick | a short label (`X`, `Y`, `LB`, …) |

If the action is not bound on the gamepad, the original function runs.

Control profiles: `[0xD656C8]` → object, `+0xCF6C` — number of profiles, `+0xCF70` — array of
`0x584`-byte profiles. In a profile, `+0x564` is the device type (1 — keyboard), `+0x568` — its
index. Bindings are 20-byte records starting at slot `0x1C`:
`{ action id, key (VK), joystick code, kind, 0 }`, where kind 0 is a button, 1 an axis, 3 unbound.
Slots: throttle `0x1C`, brake `0x1D`, steering `0x1E`/`0x1F`, camera `0x24`, look `0x2A–0x2D`,
PowerPlay `0x33`/`0x34`.

The game code is encrypted by SecuROM and decrypted in memory at startup, so the hook is installed
as soon as the function's bytes match the expected ones (prologue and
`mov edi, "ACTION_THROTTLE"` at `+0x8C`).

### Controls reset

The hints and the menu mapping assume a layout with the gamepad bound as Joy 0 (RT throttle,
LT brake, left stick steering, A/B PowerPlay, Y camera, right stick look) and the keyboard on the
arrow keys. By default the game has no gamepad bindings, so the installer leaves an
`ssfix_reset_controls` marker next to the exe. While it exists, the proxy sets this layout in
profiles 0 (joystick) and 1 (keyboard) once a second — only if the profile structure looks as
expected.

The save file (`Documents\Disney Interactive Studios\Split Second\*.sav`) is encrypted, so the
game has to write it itself. As soon as the save file is updated after the reset, the marker is
deleted and later changes in the controls options are kept as usual.

### Log

`%USERPROFILE%\Documents\ss_xinput_fix.log` (on Proton: inside the prefix,
`.../pfx/drive_c/users/steamuser/Documents/`): exe version, the XInput DLL in use, menu/race
switches, hook installation, controls reset.

## UI changes in `data/resident.ark`

The game's UI consists of Scaleform movies (`.gfx`, SWF with a `GFX` signature) inside
`resident.ark`. Two movies are changed.

### `UI/CommonUI/Prompt_Bar.gfx` (entry `266e5f18`)

The button hint bar at the bottom of the menus. Each button's icon (`buttonA_mc` …
`buttonSTART_mc`) is a clip with frames `Xenon_*`, `PS3_*` and `PC_KeyBlank` / `PC_GenKey` /
`PC_GenKeyLong`. The PC version switches the icons to the keyboard frames and writes the key name
into them.

`tools/padicons.py` gives every button its own icon clip that keeps all frame labels but shows
that button's Xbox glyph on every frame. Whatever frame the game selects, the gamepad button is
shown, with no delay or flicker. The glyph sits in a nested clip `glyph_mc` so that
`ui/padicon_frame1.as` can correct its proportions (see below).

### `UI/Frontend/Flash/FE_pressStart_Overlay.gfx` (entry `51bf9dda`)

The "PRESS ENTER TO START" title screen. The text is the localization key `TEXT_PRESS_START`,
translated only after the first frame. `tools/pressstart.py` adds a clip with the A button (the
same shape and atlas as in `Prompt_Bar`) and names the text field `press_txt`. The script
`ui/pressstart_text.as` waits for the translation, replaces the name of the Enter key with spaces
as wide as the icon and puts the icon there. The translations live in per-language string tables
(`DICT` entries of `resident.ark`); the key is called `ENTER` in English, German and Russian,
`ENTRÉE` in French, `INVIO` in Italian and `INTRO` in Spanish, and the script looks for these. If a
translation contains none of them, the text is left alone.

### Engine quirks

- The game only draws images from its texture packs (`*.textures`, `SXET` signature), through its
  own `cGfxTextureManager::cImageCreator`. Bitmaps embedded in a movie and `BitmapData` are not
  drawn, and neither are references to images missing from the packs (many are left over from the
  console versions). So the icons are shapes filled from the `circle_buttons` atlas of the
  CommonUI pack, which is always loaded.
- `setImageSubstitutions` does not apply to text that is already shown.
- `getExactCharBoundaries` returns the inked part of a glyph (empty for a space) and ignores line
  alignment; the icon position is computed from the neighbouring characters plus the centering
  offset.
- The UI is drawn with `exactFit`: the 1280×720 stage is stretched to the whole screen. On
  non-16:9 screens (Steam Deck is 16:10) the round icons get squashed; the scripts widen them by
  `(screen height / 720) / (screen width / 1280)` using
  `System.capabilities.screenResolutionX/Y`.

## Installer (`installer/ssfix_install.c`)

1. Checks the `SplitSecond.exe` build from its PE header (`TimeDateStamp` and `SizeOfImage`).
2. For each change (`patch/*.ssdf`) finds the entry in `data/resident.ark` by name hash, unpacks
   it and checks its size and CRC32 against the ones the diff was made for. If the archive was
   already modified or the game version differs, it refuses and changes nothing.
3. Saves the original 20-byte table records and the archive size to `data/resident.ark.ssfix`.
4. Applies the diffs, appends the results to the end of the archive (aligned to `0x800`) and
   points the table records at them (`flag = 0`, uncompressed). The rest of the archive is
   untouched.
5. Writes `xinput1_3.dll` and the controls reset marker. A foreign DLL with the same name is kept
   as `.bak`; its own DLL (any version) is recognised by the `SSFIX-XINPUT-PROXY` string inside.
6. Copies itself to `ssfix/` and puts an uninstall script next to the exe.
7. Linux only: enables the DLL in the game's Proton prefix, so no launch option is needed. It finds
   the prefix `compatdata/<appid>` of the Steam version (the library whose `steamapps/common`
   holds the game, `appmanifest_*.acf` with that `installdir`) or of a non-Steam shortcut (binary
   `userdata/*/config/shortcuts.vdf`: the entry whose `Exe` or `StartDir` is the game folder,
   its `appid`), in the native and Flatpak Steam and all library folders. In `pfx/user.reg` it adds
   `"xinput1_3"="native,builtin"` under `Software\Wine\AppDefaults\SplitSecond.exe\DllOverrides`,
   unless a `wineserver` is running (Wine would overwrite the file on exit), and lists the changed
   files in `ssfix/proton-prefixes.txt`.

Without a game folder, and with no `SplitSecond.exe` in the current folder or next to the installer,
the Linux installer looks for the game through Steam: `appmanifest_297860.acf` in every library
(the Steam version) and non-Steam shortcuts whose `Exe` is a `SplitSecond.exe`. It lists each
copy with the build and the state of the fix — not installed, installed (this version: the DLL is
byte for byte the embedded one, or another version), needs reinstalling (the DLL is there but the
archive is not patched, e.g. after Steam replaced it), unsupported — and asks what to do.

Running it again first rolls back the previous installation, so the archive does not grow.
Uninstall restores the table records, truncates the archive to its original size (it then matches
the original byte for byte) and removes the DLL (restoring a `.bak`), the marker, the script,
`ssfix/` and the Proton overrides it added. If Steam replaced the archive in the meantime (an
update, a language change), the saved records no longer match it; they are dropped and the
archive is left alone.
