/* Dump every entry of a Split/Second .ark archive to <outdir>/<hash>.bin.
 * Entry table: 20-byte records (flag, offset, csize, usize, hash) at 0x18.
 * flag 1: u32 size prefix + LZSS (4 KiB ring, zero-filled, r = 0xfee). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void lzss(const uint8_t *src, size_t srclen, uint8_t *out, size_t usize)
{
    uint8_t ring[4096] = {0};
    unsigned r = 4096 - 18;
    size_t i = 0, o = 0;
    while (o < usize && i < srclen)
    {
        unsigned flags = src[i++], bit;
        for (bit = 0; bit < 8 && o < usize && i < srclen; bit++)
        {
            if (flags & (1u << bit))
            {
                uint8_t c = src[i++];
                out[o++] = c; ring[r] = c; r = (r + 1) & 0xfff;
            }
            else
            {
                unsigned off, len, k;
                if (i + 1 >= srclen) return;
                off = src[i] | ((src[i + 1] & 0xf0) << 4);
                len = (src[i + 1] & 0x0f) + 3;
                i += 2;
                for (k = 0; k < len && o < usize; k++)
                {
                    uint8_t c = ring[(off + k) & 0xfff];
                    out[o++] = c; ring[r] = c; r = (r + 1) & 0xfff;
                }
            }
        }
    }
}

int main(int argc, char **argv)
{
    FILE *f;
    uint8_t *buf;
    long size;
    uint32_t count, k;

    if (argc != 3) { fprintf(stderr, "usage: %s archive.ark outdir\n", argv[0]); return 1; }
    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
    buf = malloc(size);
    if (fread(buf, 1, size, f) != (size_t)size) { perror("read"); return 1; }
    fclose(f);

    memcpy(&count, buf + 8, 4);
    for (k = 0; k < count; k++)
    {
        uint32_t e[5];
        char path[4096];
        uint8_t *data;
        memcpy(e, buf + 0x18 + 20 * k, 20);
        data = malloc(e[3] ? e[3] : 1);
        if (e[0] == 1) lzss(buf + e[1] + 4, e[2] - 4, data, e[3]);
        else memcpy(data, buf + e[1], e[3]);
        snprintf(path, sizeof(path), "%s/%08x.bin", argv[2], e[4]);
        f = fopen(path, "wb");
        if (!f) { perror(path); return 1; }
        fwrite(data, 1, e[3], f);
        fclose(f);
        free(data);
    }
    printf("%u entries\n", count);
    return 0;
}
