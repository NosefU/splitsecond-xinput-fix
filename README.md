# Split/Second gamepad fix

**English** | [Русский](README.ru.md)

![Title screen: "press (A) to start" instead of "press ENTER"](docs/images/title-screen.jpg)

Play the PC version of **Split/Second** (2010) with a gamepad only — on Windows or on Steam Deck.

The original PC release only uses the gamepad while driving: every menu needs the keyboard, and
all hints show keyboard keys or raw axis names ("Press ENTER to start", "press Axis Z Neg to
accelerate"). With this fix:

- **all menus work with the gamepad** — main menu, pause, results, options;
- **hints show Xbox buttons** instead of keys: the button bar at the bottom of the menus, the
  title screen and the tutorial hints during races;
- **driving is unchanged** — sticks and triggers stay analog;
- the controls are set once to a ready-to-play gamepad layout, so nothing has to be bound by hand.

<p>
  <img src="docs/images/menu-prompts.jpg" width="49%" alt="Car selection with Y / X / B / A in the button bar">
  <img src="docs/images/race-hint.jpg" width="49%" alt="Race hint with LT and left stick icons">
</p>

Tested on Steam Deck with the built-in controller, with both the boxed and the Steam version.
Any XInput gamepad (Xbox One / Series or compatible) should work the same way on Windows. With an
Xbox 360 pad the game already handles the menus by itself; the fix notices that and only adds the
button icons.

## Requirements

- The PC version of Split/Second: the boxed (retail) release or the Steam release. The installer
  checks `SplitSecond.exe` and refuses other versions.
- The original, unmodified `data/resident.ark`.

## Installation

Download the archive for your system from [Releases](../../releases) and unpack it:
`splitsecond-xinput-fix-*-windows.zip` for Windows, `splitsecond-xinput-fix-*-linux.tar.gz` for
Steam Deck / Linux. Each contains a single installer with everything the fix needs, plus
`INSTALL.txt`.

### Windows

1. Copy `ssfix-install.exe` into the game folder (next to `SplitSecond.exe`) and run it.
   You can also run it from anywhere with the game folder as an argument:
   `ssfix-install.exe "C:\Games\Split Second"`.
2. Start the game.

No DirectX installation is needed for the fix itself.

### Steam Deck / Linux (Proton)

1. In Desktop Mode, copy `ssfix-install` into the game folder (next to `SplitSecond.exe`).
2. Open a terminal in that folder (in Dolphin: right-click an empty spot → **Open Terminal Here**)
   and run:
   ```
   ./ssfix-install
   ```
   (if it says "Permission denied", run `chmod +x ssfix-install` first)

   Or keep `ssfix-install` anywhere and pass the game folder — replace the path below with the
   folder where *your* copy of the game is installed:
   ```
   ./ssfix-install "/home/deck/Games/Split Second"
   ```
3. In Steam Input, use a plain gamepad layout (no keyboard bindings needed).
4. Start the game.

Proton only loads the fix with a DLL override. The installer adds it to the game's Proton prefix
by itself, both for the Steam version and for a non-Steam shortcut to the game, and removes it on
uninstall. If it cannot do that (the game was never started, or is running right now), it says so;
then start or close the game and run the installer again, or add this to the game's
**Properties → Launch Options** in Steam:
```
WINEDLLOVERRIDES="xinput1_3=n,b" %command%
```

Both Proton 11.0 and Proton Experimental work. For the Steam version, Proton Experimental makes
the controller look like an Xbox 360 pad, and the game then navigates its menus by itself; the
fix notices that and only adds the button icons.

## Controls

| | In menus | In races |
|---|---|---|
| D-pad / left stick | navigate | left stick: steer |
| A | confirm | PowerPlay level 1 |
| B | back | PowerPlay level 2 |
| X / Y | extra actions shown in the button bar | Y: camera |
| RT / LT | — | throttle / brake |
| Right stick | — | look around |
| Start | back | pause |

On the first start after installation the controls are set to this layout. You can change them
in the game's options afterwards; your changes are kept.

## Uninstall

Run **`Uninstall gamepad fix.bat`** (Windows) or **`uninstall-gamepad-fix.sh`** (Linux / Steam
Deck) in the game folder — the installer puts it there. The game files are restored exactly as
they were. If you added the launch option by hand, remove it too.

## Steam updates and language changes

In the Steam version, `SplitSecond.exe` and `data/resident.ark` come with the language pack.
Changing the game language in Steam, a game update or **Verify integrity of game files** replace
them and remove the fix from the archive. Before changing the language, uninstall the fix (the
files are then exactly the original ones and Steam can update them safely); afterwards run the
installer again. If Steam has already replaced the files, just run the installer again: it notices
that and patches the new archive.

The fix supports the English and Russian Steam versions and the boxed release. Other Steam languages
may come with another `SplitSecond.exe`; then the installer reports it as unsupported and changes
nothing.

## Troubleshooting

- **The installer says the archive differs from the original.** `data/resident.ark` was already
  modified by another mod, or it is a different game version. Restore the original file first.
- **On Steam Deck nothing changes in the menus.** Check what the installer said about the Proton
  prefix; if it could not change it, add the launch option above.
- **Every press in a menu moves two items.** Please open an issue and attach the log: the game
  handles that gamepad by itself, and the fix did not recognise it.
- The fix writes a log to `Documents\ss_xinput_fix.log` (on Proton: inside the game's prefix,
  `.../pfx/drive_c/users/steamuser/Documents/`).

## For developers

How the fix works, the game's file formats, the tools and the build are described in
[`docs/`](docs): [implementation](docs/implementation.md),
[tools and formats](docs/tooling.md), [building and CI](docs/building.md).
