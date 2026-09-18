/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * czet - self-contained driver for the CZet compiler (stripped GCC).
 *
 * Self-extracting binary: appended to the executable itself:
 *   [ustar tar blob] [32B trailer]
 * where trailer = magic "CZETAR" + u64le off + u64le size + u64le hash.
 *
 * At runtime:
 *   1. Resolve own executable (/proc/self/exe), locate trailer and blob.
 *   2. Extract the blob (gcc toolchain + musl/glibc sysroots) to
 *      $XDG_CACHE_HOME/czet/<hash>/ (~/.cache/czet/<hash>/), unless
 *      already extracted.
 *   3. Build the xgcc command line for the chosen libc (-libc=musl|glibc|
 *      <path>; musl by default) and execv() xgcc.
 *
 * No fork()/spawnp()/system().  The embedded toolchain comes from this
 * project's build; as/ld come from the system (via PATH).
 *
 * Static build:
 *   cc -static -O2 -s -std=c11 src/czet.c -o czet
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#define CZET_MAGIC  "CZETAR"
#define TRAILER_LEN   32
#define CHUNK         65536
#define HDR_LEN       512

/* Per-arch default dynamic loaders. Resolved at compile time so each
 * native binary (x86_64 / i686 / aarch64) embeds the right paths. */
#if defined(__aarch64__)
#define CZET_MUSL_LOADER  "/lib/ld-musl-aarch64.so.1"
#define CZET_GLIBC_LOADER "/lib/ld-linux-aarch64.so.1"
#define CZET_LD_LINUX     "ld-linux-aarch64.so.1"
#elif defined(__i386__) || defined(__i686__)
#define CZET_MUSL_LOADER  "/lib/ld-musl-x86.so.1"
#define CZET_GLIBC_LOADER "/lib/ld-linux.so.2"
#define CZET_LD_LINUX     "ld-linux.so.2"
#else /* x86_64 and fallback */
#define CZET_MUSL_LOADER  "/lib/ld-musl-x86_64.so.1"
#define CZET_GLIBC_LOADER "/lib64/ld-linux-x86-64.so.2"
#define CZET_LD_LINUX     "ld-linux-x86-64.so.2"
#endif

/* ============================ helpers ============================== */

static uint64_t le64u(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--)
        v = (v << 8) | p[i];
    return v;
}

static char *ndup(const char *s, size_t n)
{
    char *r = malloc(n + 1);
    if (!r) return NULL;
    memcpy(r, s, n);
    r[n] = '\0';
    return r;
}

static char *cat(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    char *r = malloc(la + lb + 1);
    if (!r) return NULL;
    memcpy(r, a, la);
    memcpy(r + la, b, lb + 1);
    return r;
}

static const char *self_path(char *buf, size_t buflen)
{
    if (buflen < 2) return NULL;
    ssize_t n = readlink("/proc/self/exe", buf, buflen - 1);
    if (n < 0)
        n = readlink("/proc/curproc/file", buf, buflen - 1);
    if (n < 0)
        return NULL;
    buf[n] = '\0';
    return buf;
}

/* =========================== trailer ============================= */

struct trailer {
    uint64_t off;   /* Blob offset within the file */
    uint64_t size;  /* Blob size                   */
    uint64_t hash;  /* FNV-1a 64 of the blob       */
};

static int read_trailer(FILE *f, struct trailer *t)
{
    uint8_t h[TRAILER_LEN];
    if (fseek(f, -(long)TRAILER_LEN, SEEK_END) != 0)
        return -1;
    if (fread(h, 1, TRAILER_LEN, f) != TRAILER_LEN)
        return -1;
    if (memcmp(h, CZET_MAGIC, 7) != 0)
        return -1;
    t->off  = le64u(h + 8);
    t->size = le64u(h + 16);
    t->hash = le64u(h + 24);
    return 0;
}

/* FNV-1a 64 over the blob (for debugging / --extract-only). */
static uint64_t fnv1a64(FILE *f, uint64_t off, uint64_t size)
{
    uint64_t h = 0xcbf29ce484222325ULL;
    uint8_t buf[CHUNK];
    if (fseek(f, (long)off, SEEK_SET) != 0)
        return 0;
    uint64_t left = size;
    while (left > 0) {
        size_t want = left > CHUNK ? CHUNK : (size_t)left;
        size_t got = fread(buf, 1, want, f);
        if (got == 0) break;
        for (size_t i = 0; i < got; i++) {
            h ^= buf[i];
            h *= 0x100000001b3ULL;
        }
        left -= got;
    }
    return h;
}

/* =========================== fs helpers =========================== */

static int mkdirs(const char *path)
{
    char tmp[PATH_MAX];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp))
        return -1;
    memcpy(tmp, path, len + 1);
    while (len > 0 && tmp[len - 1] == '/')
        tmp[--len] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
                return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

/* Recursively delete a tree (no /bin needed on NixOS). */
static void remove_tree(const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0)
        return;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (!d) return;
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
                continue;
            char *sub = cat(cat(path, "/"), e->d_name);
            if (sub) {
                remove_tree(sub);
                free(sub);
            }
        }
        closedir(d);
        rmdir(path);
    } else {
        unlink(path);
    }
}

/* Safe tar name: no .. or absolute paths. */
static int safe_name(const char *name)
{
    if (name[0] == '/') return -1;
    if (strcmp(name, "..") == 0) return -1;
    const char *s = name;
    while ((s = strstr(s, "/.."))) {
        if (s[3] == '/' || s[3] == '\0')
            return -1;
        s += 3;
    }
    return 0;
}

/* =========================== tar extractor ========================= */

static uint64_t parse_num(const char *p, size_t n)
{
    const unsigned char *b = (const unsigned char *)p;
    if (n > 0 && (b[0] & 0x80)) {          /* base-256 (GNU/posix) */
        uint64_t v = b[0] & 0x7f;
        for (size_t i = 1; i < n; i++)
            v = (v << 8) | b[i];
        return v;
    }
    uint64_t v = 0;                         /* octal */
    for (size_t i = 0; i < n; i++) {
        unsigned char c = b[i];
        if (c == 0) break;
        if (c >= '0' && c <= '7')
            v = (v << 3) | (c - '0');
        else
            break;
    }
    return v;
}

static int write_all(int fd, const uint8_t *buf, size_t n)
{
    while (n > 0) {
        ssize_t w = write(fd, buf, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        buf += w;
        n -= (size_t)w;
    }
    return 0;
}

static int copy_data(FILE *in, uint64_t size, int outfd)
{
    uint8_t buf[CHUNK];
    while (size > 0) {
        size_t want = size > CHUNK ? CHUNK : (size_t)size;
        size_t got = fread(buf, 1, want, in);
        if (got == 0) return -1;
        if (write_all(outfd, buf, got) != 0) return -1;
        size -= got;
    }
    return 0;
}

/* Skip only the padding up to a 512 multiple (after the data). */
static int skip_pad_only(FILE *in, uint64_t size)
{
    uint64_t pend = (512 - size % 512) % 512;
    uint8_t tmp[HDR_LEN];
    while (pend > 0) {
        size_t r = fread(tmp, 1, (size_t)pend, in);
        if (r == 0) return -1;
        pend -= r;
    }
    return 0;
}

/* Skip size + padding to a 512 multiple. */
static int skip_padded(FILE *in, uint64_t size)
{
    uint64_t pend = (512 - size % 512) % 512;
    uint8_t tmp[HDR_LEN];
    uint64_t skip = size;
    while (skip > 0) {
        size_t want = skip > HDR_LEN ? HDR_LEN : (size_t)skip;
        size_t r = fread(tmp, 1, want, in);
        if (r == 0) return -1;
        skip -= r;
    }
    while (pend > 0) {
        size_t r = fread(tmp, 1, (size_t)pend, in);
        if (r == 0) return -1;
        pend -= r;
    }
    return 0;
}

/*
 * Extract an uncompressed ustar/posix tar over base, reading from the
 * current offset of `in'.  Return 0 on success, -1 on error.
 */
static int extract_tar(FILE *in, const char *base)
{
    uint8_t hdr[HDR_LEN];
    for (;;) {
        size_t got = fread(hdr, 1, HDR_LEN, in);
        if (got < HDR_LEN) {
            return got == 0 ? 0 : -1;       /* End of archive. */
        }
        int allzero = 1;
        for (size_t i = 0; i < HDR_LEN; i++)
            if (hdr[i]) { allzero = 0; break; }
        if (allzero)
            return 0;

        char *name = ndup((const char *)hdr, 100);
        const char *prefix = ndup((const char *)hdr + 345, 155);
        char typeflag = hdr[156];
        uint64_t size = parse_num((const char *)hdr + 124, 12);
        if (!name || !prefix) {
            free(name); free((char *)prefix);
            return -1;
        }

        if (typeflag == 'L' || typeflag == 'K') {
            /* GNU longname/longlink: the next header is the real one.
             * Long-name payload discarded (never emitted as ustar). */
            if (skip_padded(in, size) != 0) {
                free(name); free((char *)prefix);
                return -1;
            }
            free(name); free((char *)prefix);
            continue;
        }

        char *rel;
        if (prefix[0]) {
            /* Join prefix and name. */
            char *p2 = cat(prefix, "/");
            rel = p2 ? cat(p2, name) : NULL;
            free(p2);
            free(name);
        } else {
            /* No prefix: rel takes over name. */
            rel = name;
        }
        free((char *)prefix);
        if (!rel)
            return -1;

        if (safe_name(rel) != 0) {
            free(rel);
            return -1;
        }
        char *out = cat(cat(base, "/"), rel);
        free(rel);
        if (!out)
            return -1;

        int ret = 0;
        if (typeflag == '5') {                       /* Directory. */
            if (mkdir(out, 0755) != 0 && errno != EEXIST)
                ret = -1;
        } else if (typeflag == '2') {                /* symlink */
            char *ln = ndup((const char *)hdr + 157, 100);
            if (!ln) { ret = -1; }
            else if (unlink(out) != 0 && errno != ENOENT)
                ret = -1;
            else if (symlink(ln, out) != 0)
                ret = -1;
            free(ln);
        } else if (typeflag == '0' || typeflag == '\0' || typeflag == '7') {
            /* Regular file: ensure parent, then write. */
            char *slash = strrchr(out, '/');
            char saved = 0;
            if (slash && slash != out) {
                saved = *slash;
                *slash = '\0';
            }
            if (mkdirs(slash && slash != out ? out : ".") != 0) {
                if (saved) *slash = saved;
                ret = -1;
                goto freeout;
            }
            if (saved) *slash = saved;

            int fd = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0755);
            if (fd < 0) { ret = -1; goto freeout; }
            if (copy_data(in, size, fd) != 0) ret = -1;
            if (close(fd) != 0) ret = -1;
            if (skip_pad_only(in, size) != 0) ret = -1;
        } else {
            if (skip_padded(in, size) != 0) ret = -1;
        }
freeout:
        free(out);
        if (ret != 0)
            return -1;
    }
}

/* =========================== cache ============================= */

static char *cache_root(void)
{
    const char *x = getenv("XDG_CACHE_HOME");
    if (x && *x)
        return strdup(x);
    const char *h = getenv("HOME");
    if (!h || !*h) h = "/tmp";
    return cat(h, "/.cache");
}

/*
 * Return malloc'd path of the extracted cache dir, or NULL.
 * Extract (or reuse) the blob under <cache>/czet/<hash16>.
 */
static char *ensure_cache(FILE *self, const struct trailer *tr)
{
    char hashstr[17];
    snprintf(hashstr, sizeof(hashstr), "%016llx",
             (unsigned long long)tr->hash);

    char *base = cache_root();
    if (!base) return NULL;
    char *cb = cat(base, "/czet");
    free(base);
    if (!cb) return NULL;
    char *dir = cat(cat(cb, "/"), hashstr);
    free(cb);
    if (!dir) return NULL;

    if (mkdirs(dir) != 0) {
        free(dir);
        return NULL;
    }

    char *done = cat(dir, "/.done");
    if (!done) { free(dir); return NULL; }

    /* Already extracted? */
    int cached = 0;
    FILE *mf = fopen(done, "r");
    if (mf) {
        char marker[17] = {0};
        size_t n = fread(marker, 1, 16, mf);
        fclose(mf);
        if (n >= 16 && strncmp(marker, hashstr, 16) == 0)
            cached = 1;
    }
    if (cached) {
        free(done);
        return dir;
    }

    /* Extract to a temp dir, then rename into place. */
    char *tpl = cat(dir, ".tmp-XXXXXX");
    if (!tpl) { free(done); free(dir); return NULL; }
    char *work = mkdtemp(tpl);
    if (!work) { free(tpl); free(done); free(dir); return NULL; }

    if (fseek(self, (long)tr->off, SEEK_SET) != 0 ||
        extract_tar(self, work) != 0) {
        remove_tree(work);
        free(tpl); free(done); free(dir);
        return NULL;
    }

    /* Version stamp inside the extracted tree. */
    char *d2 = cat(work, "/.done");
    if (d2) {
        FILE *df = fopen(d2, "w");
        if (df) {
            fwrite(hashstr, 1, 16, df);
            fclose(df);
        }
        free(d2);
    }

    /* Atomic replace: remove old dir, rename. */
    remove_tree(dir);
    if (rename(work, dir) != 0) {
        remove_tree(work);
        free(tpl); free(done); free(dir);
        return NULL;
    }

    free(tpl); free(done);
    return dir;
}

/* =========================== driver ============================ */

/* CZet sources needing explicit `-x c` wrapping: gcc keys language off
   known suffixes, so unknown ones (.ct, .czet) must be wrapped.
   Plain .c files need no wrapping: they pass through natively and get
   -std=czet by default (needstd below).  Returns 1 if A needs wrapping. */
static int is_wrapped_czet_src(const char *a)
{
    size_t n;
    if (a == NULL || a[0] == '-' || a[0] == '\0')
        return 0;
    n = strlen(a);
    if (n > 3 && strcmp(a + n - 3, ".ct") == 0)
        return 1;
    if (n > 5 && strcmp(a + n - 5, ".czet") == 0)
        return 1;
    return 0;
}

static void print_usage(const char *prog)
{
    printf("Usage: %s [options] file...\n"
           "CZet compiler driver (self-contained GCC + musl/glibc).\n"
           "\n"
           "Options:\n"
           "  -libc=musl|glibc|/path/sysroot\n"
           "      Select the libc sysroot (default: musl, static).\n"
           "  -static\n"
           "      Fully static link (default mode is dynamic with\n"
           "      -static-libgcc + pinned loader).\n"
           "  -std=czet (default) | -std=gnu23\n"
           "      -std=czet enables CZet extensions; any explicit -std\n"
           "      disables the default.\n"
           "  --extract-only\n"
           "      Extract the embedded toolchain blob to cache and print\n"
           "      its path.\n"
           "  -h, --help\n"
           "      Show this help.\n"
           "  All other options are forwarded to the embedded xgcc.\n"
           "\n"
           "Source files:\n"
           "  .ct, .czet   CZet sources (compiled as C with extensions).\n"
           "  .c           Plain C sources (built with -std=czet by default).\n"
           "\n"
           "Examples:\n"
           "  %s -static hello.ct -o hello\n"
           "  %s -static hello.czet -o hello\n"
           "  %s -static hello.c -o hello\n"
           "  %s -libc=glibc -static hello.ct -o hello\n",
           prog, prog, prog, prog, prog);
}

int main(int argc, char **argv)
{
    for (int a = 1; a < argc; a++)
        if (strcmp(argv[a], "-h") == 0
            || strcmp(argv[a], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    char exebuf[PATH_MAX];
    const char *exe = self_path(exebuf, sizeof(exebuf));
    if (!exe) {
        fprintf(stderr, "czet: cannot resolve executable.\n");
        return 1;
    }

    FILE *self = fopen(exe, "rb");
    if (!self) {
        fprintf(stderr, "czet: cannot open %s: %s\n", exe, strerror(errno));
        return 1;
    }

    struct trailer tr;
    if (read_trailer(self, &tr) != 0) {
        fprintf(stderr, "czet: binary has no toolchain blob "
                        "(missing CZET trailer).\n");
        fclose(self);
        return 1;
    }

    /* Sanity check: file = exe + blob + trailer. */
    long filesz = 0;
    if (fseek(self, 0, SEEK_END) == 0)
        filesz = ftell(self);
    if (filesz > 0 &&
        (unsigned long long)filesz != tr.off + tr.size + TRAILER_LEN) {
        fprintf(stderr, "czet: inconsistent blob size (%ld != %llu).\n",
                filesz, (unsigned long long)(tr.off + tr.size + TRAILER_LEN));
        fclose(self);
        return 1;
    }

    char *cache = ensure_cache(self, &tr);
    fclose(self);
    if (!cache) {
        fprintf(stderr, "czet: cannot set up extraction cache.\n");
        return 1;
    }

    for (int a = 1; a < argc; a++)
        if (strcmp(argv[a], "--extract-only") == 0) {
            FILE *chk = fopen(exe, "rb");
            uint64_t h = 0;
            if (chk)
                h = fnv1a64(chk, tr.off, tr.size);
            if (chk) fclose(chk);
            if (h != tr.hash)
                fprintf(stderr, "czet: warning: blob hash %016llx != "
                        "expected %016llx\n",
                        (unsigned long long)h, (unsigned long long)tr.hash);
            printf("czet: blob '%016llx' extracted to %s\n",
                   (unsigned long long)tr.hash, cache);
            free(cache);
            return 0;
        }

    /* Libc selection. */
    const char *libc = "musl";
    const char *lcroot = NULL;
    for (int a = 1; a < argc; a++) {
        if (strncmp(argv[a], "-libc=", 6) == 0) {
            libc = argv[a] + 6;
            if (strcmp(libc, "musl") != 0 && strcmp(libc, "glibc") != 0)
                lcroot = libc;
        }
    }

    char *gccdir = cat(cache, "/gcc/");
    char *lgdir  = cat(cache, "/libgcc/");
    char *ginc   = cat(cache, "/gcc/include");
    char *sysinc, *syslib;
    if (lcroot) {
        sysinc = cat(cat(lcroot, "/"), "include");
        syslib = cat(cat(lcroot, "/"), "lib");
    } else {
        char *sr = cat(cat(cache, "/sysroot/"), libc);
        sysinc = cat(cat(sr, "/"), "include");
        syslib = cat(cat(sr, "/"), "lib");
        free(sr);
    }
    if (!gccdir || !lgdir || !ginc || !sysinc || !syslib) {
        fprintf(stderr, "czet: out of memory.\n");
        free(cache); free(gccdir); free(lgdir); free(ginc);
        free(sysinc); free(syslib);
        return 1;
    }

    /* glibc links '-lc' (dynamic) through a linker script with absolute
       store paths; regenerate it against the extracted cache. */
    if (strcmp(libc, "glibc") == 0) {
        char *script = cat(syslib, "/libc.so");
        char *so6    = cat(syslib, "/libc.so.6");
        char *ns     = cat(syslib, "/libc_nonshared.a");
        char *ld     = cat(syslib, "/" CZET_LD_LINUX);
        if (script && so6 && ns && ld) {
            size_t len = strlen(so6) + strlen(ns) + strlen(ld) + 160;
            char *body = malloc(len);
            if (body) {
                snprintf(body, len,
                         "/* GNU ld script */\n"
                         "GROUP ( %s %s AS_NEEDED ( %s ) )\n",
                         so6, ns, ld);
                FILE *f = fopen(script, "w");
                if (f) { fputs(body, f); fclose(f); }
                free(body);
            }
            free(script); free(so6); free(ns); free(ld);
        }
    }

    /* -std=czet by default unless set; detect explicit -static.
       CZet sources (.ct, .czet) are C, but the gcc driver keys
       language off known suffixes: wrap each such input in -x c ... -x none.
       Plain .c inputs pass through directly (still built with -std=czet
       when active).  Skip the -o parameter, which names an output. */
    int nuser = 0, needstd = 1, is_static = 0, wrap_extra = 0, skip_next = 0;
    for (int a = 1; a < argc; a++) {
        if (strncmp(argv[a], "-libc=", 6) == 0)
            continue;
        if (strncmp(argv[a], "-std", 4) == 0)
            needstd = 0;
        if (strcmp(argv[a], "-static") == 0)
            is_static = 1;
        if (skip_next) {
            skip_next = 0;
        } else if (strcmp(argv[a], "-o") == 0) {
            skip_next = 1;
        } else {
            if (is_wrapped_czet_src(argv[a]))
                wrap_extra += 4;
        }
        nuser++;
    }

    /* Dynamic link by default (needs the libc installed); -static links
       fully statically.  Dynamic mode forces -static-libgcc and pins the
       loader of the chosen libc. */
    /* CZet runtime (prebuilt libaco/critbit/btree): embedded sysroots
       only; passed by absolute path AFTER user objects for link order. */
    char *utils_a = lcroot ? NULL : cat(syslib, "/libczet_utils.a");
    int nfix = 14 + (needstd ? 1 : 0);
    char **args = calloc((size_t)(nfix + nuser + 3 + wrap_extra),
                         sizeof(char *));
    if (!args) {
        fprintf(stderr, "czet: out of memory.\n");
        free(cache); free(gccdir); free(lgdir); free(ginc);
        free(sysinc); free(syslib); free(utils_a);
        return 1;
    }

    char *xgcc = cat(gccdir, "xgcc");
    int i = 0;
    args[i++] = xgcc;
    args[i++] = cat("-B", gccdir);   /* cc1, collect2, lto-wrapper, crt*, liblto_plugin */
    args[i++] = cat("-B", lgdir);    /* libgcc.a y libgcc_eh.a                   */
    args[i++] = cat("-L", lgdir);
    args[i++] = strdup("-isystem");
    args[i++] = ginc;                 /* stddef.h, stdarg.h, float.h, ...         */
    args[i++] = strdup("-isystem");
    args[i++] = strdup(sysinc);       /* cabeceras de la libc elegida             */
    args[i++] = cat("-B", syslib);    /* crt1.o, crti.o, crtn.o                   */
    args[i++] = cat("-L", syslib);    /* libc.a y libc.so                          */
    if (is_static) {
        args[i++] = strdup("-static");
    } else {
        char *loader = strcmp(libc, "musl") == 0
                           ? CZET_MUSL_LOADER
                           : (strcmp(libc, "glibc") == 0
                                  ? CZET_GLIBC_LOADER
                                  : NULL);
        args[i++] = strdup("-static-libgcc");
        if (loader)
            args[i++] = cat("-Wl,--dynamic-linker=", loader);
    }
    if (needstd)
        args[i++] = strdup("-std=czet");

    skip_next = 0;
    for (int a = 1; a < argc; a++) {
        if (strncmp(argv[a], "-libc=", 6) == 0)
            continue;
        if (!skip_next && strcmp(argv[a], "-o") == 0) {
            skip_next = 1;
            args[i++] = argv[a];
            continue;
        }
        if (skip_next) {
            skip_next = 0;
            args[i++] = argv[a];
            continue;
        }
        if (is_wrapped_czet_src(argv[a])) {
            args[i++] = strdup("-x");
            args[i++] = strdup("c");
            args[i++] = argv[a];
            args[i++] = strdup("-x");
            args[i++] = strdup("none");
            continue;
        }
        args[i++] = argv[a];
    }
    if (utils_a)
        args[i++] = utils_a;
    args[i] = NULL;

    execv(xgcc, args);

    fprintf(stderr, "czet: execv(%s): %s\n", xgcc, strerror(errno));
    return 1;
}