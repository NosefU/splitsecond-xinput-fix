/* Split/Second gamepad fix installer (Windows and Linux / Steam Deck).
 *
 *   ssfix-install [--uninstall] [game folder]
 *
 * Install:
 *   - checks SplitSecond.exe is a supported build (retail or Steam, any language);
 *   - patches two UI movies inside data/resident.ark: each original entry is
 *     unpacked, checked against the CRC the diff was made for, the SSDF diff is
 *     applied and the result is appended to the archive; only the entry's
 *     20-byte table record changes. The original records and archive size are
 *     saved first to data/resident.ark.ssfix, so uninstall restores the archive
 *     byte for byte;
 *   - writes xinput1_3.dll next to the exe (an existing different one is kept
 *     as xinput1_3.dll.bak) and, on the first installation (not on an update),
 *     the ssfix_reset_controls marker, which makes the DLL reset the controls
 *     to the layout the fix expects;
 *   - copies itself to ssfix/ in the game folder and writes an uninstall
 *     script next to the exe ("Uninstall gamepad fix.bat" on Windows,
 *     uninstall-gamepad-fix.sh on Linux).
 * Uninstall reverses all of that. Without a folder argument the current
 * folder is used, or the installer's own folder if the game is not there;
 * if it is in neither, the copies of the game found on this computer are
 * listed to choose from. */
#define _DEFAULT_SOURCE  /* realpath, strncasecmp */
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#define PATH_SEP '\\'
#define SELF_NAME        "ssfix-install.exe"
#define UNINSTALL_SCRIPT "Uninstall gamepad fix.bat"
static int truncate_file(FILE *f, int64_t size) { return _chsize_s(_fileno(f), size) ? -1 : 0; }
static int seek64(FILE *f, int64_t off, int whence) { return _fseeki64(f, off, whence); }
static int64_t tell64(FILE *f) { return _ftelli64(f); }
static int make_dir(const char *path) { return _mkdir(path); }
static int remove_dir(const char *path) { return _rmdir(path); }
static int self_path(char *buf, size_t size) { DWORD n = GetModuleFileNameA(NULL, buf, (DWORD)size); return n > 0 && n < size ? 0 : -1; }
static int same_file(const char *a, const char *b) { return !_stricmp(a, b); }
#else
#include <sys/stat.h>
#include <unistd.h>
#define PATH_SEP '/'
#define SELF_NAME        "ssfix-install"
#define UNINSTALL_SCRIPT "uninstall-gamepad-fix.sh"
static int truncate_file(FILE *f, int64_t size) { return ftruncate(fileno(f), (off_t)size); }
static int seek64(FILE *f, int64_t off, int whence) { return fseeko(f, (off_t)off, whence); }
static int64_t tell64(FILE *f) { return (int64_t)ftello(f); }
static int make_dir(const char *path) { return mkdir(path, 0755); }
static int remove_dir(const char *path) { return rmdir(path); }
static int self_path(char *buf, size_t size)
{
    ssize_t n = readlink("/proc/self/exe", buf, size - 1);
    if (n <= 0) return -1;
    buf[n] = 0;
    return 0;
}
static int same_file(const char *a, const char *b)
{
    struct stat sa, sb;
    return !stat(a, &sa) && !stat(b, &sb) && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}
#endif

#include "payload.h"  /* generated: payload_dll, payload_diffs */

/* Supported SplitSecond.exe builds: retail (disc) and Steam. The Steam
 * English, German, French, Italian and Spanish versions share one exe. */
static const struct { uint32_t timestamp, image_size; const char *name; } exe_builds[] = {
    { 0x4be3ea74u, 0x1a64000u, "retail" },
    { 0x4be13c66u, 0x1db7000u, "Steam (English/German/French/Italian/Spanish)" },
    { 0x4be3ea74u, 0x1dbf000u, "Steam (Russian)" },
    { 0x4be13cc6u, 0x1dbc000u, "Steam (Polish)" },
};
#define ARK_VERSION     0x10000u
#define ARK_TABLE       0x18
#define ARK_RECORD      20
#define ARK_ALIGN       0x800
#define SIDECAR_MAGIC   "SSFX"
#define MAX_PATCHES     8

static char game_dir[4096];
static int from_script;  /* --from-script: given by uninstall scripts; kept for older ones */

/* On Windows the installer usually runs in its own console window (it asks for
 * admin rights, so even the uninstall script starts it in a new one): keep it
 * open until the output is read. */
static void pause_on_windows(void)
{
#ifdef _WIN32
    printf("\nPress Enter to close.");
    fflush(stdout);
    getchar();
#endif
}

static __attribute__((noreturn)) void fail(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "\nERROR: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    pause_on_windows();
    exit(1);
}

static void path_join(char *out, size_t size, const char *rel)
{
    snprintf(out, size, "%s%c%s", game_dir, PATH_SEP, rel);
    for (char *p = out + strlen(game_dir); *p; p++)
        if (*p == '/' || *p == '\\') *p = PATH_SEP;
}

static int file_exists(const char *rel)
{
    char path[4200];
    FILE *f;
    path_join(path, sizeof(path), rel);
    if (!(f = fopen(path, "rb"))) return 0;
    fclose(f);
    return 1;
}

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

static uint32_t crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xffffffffu;
    while (len--)
    {
        crc ^= *data++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xedb88320u & -(crc & 1));
    }
    return ~crc;
}

/* .ark LZSS: 4 KiB ring buffer, zero-filled, write position 0xfee. */
static int lzss(const uint8_t *src, size_t srclen, uint8_t *out, size_t usize)
{
    uint8_t ring[4096] = {0};
    unsigned r = 4096 - 18;
    size_t i = 0, o = 0;
    while (o < usize && i < srclen)
    {
        unsigned flags = src[i++];
        for (int bit = 0; bit < 8 && o < usize && i < srclen; bit++)
        {
            if (flags & (1u << bit))
            {
                out[o++] = ring[r] = src[i++];
                r = (r + 1) & 0xfff;
            }
            else
            {
                if (i + 1 >= srclen) return -1;
                unsigned off = src[i] | ((src[i + 1] & 0xf0) << 4), len = (src[i + 1] & 0x0f) + 3;
                i += 2;
                for (unsigned k = 0; k < len && o < usize; k++)
                {
                    out[o++] = ring[r] = ring[(off + k) & 0xfff];
                    r = (r + 1) & 0xfff;
                }
            }
        }
    }
    return o == usize ? 0 : -1;
}

static void read_at(FILE *f, int64_t off, void *buf, size_t len, const char *what)
{
    if (seek64(f, off, SEEK_SET) || fread(buf, 1, len, f) != len) fail("cannot read %s", what);
}

static void write_at(FILE *f, int64_t off, const void *buf, size_t len, const char *what)
{
    if (seek64(f, off, SEEK_SET) || fwrite(buf, 1, len, f) != len) fail("cannot write %s: %s", what, strerror(errno));
}

/* ---- SSDF diffs ---- */

struct diff { uint32_t hash, src_size, src_crc, dst_size, dst_crc; const uint8_t *ops; size_t ops_len; };

static struct diff parse_diff(const uint8_t *data, size_t len)
{
    struct diff d;
    if (len < 28 || memcmp(data, "SSDF", 4) || rd32(data + 4) != 1) fail("corrupt patch data");
    d.hash = rd32(data + 8);
    d.src_size = rd32(data + 12); d.src_crc = rd32(data + 16);
    d.dst_size = rd32(data + 20); d.dst_crc = rd32(data + 24);
    d.ops = data + 28; d.ops_len = len - 28;
    return d;
}

static uint8_t *apply_diff(const struct diff *d, const uint8_t *src)
{
    uint8_t *out = malloc(d->dst_size);
    size_t pos = 0, o = 0;
    if (!out) fail("out of memory");
    while (pos < d->ops_len)
    {
        uint8_t op = d->ops[pos];
        if (op == 1 && pos + 9 <= d->ops_len)
        {
            uint32_t off = rd32(d->ops + pos + 1), n = rd32(d->ops + pos + 5);
            if (off + (uint64_t)n > d->src_size || o + n > d->dst_size) fail("corrupt patch data");
            memcpy(out + o, src + off, n); o += n; pos += 9;
        }
        else if (op == 2 && pos + 5 <= d->ops_len)
        {
            uint32_t n = rd32(d->ops + pos + 1);
            if (pos + 5 + (uint64_t)n > d->ops_len || o + n > d->dst_size) fail("corrupt patch data");
            memcpy(out + o, d->ops + pos + 5, n); o += n; pos += 5 + n;
        }
        else fail("corrupt patch data");
    }
    if (o != d->dst_size || crc32(out, o) != d->dst_crc) fail("patch result check failed");
    return out;
}

/* ---- archive ---- */

struct sidecar { uint64_t ark_size; uint32_t count; uint32_t index[MAX_PATCHES]; uint8_t record[MAX_PATCHES][ARK_RECORD]; };

static int load_sidecar(struct sidecar *sc)
{
    char path[4200];
    uint8_t head[16];
    FILE *f;
    path_join(path, sizeof(path), "data/resident.ark.ssfix");
    if (!(f = fopen(path, "rb"))) return 0;
    if (fread(head, 1, 16, f) != 16 || memcmp(head, SIDECAR_MAGIC, 4)) fail("%s is corrupt", path);
    sc->ark_size = rd32(head + 4) | (uint64_t)rd32(head + 8) << 32;
    sc->count = rd32(head + 12);
    if (sc->count > MAX_PATCHES) fail("%s is corrupt", path);
    for (uint32_t i = 0; i < sc->count; i++)
    {
        uint8_t idx[4];
        if (fread(idx, 1, 4, f) != 4 || fread(sc->record[i], 1, ARK_RECORD, f) != ARK_RECORD) fail("%s is corrupt", path);
        sc->index[i] = rd32(idx);
    }
    fclose(f);
    return 1;
}

static void save_sidecar(const struct sidecar *sc)
{
    char path[4200];
    uint8_t head[16];
    FILE *f;
    path_join(path, sizeof(path), "data/resident.ark.ssfix");
    if (!(f = fopen(path, "wb"))) fail("cannot create %s: %s", path, strerror(errno));
    memcpy(head, SIDECAR_MAGIC, 4);
    wr32(head + 4, (uint32_t)sc->ark_size); wr32(head + 8, (uint32_t)(sc->ark_size >> 32)); wr32(head + 12, sc->count);
    fwrite(head, 1, 16, f);
    for (uint32_t i = 0; i < sc->count; i++)
    {
        uint8_t idx[4];
        wr32(idx, sc->index[i]);
        fwrite(idx, 1, 4, f);
        fwrite(sc->record[i], 1, ARK_RECORD, f);
    }
    if (fclose(f)) fail("cannot write %s", path);
}

static FILE *open_archive(uint32_t *count)
{
    char path[4200];
    uint8_t head[ARK_TABLE];
    FILE *f;
    path_join(path, sizeof(path), "data/resident.ark");
    if (!(f = fopen(path, "r+b"))) fail("cannot open %s: %s", path, strerror(errno));
    read_at(f, 0, head, sizeof(head), "archive header");
    if (rd32(head) != ARK_VERSION) fail("data/resident.ark has an unknown format");
    *count = rd32(head + 8);
    return f;
}

/* The archive is still the one the sidecar was made for: it is longer than the
 * saved size, and each patched record keeps its entry hash and points at data
 * appended past the saved size. */
static int archive_is_patched(FILE *f, uint32_t count, const struct sidecar *sc)
{
    int64_t size;
    if (seek64(f, 0, SEEK_END) || (size = tell64(f)) <= (int64_t)sc->ark_size) return 0;
    for (uint32_t i = 0; i < sc->count; i++)
    {
        uint8_t rec[ARK_RECORD];
        if (sc->index[i] >= count) return 0;
        read_at(f, ARK_TABLE + (int64_t)sc->index[i] * ARK_RECORD, rec, ARK_RECORD, "archive table");
        if (rd32(rec + 16) != rd32(sc->record[i] + 16) || rd32(rec) != 0
            || rd32(rec + 4) < sc->ark_size || rd32(rec + 4) + (int64_t)rd32(rec + 8) > size)
            return 0;
    }
    return 1;
}

static int restore_archive(void)
{
    struct sidecar sc;
    uint32_t count;
    FILE *f;
    char path[4200];
    if (!load_sidecar(&sc)) return 0;
    f = open_archive(&count);
    if (!archive_is_patched(f, count, &sc))
    {
        /* The archive was replaced since (a Steam update, a language change,
         * "verify integrity"): the saved records belong to another file. */
        fclose(f);
        path_join(path, sizeof(path), "data/resident.ark.ssfix");
        remove(path);
        printf("  data/resident.ark was replaced since the fix was installed; nothing to restore\n");
        return 0;
    }
    for (uint32_t i = 0; i < sc.count; i++)
        write_at(f, ARK_TABLE + (int64_t)sc.index[i] * ARK_RECORD, sc.record[i], ARK_RECORD, "archive table");
    fflush(f);
    if (truncate_file(f, (int64_t)sc.ark_size)) fail("cannot truncate data/resident.ark");
    fclose(f);
    path_join(path, sizeof(path), "data/resident.ark.ssfix");
    remove(path);
    printf("  restored data/resident.ark (%u entries)\n", sc.count);
    return 1;
}

static void patch_archive(void)
{
    struct sidecar sc = {0};
    uint32_t count, n = 0;
    uint8_t *results[MAX_PATCHES];
    struct diff diffs[MAX_PATCHES];
    FILE *f;

    f = open_archive(&count);
    seek64(f, 0, SEEK_END);
    sc.ark_size = (uint64_t)tell64(f);

    for (size_t p = 0; p < payload_diff_count; p++)
    {
        struct diff d = parse_diff(payload_diffs[p].data, payload_diffs[p].size);
        uint8_t rec[ARK_RECORD], *raw, *src;
        uint32_t k;
        for (k = 0; k < count; k++)
        {
            read_at(f, ARK_TABLE + (int64_t)k * ARK_RECORD, rec, ARK_RECORD, "archive table");
            if (rd32(rec + 16) == d.hash) break;
        }
        if (k == count) fail("entry %08x not found in data/resident.ark", d.hash);

        uint32_t flag = rd32(rec), off = rd32(rec + 4), csize = rd32(rec + 8), usize = rd32(rec + 12);
        if (usize != d.src_size)
            fail("entry %08x differs from the original (already modified, or another game version)", d.hash);
        if (!(raw = malloc(csize)) || !(src = malloc(usize))) fail("out of memory");
        read_at(f, off, raw, csize, "archive entry");
        if (flag == 1)
        {
            if (csize < 4 || rd32(raw) != usize || lzss(raw + 4, csize - 4, src, usize)) fail("cannot unpack entry %08x", d.hash);
        }
        else memcpy(src, raw, usize);
        if (crc32(src, usize) != d.src_crc)
            fail("entry %08x differs from the original (already modified, or another game version)", d.hash);

        results[n] = apply_diff(&d, src);
        diffs[n] = d;
        sc.index[n] = k;
        memcpy(sc.record[n], rec, ARK_RECORD);
        n++;
        free(raw); free(src);
    }
    sc.count = n;
    save_sidecar(&sc);  /* before touching the archive, so it can always be restored */

    int64_t pos = (int64_t)sc.ark_size;
    for (uint32_t i = 0; i < n; i++)
    {
        static const uint8_t zero[ARK_ALIGN];
        uint8_t rec[ARK_RECORD];
        pos += (ARK_ALIGN - pos % ARK_ALIGN) % ARK_ALIGN;
        write_at(f, pos, results[i], diffs[i].dst_size, "archive data");
        int64_t end = pos + diffs[i].dst_size;
        int64_t pad = (ARK_ALIGN - end % ARK_ALIGN) % ARK_ALIGN;
        if (pad) write_at(f, end, zero, (size_t)pad, "archive data");
        if (pos > 0xffffffffLL) fail("data/resident.ark is too large");
        wr32(rec, 0); wr32(rec + 4, (uint32_t)pos); wr32(rec + 8, diffs[i].dst_size);
        wr32(rec + 12, diffs[i].dst_size); wr32(rec + 16, diffs[i].hash);
        write_at(f, ARK_TABLE + (int64_t)sc.index[i] * ARK_RECORD, rec, ARK_RECORD, "archive table");
        printf("  patched entry %08x (%u bytes)\n", diffs[i].hash, diffs[i].dst_size);
        pos = end + pad;
        free(results[i]);
    }
    if (fclose(f)) fail("cannot write data/resident.ark");
}

/* ---- files next to the exe ---- */

/* Any version of the fix's DLL carries this marker (see src/xinput_fix.c). */
static int is_our_dll(const char *path)
{
    static const char marker[] = "SSFIX-XINPUT-PROXY";
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    long len;
    int ours = 0;
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len > 0 && len < (16 << 20) && (buf = malloc(len)))
    {
        if (fread(buf, 1, len, f) == (size_t)len)
            for (long i = 0; i + (long)sizeof(marker) - 1 <= len && !ours; i++)
                ours = !memcmp(buf + i, marker, sizeof(marker) - 1);
        free(buf);
    }
    fclose(f);
    return ours;
}

static void install_dll(void)
{
    char path[4200], bak[4300];
    FILE *f;
    path_join(path, sizeof(path), "xinput1_3.dll");
    snprintf(bak, sizeof(bak), "%s.bak", path);
    int update = file_exists("xinput1_3.dll") && is_our_dll(path);
    if (file_exists("xinput1_3.dll") && !update && !file_exists("xinput1_3.dll.bak"))
    {
        if (rename(path, bak)) fail("cannot rename %s: %s", path, strerror(errno));
        printf("  kept the previous xinput1_3.dll as xinput1_3.dll.bak\n");
    }
    if (!(f = fopen(path, "wb")) || fwrite(payload_dll, 1, payload_dll_size, f) != payload_dll_size || fclose(f))
        fail("cannot write %s: %s", path, strerror(errno));
    printf("  installed xinput1_3.dll\n");
    if (update)
    {
        printf("  updated over a previous version: the controls are kept\n");
        return;
    }
    path_join(path, sizeof(path), "ssfix_reset_controls");
    if (!(f = fopen(path, "wb")) || fclose(f)) fail("cannot write %s: %s", path, strerror(errno));
    printf("  controls will be reset to the gamepad layout on the next start\n");
}

static void remove_dll(void)
{
    char path[4200], bak[4300];
    path_join(path, sizeof(path), "xinput1_3.dll");
    snprintf(bak, sizeof(bak), "%s.bak", path);
    if (is_our_dll(path))
    {
        remove(path);
        printf("  removed xinput1_3.dll\n");
        if (file_exists("xinput1_3.dll.bak") && !rename(bak, path)) printf("  restored the previous xinput1_3.dll\n");
    }
    path_join(path, sizeof(path), "ssfix_reset_controls");
    remove(path);
}

/* ---- uninstaller: a copy of this program in ssfix/ plus a script next to the exe ---- */

static const char uninstall_script[] =
#ifdef _WIN32
    /* "start" goes through ShellExecute, so Windows asks for admin rights
     * (cmd itself cannot start a program that requires them). The installer
     * runs in its own window and deletes this script. */
    "@start \"\" \"%~dp0ssfix\\" SELF_NAME "\" --uninstall --from-script \"%~dp0.\"\r\n";
#else
    "#!/bin/sh\n"
    "# Removes the Split/Second gamepad fix from this folder.\n"
    "cd \"$(dirname \"$0\")\" || exit 1\n"
    "exec ./ssfix/" SELF_NAME " --uninstall \"$PWD\"\n";
#endif

static void install_uninstaller(void)
{
    char self[4200], dir[4200], copy[4200], script[4200];
    uint8_t *buf;
    long len;
    FILE *f;

    path_join(dir, sizeof(dir), "ssfix");
    path_join(copy, sizeof(copy), "ssfix/" SELF_NAME);
    path_join(script, sizeof(script), UNINSTALL_SCRIPT);
    if (self_path(self, sizeof(self))) fail("cannot find the installer's own path");
    make_dir(dir);
    if (!same_file(self, copy))
    {
        if (!(f = fopen(self, "rb"))) fail("cannot read %s: %s", self, strerror(errno));
        fseek(f, 0, SEEK_END);
        len = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (len <= 0 || !(buf = malloc(len)) || fread(buf, 1, len, f) != (size_t)len) fail("cannot read %s", self);
        fclose(f);
        if (!(f = fopen(copy, "wb")) || fwrite(buf, 1, len, f) != (size_t)len || fclose(f))
            fail("cannot write %s: %s", copy, strerror(errno));
        free(buf);
#ifndef _WIN32
        chmod(copy, 0755);
#endif
    }
    if (!(f = fopen(script, "wb")) || fputs(uninstall_script, f) < 0 || fclose(f))
        fail("cannot write %s: %s", script, strerror(errno));
#ifndef _WIN32
    chmod(script, 0755);
#endif
    printf("  to uninstall, run \"%s\" in the game folder\n", UNINSTALL_SCRIPT);
}

static void remove_uninstaller(void)
{
    char dir[4200], copy[4200], script[4200];
    path_join(dir, sizeof(dir), "ssfix");
    path_join(copy, sizeof(copy), "ssfix/" SELF_NAME);
    path_join(script, sizeof(script), UNINSTALL_SCRIPT);
    remove(script);
    if (remove(copy) && file_exists("ssfix/" SELF_NAME))
    {
#ifdef _WIN32
        /* this is the running copy (started by the uninstall script): remove it on the next reboot */
        MoveFileExA(copy, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
        MoveFileExA(dir, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
        printf("  ssfix\\%s will be removed on the next reboot\n", SELF_NAME);
#endif
        return;
    }
    remove_dir(dir);
}

/* ---- Steam libraries ----
 * The Steam folder (Linux: native and Flatpak; Windows: from the registry)
 * and the library folders listed in its steamapps/libraryfolders.vdf. */
#include <limits.h>
#include <strings.h>
#define PATH_LEN 4096  /* PATH_MAX of Linux; MinGW's is only 260 */
#ifdef _WIN32
/* Full path of an existing file or folder. */
static int real_path(const char *path, char *out)
{
    DWORD n = GetFullPathNameA(path, PATH_LEN, out, NULL);
    return n > 0 && n < PATH_LEN && GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES;
}
static int same_path(const char *a, const char *b) { return !_stricmp(a, b); }

static int reg_string(HKEY root, const char *key, const char *name, char *out, DWORD size)
{
    HKEY h;
    DWORD type, n = size - 1;
    LONG r;
    if (RegOpenKeyExA(root, key, 0, KEY_READ, &h)) return 0;
    r = RegQueryValueExA(h, name, NULL, &type, (BYTE *)out, &n);
    RegCloseKey(h);
    if (r || type != REG_SZ) return 0;
    out[n] = 0;
    return 1;
}
#else
static int real_path(const char *path, char *out) { return realpath(path, out) != NULL; }
static int same_path(const char *a, const char *b) { return !strcmp(a, b); }
#endif

static char steam_libs[16][PATH_LEN];
static int steam_lib_count;

static void add_lib(const char *path)
{
    char real[PATH_LEN];
    if (!real_path(path, real) || steam_lib_count == 16) return;
    for (int i = 0; i < steam_lib_count; i++)
        if (same_path(steam_libs[i], real)) return;
    snprintf(steam_libs[steam_lib_count++], PATH_LEN, "%s", real);
}

static char *read_text(const char *path, long *len_out)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long len;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0 || !(buf = malloc(len + 1)) || fread(buf, 1, len, f) != (size_t)len)
    {
        fclose(f);
        return NULL;
    }
    fclose(f);
    buf[len] = 0;
    if (len_out) *len_out = len;
    return buf;
}

static void add_steam_root(const char *root)
{
    char path[PATH_LEN + 64], *text, *p;
    add_lib(root);
    snprintf(path, sizeof(path), "%s/steamapps/libraryfolders.vdf", root);
    if (!(text = read_text(path, NULL))) return;
    for (p = text; (p = strstr(p, "\"path\"")); )
    {
        char *a = strchr(p + 6, '"'), *s, *d;
        if (!a) break;
        /* VDF strings escape backslashes: "C:\\Games\\Steam" */
        for (s = d = ++a; *s && *s != '"'; s++, d++)
        {
            if (*s == '\\' && s[1]) s++;
            *d = *s;
        }
        if (!*s) break;
        *d = 0;
        add_lib(a);
        p = s + 1;
    }
    free(text);
}

static void find_steam_libs(void)
{
    char root[PATH_LEN];
    if (steam_lib_count) return;
#ifdef _WIN32
    if (reg_string(HKEY_LOCAL_MACHINE, "SOFTWARE\\Valve\\Steam", "InstallPath", root, sizeof(root))) add_steam_root(root);
    if (reg_string(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath", root, sizeof(root))) add_steam_root(root);
#else
    static const char *const roots[] = { ".local/share/Steam", ".steam/steam", ".var/app/com.valvesoftware.Steam/.local/share/Steam" };
    const char *home = getenv("HOME");
    if (!home) return;
    for (size_t r = 0; r < sizeof(roots) / sizeof(*roots); r++)
    {
        snprintf(root, sizeof(root), "%s/%s", home, roots[r]);
        add_steam_root(root);
    }
#endif
}

#ifndef _WIN32
/* ---- Proton: DLL override in the game's prefix ----
 * Proton only loads xinput1_3.dll from the game folder with a DLL override.
 * Instead of asking for a launch option, the installer adds
 *   [Software\\Wine\\AppDefaults\\SplitSecond.exe\\DllOverrides] "xinput1_3"="native,builtin"
 * to user.reg of the game's prefix: compatdata/<appid> of the Steam version
 * (appmanifest) or of a non-Steam shortcut to the game (shortcuts.vdf). The
 * prefixes changed are listed in ssfix/proton-prefixes.txt for uninstall. */
#include <dirent.h>
#include <time.h>

#define REG_KEY   "[Software\\\\Wine\\\\AppDefaults\\\\SplitSecond.exe\\\\DllOverrides]"
#define REG_VALUE "\"xinput1_3\"=\"native,builtin\""
#define PREFIX_LIST "ssfix/proton-prefixes.txt"

static int user_reg_of(const char *appid, char *out, size_t size)
{
    for (int i = 0; i < steam_lib_count; i++)
    {
        snprintf(out, size, "%s/steamapps/compatdata/%s/pfx/user.reg", steam_libs[i], appid);
        if (!access(out, R_OK | W_OK)) return 1;
    }
    return 0;
}

/* Steam version: the library whose steamapps/common contains the game. */
static int steam_appid(const char *game, char *appid, size_t size)
{
    for (int i = 0; i < steam_lib_count; i++)
    {
        char common[PATH_LEN + 32], dir[PATH_LEN + 32];
        size_t n = (size_t)snprintf(common, sizeof(common), "%s/steamapps/common/", steam_libs[i]);
        DIR *d;
        struct dirent *e;
        if (strncmp(game, common, n) || strchr(game + n, '/')) continue;
        snprintf(dir, sizeof(dir), "%s/steamapps", steam_libs[i]);
        if (!(d = opendir(dir))) continue;
        while ((e = readdir(d)))
        {
            char path[PATH_LEN * 2], want[PATH_LEN + 16], *text;
            if (strncmp(e->d_name, "appmanifest_", 12) || !strstr(e->d_name, ".acf")) continue;
            snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
            snprintf(want, sizeof(want), "\"%s\"", game + n);
            if (!(text = read_text(path, NULL))) continue;
            char *p = strstr(text, "\"installdir\"");
            int match = p && (p = strchr(p + 12, '"')) && !strncasecmp(p, want, strlen(want));
            free(text);
            if (match)
            {
                snprintf(appid, size, "%.*s", (int)(strstr(e->d_name, ".acf") - e->d_name - 12), e->d_name + 12);
                closedir(d);
                return 1;
            }
        }
        closedir(d);
    }
    return 0;
}

/* Non-Steam shortcuts: binary userdata/<account>/config/shortcuts.vdf, a map
 * "shortcuts" of maps "0", "1", ... with fields such as appid (int32),
 * AppName, Exe and StartDir (strings). Field types: 0 map, 1 string,
 * 2 int32, 7 int64, 8 end of map. */
struct shortcut { uint32_t appid; char name[256], exe[PATH_LEN], start_dir[PATH_LEN]; };

/* Skips the rest of a map; returns the byte after its end marker, or NULL. */
static const uint8_t *vdf_skip_map(const uint8_t *p, const uint8_t *end)
{
    int depth = 1;
    while (p < end && depth)
    {
        uint8_t type = *p++;
        if (type == 8) { depth--; continue; }
        if (!(p = memchr(p, 0, end - p))) return NULL;
        p++;
        if (type == 0) depth++;
        else if (type == 1) { if (!(p = memchr(p, 0, end - p))) return NULL; p++; }
        else if (type == 2) p += 4;
        else if (type == 7) p += 8;
        else return NULL;
    }
    return depth ? NULL : p;
}

/* "\"/path/to/file\"" or "/path/to/file" -> /path/to/file */
static void unquote(const char *in, char *out, size_t size)
{
    char *q;
    snprintf(out, size, "%s", in[0] == '"' ? in + 1 : in);
    if ((q = strchr(out, '"'))) *q = 0;
}

/* Calls cb for every shortcut of every Steam account; stops when cb returns non-zero. */
static int for_each_shortcut(int (*cb)(const struct shortcut *, void *), void *ctx)
{
    for (int i = 0; i < steam_lib_count; i++)
    {
        char dir[PATH_LEN + 16];
        DIR *d;
        struct dirent *e;
        snprintf(dir, sizeof(dir), "%s/userdata", steam_libs[i]);
        if (!(d = opendir(dir))) continue;
        while ((e = readdir(d)))
        {
            char path[PATH_LEN * 2];
            uint8_t *data;
            long len;
            int stop = 0;
            if (e->d_name[0] == '.') continue;
            snprintf(path, sizeof(path), "%s/%s/config/shortcuts.vdf", dir, e->d_name);
            if (!(data = (uint8_t *)read_text(path, &len))) continue;
            const uint8_t *p = data, *end = data + len;
            if (len > 11 && p[0] == 0 && !strcasecmp((const char *)p + 1, "shortcuts"))
                for (p += 11; !stop && p && p < end && *p == 0; )
                {
                    struct shortcut sc = {0};
                    if (!(p = memchr(p + 1, 0, end - p - 1))) break;
                    for (p++; p && p < end && *p != 8; )
                    {
                        uint8_t type = *p++;
                        const char *name = (const char *)p;
                        if (!(p = memchr(p, 0, end - p))) break;
                        p++;
                        if (type == 0) p = vdf_skip_map(p, end);
                        else if (type == 1)
                        {
                            const char *val = (const char *)p;
                            if (!(p = memchr(p, 0, end - p))) break;
                            p++;
                            if (!strcasecmp(name, "AppName")) snprintf(sc.name, sizeof(sc.name), "%s", val);
                            else if (!strcasecmp(name, "Exe")) unquote(val, sc.exe, sizeof(sc.exe));
                            else if (!strcasecmp(name, "StartDir")) unquote(val, sc.start_dir, sizeof(sc.start_dir));
                        }
                        else if (type == 2 && p + 4 <= end)
                        {
                            if (!strcasecmp(name, "appid")) sc.appid = rd32(p);
                            p += 4;
                        }
                        else if (type == 7) p += 8;
                        else p = NULL;
                    }
                    if (!p || p >= end) break;
                    p++;  /* end of this shortcut */
                    stop = cb(&sc, ctx);
                }
            free(data);
            if (stop)
            {
                closedir(d);
                return 1;
            }
        }
        closedir(d);
    }
    return 0;
}

/* Directory part of a path, resolved; FALSE if it does not exist. */
static int real_dir_of(const char *file, char *out)
{
    char dir[PATH_LEN], *slash;
    snprintf(dir, sizeof(dir), "%s", file);
    if (!(slash = strrchr(dir, '/'))) return 0;
    *slash = 0;
    return realpath(dir[0] ? dir : "/", out) != NULL;
}

struct appid_search { const char *game; uint32_t appid; };

/* A shortcut belongs to the game if its Exe is in the game folder or its StartDir is the game folder. */
static int match_shortcut(const struct shortcut *sc, void *ctx)
{
    struct appid_search *s = ctx;
    char real[PATH_LEN];
    if (!sc->appid) return 0;
    if ((sc->exe[0] && real_dir_of(sc->exe, real) && !strcmp(real, s->game))
        || (sc->start_dir[0] && realpath(sc->start_dir, real) && !strcmp(real, s->game)))
    {
        s->appid = sc->appid;
        return 1;
    }
    return 0;
}

static int shortcut_appid(const char *game, char *appid, size_t size)
{
    struct appid_search s = { game, 0 };
    if (!for_each_shortcut(match_shortcut, &s)) return 0;
    snprintf(appid, size, "%u", s.appid);
    return 1;
}

static int wine_running(void)
{
    DIR *d = opendir("/proc");
    struct dirent *e;
    int running = 0;
    if (!d) return 0;
    while (!running && (e = readdir(d)))
    {
        char path[300], comm[64] = "";
        FILE *f;
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        snprintf(path, sizeof(path), "/proc/%s/comm", e->d_name);
        if (!(f = fopen(path, "r"))) continue;
        if (fgets(comm, sizeof(comm), f)) running = !strncmp(comm, "wineserver", 10);
        fclose(f);
    }
    closedir(d);
    return running;
}

static int write_text(const char *path, const char *text, size_t len)
{
    char tmp[PATH_LEN + 16];
    struct stat st;
    FILE *f;
    snprintf(tmp, sizeof(tmp), "%s.ssfix-tmp", path);
    if (!(f = fopen(tmp, "wb")) || fwrite(text, 1, len, f) != len || fclose(f)) return -1;
    if (!stat(path, &st)) chmod(tmp, st.st_mode & 0777);
    return rename(tmp, path);
}

/* Finds the override key in user.reg; sets *body to the line after the header
 * and *end to the start of the next key (or the end of the text). */
static char *find_key(char *text, char **body, char **end)
{
    for (char *line = text; line && *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : NULL)
        if (!strncasecmp(line, REG_KEY, strlen(REG_KEY)))
        {
            char *b = strchr(line, '\n'), *e;
            b = b ? b + 1 : line + strlen(line);
            for (e = b; *e && *e != '['; e = strchr(e, '\n') ? strchr(e, '\n') + 1 : e + strlen(e)) ;
            *body = b;
            *end = e;
            return line;
        }
    return NULL;
}

static char *find_value(char *body, char *end)
{
    for (char *line = body; line < end; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : end)
        if (!strncasecmp(line, "\"xinput1_3\"=", 12)) return line;
    return NULL;
}

/* Returns 1 if the override was added, 0 if it was already there, -1 on failure. */
static int reg_add_override(const char *reg)
{
    long len;
    char *text = read_text(reg, &len), *key, *body, *end, *val, *out;
    size_t n;
    if (!text) return -1;
    if ((key = find_key(text, &body, &end)) && (val = find_value(body, end)))
    {
        int ok = !strncasecmp(val, REG_VALUE, strlen(REG_VALUE)) || !strncasecmp(val, "\"xinput1_3\"=\"native\"", 20);
        free(text);
        return ok ? 0 : -1;
    }
    if (!(out = malloc(len + 256))) return -1;
    if (key)
    {
        n = (size_t)(body - text);
        memcpy(out, text, n);
        n += (size_t)sprintf(out + n, "%s\n", REG_VALUE);
        memcpy(out + n, body, len - (body - text));
        n += len - (body - text);
    }
    else
    {
        time_t now = time(NULL);
        memcpy(out, text, len);
        n = len;
        if (n && out[n - 1] != '\n') out[n++] = '\n';
        n += (size_t)sprintf(out + n, "\n%s %lld\n#time=%llx\n%s\n", REG_KEY, (long long)now,
                             (unsigned long long)((long long)now * 10000000LL + 116444736000000000LL), REG_VALUE);
    }
    int rc = write_text(reg, out, n) ? -1 : 1;
    free(out);
    free(text);
    return rc;
}

static void reg_remove_override(const char *reg)
{
    long len;
    char *text = read_text(reg, &len), *key, *body, *end, *val;
    if (!text) return;
    if ((key = find_key(text, &body, &end)) && (val = find_value(body, end))
        && !strncasecmp(val, REG_VALUE, strlen(REG_VALUE)))
    {
        char *next = strchr(val, '\n');
        next = next ? next + 1 : val + strlen(val);
        memmove(val, next, strlen(next) + 1);
        end -= next - val;
        /* drop the key if no values are left in it */
        int empty = 1;
        for (char *line = body; line < end; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : end)
            if (*line == '"' || *line == '@') empty = 0;
        if (empty)
        {
            char *from = key;
            if (from > text + 1 && from[-1] == '\n' && from[-2] == '\n') from--;
            memmove(from, end, strlen(end) + 1);
        }
        if (!write_text(reg, text, strlen(text))) printf("  removed the DLL override from %s\n", reg);
    }
    free(text);
}

static void proton_install(void)
{
    char game[PATH_LEN], appid[32], reg[PATH_LEN * 2], list[4200];
    const char *kind;
    FILE *f;
    if (!realpath(game_dir, game)) return;
    find_steam_libs();
    if (steam_appid(game, appid, sizeof(appid))) kind = "Steam";
    else if (shortcut_appid(game, appid, sizeof(appid))) kind = "non-Steam shortcut";
    else
    {
        printf("\nNo Steam entry for this folder found. If you run the game through Proton, add this\n"
               "to its launch options in Steam:\n  WINEDLLOVERRIDES=\"xinput1_3=n,b\" %%command%%\n");
        return;
    }
    if (!user_reg_of(appid, reg, sizeof(reg)))
    {
        printf("\nThe game's Proton prefix (%s, %s) does not exist yet. Start the game once, close it\n"
               "and run the installer again, or add this to its launch options in Steam:\n"
               "  WINEDLLOVERRIDES=\"xinput1_3=n,b\" %%command%%\n", kind, appid);
        return;
    }
    if (wine_running())
    {
        printf("\nA Proton/Wine program is running, so the game's prefix was not changed. Close the game\n"
               "and run the installer again, or add this to its launch options in Steam:\n"
               "  WINEDLLOVERRIDES=\"xinput1_3=n,b\" %%command%%\n");
        return;
    }
    switch (reg_add_override(reg))
    {
    case 1:
        path_join(list, sizeof(list), PREFIX_LIST);
        if ((f = fopen(list, "a"))) { fprintf(f, "%s\n", reg); fclose(f); }
        printf("  enabled xinput1_3.dll in the Proton prefix (%s, %s)\n", kind, appid);
        break;
    case 0:
        printf("  the Proton prefix (%s, %s) already loads xinput1_3.dll\n", kind, appid);
        break;
    default:
        printf("\nCould not change the Proton prefix %s. Add this to the game's launch options in Steam:\n"
               "  WINEDLLOVERRIDES=\"xinput1_3=n,b\" %%command%%\n", reg);
    }
}

static void proton_uninstall(void)
{
    char list[4200], *text, *line;
    path_join(list, sizeof(list), PREFIX_LIST);
    if (!(text = read_text(list, NULL))) return;
    if (wine_running()) fail("close the game (and other Proton/Wine programs) first");
    for (line = strtok(text, "\n"); line; line = strtok(NULL, "\n"))
        reg_remove_override(line);
    free(text);
    remove(list);
}
#endif

/* ---- game folder ---- */

/* Index in exe_builds of the game's SplitSecond.exe, -1 if unsupported, -2 if missing. */
static int exe_build(uint32_t *timestamp)
{
    char path[4200];
    uint8_t buf[0x2000];  /* the SecuROM builds have a long DOS stub: PE header at 0xd20-0xd40 */
    FILE *f;
    *timestamp = 0;
    path_join(path, sizeof(path), "SplitSecond.exe");
    if (!(f = fopen(path, "rb"))) return -2;
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    uint32_t pe = n == sizeof(buf) ? rd32(buf + 0x3c) : 0;
    if (!pe || pe > sizeof(buf) - 0x60 || memcmp(buf + pe, "PE\0\0", 4)) return -1;
    *timestamp = rd32(buf + pe + 8);
    for (size_t i = 0; i < sizeof(exe_builds) / sizeof(*exe_builds); i++)
        if (*timestamp == exe_builds[i].timestamp && rd32(buf + pe + 24 + 56) == exe_builds[i].image_size)
            return (int)i;
    return -1;
}

static void check_exe(void)
{
    uint32_t timestamp;
    int build = exe_build(&timestamp);
    if (build == -2) fail("SplitSecond.exe not found in %s", game_dir);
    if (build < 0) fail("this SplitSecond.exe version is not supported (timestamp %08x)", timestamp);
    printf("SplitSecond.exe: %s version\n", exe_builds[build].name);
}

static void find_game_dir(const char *arg, const char *argv0)
{
    if (arg) snprintf(game_dir, sizeof(game_dir), "%s", arg);
    else
    {
        snprintf(game_dir, sizeof(game_dir), ".");
        if (!file_exists("SplitSecond.exe") && argv0 && (strchr(argv0, '/') || strchr(argv0, '\\')))
        {
            snprintf(game_dir, sizeof(game_dir), "%s", argv0);
            char *s1 = strrchr(game_dir, '/'), *s2 = strrchr(game_dir, '\\');
            *(s1 > s2 ? s1 : s2) = 0;
        }
    }
    size_t len = strlen(game_dir);
    for (;;)
    {
        if (len > 1 && (game_dir[len - 1] == '/' || game_dir[len - 1] == '\\')) game_dir[--len] = 0;
        else if (len > 2 && game_dir[len - 1] == '.' && (game_dir[len - 2] == '/' || game_dir[len - 2] == '\\')) game_dir[len -= 2] = 0;
        else break;
    }
}

static void run(int uninstall)
{
    printf("game folder: %s\n", game_dir);
    check_exe();
    if (uninstall)
    {
#ifndef _WIN32
        proton_uninstall();
#endif
        if (!restore_archive()) printf("  data/resident.ark is not patched\n");
        remove_dll();
        remove_uninstaller();
        printf("Uninstalled.\n");
    }
    else
    {
        restore_archive();  /* reinstall over a previous version */
        patch_archive();
        install_dll();
        install_uninstaller();
#ifndef _WIN32
        proton_install();
#endif
        printf("Installed.\n");
    }
}

/* ---- finding the game ----
 * Without a game folder the installer lists the copies it can find, with the
 * state of the fix in each, and asks what to do:
 *   - the Steam version (appmanifest_297860.acf -> steamapps/common/<installdir>);
 *   - Linux: non-Steam shortcuts whose Exe is a SplitSecond.exe;
 *   - Windows: the disc version's InstallPath and the uninstall entries of
 *     installed programs whose folder has a SplitSecond.exe. */
#define MAX_GAMES 16
static struct game { char dir[PATH_LEN], source[320]; } games[MAX_GAMES];
static int game_count;

static void add_game(const char *dir, const char *source)
{
    char real[PATH_LEN], exe[PATH_LEN + 32];
    size_t len;
    FILE *f;
    if (game_count == MAX_GAMES || !real_path(dir, real)) return;
    for (len = strlen(real); len > 3 && (real[len - 1] == '/' || real[len - 1] == '\\'); ) real[--len] = 0;
    snprintf(exe, sizeof(exe), "%s%cSplitSecond.exe", real, PATH_SEP);
    if (!(f = fopen(exe, "rb"))) return;
    fclose(f);
    for (int i = 0; i < game_count; i++)
        if (same_path(games[i].dir, real)) return;
    snprintf(games[game_count].dir, PATH_LEN, "%s", real);
    snprintf(games[game_count].source, sizeof(games[0].source), "%s", source);
    game_count++;
}

#ifdef _WIN32
#define UNINSTALL_KEY "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"

/* The installer is 32-bit, so HKLM\SOFTWARE is the 32-bit view (WOW6432Node),
 * where the game and its installers write their keys. */
static void find_installed(void)
{
    char dir[PATH_LEN], name[256], key[400];
    HKEY h;
    if (reg_string(HKEY_LOCAL_MACHINE, "SOFTWARE\\Disney Interactive Studios\\Split Second", "InstallPath", dir, sizeof(dir)))
        add_game(dir, "installed");
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, UNINSTALL_KEY, 0, KEY_READ, &h)) return;
    for (DWORD i = 0, n = sizeof(name); !RegEnumKeyExA(h, i, name, &n, NULL, NULL, NULL, NULL); i++, n = sizeof(name))
    {
        snprintf(key, sizeof(key), UNINSTALL_KEY "\\%s", name);
        if (reg_string(HKEY_LOCAL_MACHINE, key, "InstallLocation", dir, sizeof(dir)) && dir[0])
            add_game(dir, strncmp(name, "Steam App ", 10) ? "installed" : "Steam");
    }
    RegCloseKey(h);
}
#else
static int collect_shortcut(const struct shortcut *sc, void *ctx)
{
    const char *name = strrchr(sc->exe, '/');
    char dir[PATH_LEN], source[300];
    (void)ctx;
    if (!name || strcasecmp(name + 1, "SplitSecond.exe") || !real_dir_of(sc->exe, dir)) return 0;
    snprintf(source, sizeof(source), "Steam shortcut \"%.250s\"", sc->name);
    add_game(dir, source);
    return 0;
}
#endif

static void find_games(void)
{
    find_steam_libs();
    for (int i = 0; i < steam_lib_count; i++)
    {
        char path[PATH_LEN + 64], dir[PATH_LEN * 2], *text, *p, *q;
        snprintf(path, sizeof(path), "%s/steamapps/appmanifest_297860.acf", steam_libs[i]);
        if (!(text = read_text(path, NULL))) continue;
        if ((p = strstr(text, "\"installdir\"")) && (p = strchr(p + 12, '"')) && (q = strchr(p + 1, '"')))
        {
            *q = 0;
            snprintf(dir, sizeof(dir), "%s/steamapps/common/%s", steam_libs[i], p + 1);
            add_game(dir, "Steam");
        }
        free(text);
    }
#ifdef _WIN32
    find_installed();
#else
    for_each_shortcut(collect_shortcut, NULL);
#endif
}

enum { ST_MISSING, ST_UNSUPPORTED, ST_NOT_INSTALLED, ST_INSTALLED, ST_INSTALLED_OTHER, ST_BROKEN };

/* State of the fix in game_dir. */
static int fix_state(uint32_t *timestamp, int *build)
{
    char path[4200];
    struct sidecar sc;
    int dll, patched = 0;
    if ((*build = exe_build(timestamp)) == -2) return ST_MISSING;
    if (*build < 0) return ST_UNSUPPORTED;
    path_join(path, sizeof(path), "xinput1_3.dll");
    dll = is_our_dll(path);
    if (file_exists("data/resident.ark") && load_sidecar(&sc))
    {
        uint32_t count;
        FILE *f = open_archive(&count);
        patched = archive_is_patched(f, count, &sc);
        fclose(f);
    }
    if (!dll && !patched) return ST_NOT_INSTALLED;
    if (!dll || !patched) return ST_BROKEN;
    /* same version if the DLL is byte for byte the one built into this installer */
    uint8_t *buf = NULL;
    long len = -1;
    FILE *f = fopen(path, "rb");
    if (f)
    {
        fseek(f, 0, SEEK_END);
        len = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (len == (long)payload_dll_size && (buf = malloc(len)) && fread(buf, 1, len, f) != (size_t)len) len = -1;
        fclose(f);
    }
    int same = buf && len == (long)payload_dll_size && !memcmp(buf, payload_dll, len);
    free(buf);
    return same ? ST_INSTALLED : ST_INSTALLED_OTHER;
}

static const char *state_text(int state, uint32_t timestamp, int build)
{
    static char text[200];
    const char *name = build >= 0 ? exe_builds[build].name : "";
    switch (state)
    {
    case ST_UNSUPPORTED: snprintf(text, sizeof(text), "unsupported SplitSecond.exe (timestamp %08x)", timestamp); break;
    case ST_NOT_INSTALLED: snprintf(text, sizeof(text), "%s version, fix not installed", name); break;
    case ST_INSTALLED: snprintf(text, sizeof(text), "%s version, fix installed (this version)", name); break;
    case ST_INSTALLED_OTHER: snprintf(text, sizeof(text), "%s version, fix installed (another version)", name); break;
    case ST_BROKEN: snprintf(text, sizeof(text), "%s version, fix needs reinstalling (game files were replaced)", name); break;
    default: snprintf(text, sizeof(text), "SplitSecond.exe not found");
    }
    return text;
}

static int ask(const char *prompt, char *answer, size_t size)
{
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(answer, (int)size, stdin)) return 0;
    answer[strcspn(answer, "\r\n")] = 0;
    return 1;
}

static int menu(void)
{
    char answer[32];
    int states[MAX_GAMES], builds[MAX_GAMES], choice;
    uint32_t stamps[MAX_GAMES];
    find_games();
    if (!game_count)
    {
#ifdef _WIN32
        printf("\nSplit/Second was not found. Copy %s into the game folder and run it there.\n", SELF_NAME);
#else
        printf("\nSplit/Second was not found through Steam. Run the installer with the game folder:\n"
               "  ./ssfix-install \"/path/to/Split Second\"\n");
#endif
        return 1;
    }
    printf("\nFound Split/Second:\n");
    for (int i = 0; i < game_count; i++)
    {
        memcpy(game_dir, games[i].dir, sizeof(game_dir));  /* same size */
        states[i] = fix_state(&stamps[i], &builds[i]);
        printf("  %d. %s  [%s]\n     %s\n", i + 1, games[i].dir, games[i].source, state_text(states[i], stamps[i], builds[i]));
    }
    if (!isatty(0))
    {
        printf("\nRun the installer in a terminal to choose, or pass the game folder.\n");
        return 1;
    }
    if (game_count == 1) choice = 1;
    else
    {
        char prompt[64];
        snprintf(prompt, sizeof(prompt), "\nChoose a game [1-%d], or q to quit: ", game_count);
        if (!ask(prompt, answer, sizeof(answer)) || (choice = atoi(answer)) < 1 || choice > game_count) return 0;
    }
    int st = states[choice - 1];
    memcpy(game_dir, games[choice - 1].dir, sizeof(game_dir));  /* same size */
    printf("\n%s\n%s\n", game_dir, state_text(st, stamps[choice - 1], builds[choice - 1]));
    if (st == ST_UNSUPPORTED || st == ST_MISSING) return 1;
    if (st == ST_NOT_INSTALLED)
    {
        if (!ask("  i) install    q) quit\n> ", answer, sizeof(answer)) || answer[0] != 'i') return 0;
        printf("\n");
        run(0);
        return 0;
    }
    if (!ask("  r) reinstall / update    u) uninstall    q) quit\n> ", answer, sizeof(answer))) return 0;
    if (answer[0] != 'r' && answer[0] != 'u') return 0;
    printf("\n");
    run(answer[0] == 'u');
    return 0;
}

int main(int argc, char **argv)
{
    int uninstall = 0;
    const char *dir = NULL;
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--uninstall") || !strcmp(argv[i], "-u")) uninstall = 1;
        else if (!strcmp(argv[i], "--from-script")) from_script = 1;
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h"))
        {
            printf("usage: %s [--uninstall] [game folder]\n"
                   "Without a folder: the current folder or the installer's own one if the game is there;\n"
                   "otherwise the copies of the game found on this computer are listed to choose from.\n", argv[0]);
            return 0;
        }
        else dir = argv[i];
    }
    printf("Split/Second gamepad fix %s\n", PAYLOAD_VERSION);
    find_game_dir(dir, argv[0]);
    if (!dir && !file_exists("SplitSecond.exe"))
    {
        int r = menu();
        pause_on_windows();
        return r;
    }
    run(uninstall);
    pause_on_windows();
    return 0;
}
