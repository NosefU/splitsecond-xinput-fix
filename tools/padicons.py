"""Make Prompt_Bar.gfx always show Xbox button icons.

Each prompt button (buttonA_mc ... buttonSTART_mc) gets its own icon sprite
in which every frame shows that button's Xbox glyph. All original frame
labels are kept, so whatever key frame the game selects (PC_GenKey,
PC_GenKeyLong, PC_KeyBlank) the gamepad icon is shown immediately.
The glyph sits in a named child clip (glyph_mc) so the frame script compiled
in afterwards (padicon_frame1.as) can undo the exactFit stretch around its centre.
Usage: padicons.py <in.gfx> <out.gfx>
"""
import struct
import sys

import swf

ICON_SPRITE = 57
BUTTON_SPRITE = 59
BAR_SPRITES = (60, 84)
# Xbox glyph shapes placed by the original icon sprite (frames Xenon_Y,
# Xenon_B, Xenon_A, Xenon_X, Xenon_Start).
GLYPHS = {
    'buttonA_mc': 45,
    'buttonB_mc': 44,
    'buttonX_mc': 46,
    'buttonY_mc': 43,
    'buttonSTART_mc': 47,
}
FIRST_NEW_ID = 900


def defined_ids(tags):
    ids = set()
    for t in tags:
        if t.code in (2, 6, 10, 11, 20, 21, 22, 32, 33, 34, 35, 36, 37, 39, 46, 48, 75, 83, 84, 90, 1001, 1009):
            ids.add(t.char_id)
    return ids


def glyph_clip(new_id, glyph):
    """One-frame sprite holding just the glyph shape."""
    clip = swf.Tag(swf.T_DEFINESPRITE, struct.pack('<HH', new_id, 1), True)
    clip.children = [swf.Tag(swf.T_PLACEOBJECT2, struct.pack('<BHH', 0x02, 1, glyph)),
                     swf.Tag(swf.T_SHOWFRAME, b''), swf.Tag(swf.T_END, b'')]
    return clip


def icon_sprite(src, new_id, clip_id):
    """Same frames and labels as src; frame 1 places the glyph clip as glyph_mc."""
    children = []
    for i, frame in enumerate(swf.frames(src)):
        for t in frame:
            if t.code == swf.T_FRAMELABEL or (i == 0 and t.code == swf.T_DOACTION):
                children.append(t)
        if i == 0:
            # PlaceObject2: flags=HasName|HasCharacter, depth 1, character id, name
            children.append(swf.Tag(swf.T_PLACEOBJECT2, struct.pack('<BHH', 0x22, 1, clip_id) + b'glyph_mc\0'))
        children.append(swf.Tag(swf.T_SHOWFRAME, b''))
    children.append(swf.Tag(swf.T_END, b''))
    sprite = swf.Tag(swf.T_DEFINESPRITE, struct.pack('<HH', new_id, len(swf.frames(src))), True)
    sprite.children = children
    return sprite


def retarget(sprite_tag, new_id, old_child, new_child):
    """Copy of sprite_tag with its own id, placing new_child instead of old_child."""
    copy = swf.Tag(swf.T_DEFINESPRITE, struct.pack('<H', new_id) + sprite_tag.data[2:4], True)
    copy.children = [replace_char(t, old_child, new_child) for t in sprite_tag.children]
    return copy


def replace_char(t, old, new):
    if t.code not in (swf.T_PLACEOBJECT2, swf.T_PLACEOBJECT3):
        return t
    off = swf.char_offset(t)
    if off is None or struct.unpack_from('<H', t.data, off)[0] != old:
        return t
    data = bytearray(t.data)
    struct.pack_into('<H', data, off, new)
    return swf.Tag(t.code, data, t.long_hdr)


def main():
    header, tags = swf.load(sys.argv[1])
    sprites = {t.char_id: t for t in tags if t.code == swf.T_DEFINESPRITE}
    used = defined_ids(tags)
    new_ids = iter(i for i in range(FIRST_NEW_ID, 0xffff) if i not in used)

    new_tags, button_for = [], {}
    for name, glyph in GLYPHS.items():
        clip_id, icon_id, button_id = next(new_ids), next(new_ids), next(new_ids)
        new_tags.append(glyph_clip(clip_id, glyph))
        new_tags.append(icon_sprite(sprites[ICON_SPRITE], icon_id, clip_id))
        new_tags.append(retarget(sprites[BUTTON_SPRITE], button_id, ICON_SPRITE, icon_id))
        button_for[name] = button_id
        print(f"{name}: icon sprite {icon_id} (glyph {glyph}), button sprite {button_id}")

    for bar in BAR_SPRITES:
        children = []
        for t in sprites[bar].children:
            if t.code in (swf.T_PLACEOBJECT2, swf.T_PLACEOBJECT3):
                _, char, name, _ = swf.place_info(t)
                if char == BUTTON_SPRITE and name in button_for:
                    t = replace_char(t, BUTTON_SPRITE, button_for[name])
                    print(f"sprite {bar}: {name} -> {button_for[name]}")
            children.append(t)
        sprites[bar].children = children

    at = tags.index(sprites[BUTTON_SPRITE]) + 1
    tags[at:at] = new_tags
    swf.save(sys.argv[2], header, tags)


if __name__ == '__main__':
    main()
