"""Prepare FE_pressStart_Overlay.gfx to show the Xbox A button instead of "ENTER".

The game only renders images from its texture packs, so the A glyph is the
Prompt_Bar.gfx shape that crops the resident circle_buttons atlas. This adds
that image + shape as an exported clip "pad_A_mc", names the "press start"
text field press_txt and adds an empty frame script next to it; the script
body is compiled in afterwards with FFDec.
Usage: pressstart.py <FE_pressStart_Overlay.gfx> <Prompt_Bar.gfx> <out.gfx>
"""
import struct
import sys

import swf

TEXT_SPRITE = 3
TEXT_CHAR = 2
FIELD_NAME = b'press_txt'

ATLAS_IMAGE = 5   # circle_buttons.tga in Prompt_Bar.gfx
GLYPH_A = 45      # shape showing the Xbox A cell of the atlas
IMAGE_ID, SHAPE_ID, CLIP_ID = 110, 111, 112
CLIP_EXPORT = b'pad_A_mc'


def name_placement(t):
    """Give a PlaceObject3 (char + matrix, no name yet) an instance name."""
    d = bytearray(t.data)
    assert t.code == swf.T_PLACEOBJECT3 and d[0] & 0x02 and d[0] & 0x04 and not d[0] & 0x20
    assert not d[0] & 0x18  # no color transform / ratio between matrix and name
    pos = swf._skip_matrix(bytes(d), swf.char_offset(t) + 2)
    d[0] |= 0x20
    d[pos:pos] = FIELD_NAME + b'\0'
    return swf.Tag(t.code, d, t.long_hdr)


def glyph_tags(prompt_bar_tags):
    image = next(t for t in prompt_bar_tags if t.code == swf.GFX_EXTERNALIMAGE and t.char_id == ATLAS_IMAGE)
    shape = next(t for t in prompt_bar_tags if t.code == swf.T_DEFINESHAPE and t.char_id == GLYPH_A)

    image = swf.Tag(image.code, struct.pack('<H', IMAGE_ID) + image.data[2:], image.long_hdr)
    data = bytearray(shape.data)
    struct.pack_into('<H', data, 0, SHAPE_ID)
    fill = data.index(b'\x42' + struct.pack('<H', ATLAS_IMAGE))  # bitmap fill -> atlas
    struct.pack_into('<H', data, fill + 1, IMAGE_ID)
    shape = swf.Tag(shape.code, data, shape.long_hdr)

    clip = swf.Tag(swf.T_DEFINESPRITE, struct.pack('<HH', CLIP_ID, 1), True)
    clip.children = [swf.Tag(swf.T_PLACEOBJECT2, struct.pack('<BHH', 0x02, 1, SHAPE_ID)),
                     swf.Tag(swf.T_SHOWFRAME, b''), swf.Tag(swf.T_END, b'')]
    export = swf.Tag(swf.T_EXPORTASSETS, struct.pack('<HH', 1, CLIP_ID) + CLIP_EXPORT + b'\0')
    return [image, shape, clip, export]


def main():
    header, tags = swf.load(sys.argv[1])
    _, prompt_bar = swf.load(sys.argv[2])
    used = {t.char_id for t in tags if t.code in (2, 36, 37, 39, swf.GFX_EXTERNALIMAGE)}
    assert not used & {IMAGE_ID, SHAPE_ID, CLIP_ID}

    sprite = next(t for t in tags if t.code == swf.T_DEFINESPRITE and t.char_id == TEXT_SPRITE)
    children = []
    for t in sprite.children:
        children.append(t)
        if t.code == swf.T_PLACEOBJECT3 and swf.place_info(t)[1] == TEXT_CHAR:
            children[-1] = name_placement(t)
            children.append(swf.Tag(swf.T_DOACTION, b'\0'))  # placeholder script
    sprite.children = children

    at = next(i for i, t in enumerate(tags) if t.code == swf.T_DOACTION)
    tags[at:at] = glyph_tags(prompt_bar)
    swf.save(sys.argv[3], header, tags)


if __name__ == '__main__':
    main()
