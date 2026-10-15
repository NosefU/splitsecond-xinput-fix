# Сборка

[English](building.md) | **Русский**

## Патч

Нужны `make`, `i686-w64-mingw32-gcc`, компилятор C для Linux (статическая сборка: `musl-gcc`
или `zig cc -target x86_64-linux-musl`), Python 3 и `zip`.

```
make dll                     # build/xinput1_3.dll
make diag                    # build/diag/xinput1_3.dll — исследовательская сборка
make all CC_LINUX=musl-gcc   # DLL + установщики build/win/ssfix-install.exe, build/linux/ssfix-install
make dist CC_LINUX=musl-gcc  # dist/splitsecond-xinput-fix-<версия>.zip
```

На macOS: `brew install mingw-w64 zig`, затем
`make dist CC_LINUX="zig cc -target x86_64-linux-musl"`. Версия берётся из `git describe`.

DLL и диффы встраиваются в оба установщика (`tools/embed.py` → `build/payload/payload.h`), так
что для установки нужен только один файл. В архив релиза кладутся оба установщика,
`xinput1_3.dll` (для ручной установки) и `INSTALL.txt`.

## Диффы интерфейса

Правки роликов лежат в репозитории как бинарные диффы `patch/*.ssdf` (≈5 КБ) относительно
оригинальных записей архива — ресурсов игры в репозитории нет, и для сборки патча игра не нужна.

Формат SSDF (`tools/mkdiff.py`), little-endian:

```
"SSDF" u32 version=1
u32 name_hash                   запись .ark, к которой применяется дифф
u32 source_size, source_crc32
u32 target_size, target_crc32
операции до конца файла:
  0x01 u32 offset u32 length    скопировать из оригинала
  0x02 u32 length bytes...      вставить байты
```

Если меняются `tools/padicons.py`, `tools/pressstart.py`, `tools/swf.py` или `ui/*.as`, диффы
пересобираются из оригинального архива игры:

```
tools/make_diffs.sh "<папка игры>/data/resident.ark" path/to/ffdec.jar
```

Нужны Python 3 (без дополнительных модулей) и Java для
[JPEXS FFDec](https://github.com/jindrapetrik/jpexs-decompiler), которым компилируется
ActionScript. Скрипт извлекает два ролика, собирает изменённые версии в `build/ui/` и пишет
`patch/prompt_bar.ssdf` и `patch/press_start.ssdf`. Результат детерминирован: на одном и том же
архиве получаются те же байты.

## CI

`.github/workflows/build.yml` на каждый push и PR собирает DLL и оба установщика на Ubuntu
(`gcc-mingw-w64-i686`, `musl-gcc`) и выкладывает zip как артефакт. На тег `v*` тот же zip
прикрепляется к GitHub-релизу, текст релиза берётся из `INSTALL.txt`:

```
git tag v1.0.0 && git push origin v1.0.0
```

## Структура репозитория

```
src/xinput_fix.c          исходник прокси (с -DDIAG — исследовательская сборка)
src/xinput1_3.def         экспорты с ординалами как у оригинального xinput1_3
installer/ssfix_install.c установщик (Windows и Linux)
patch/*.ssdf              диффы роликов интерфейса
ui/*.as                   ActionScript 2, вставляемый в ролики
tools/                    инструменты для разбора и сборки ресурсов игры
docs/                     документация
INSTALL.txt               инструкция для архива релиза
Makefile                  сборка
```
