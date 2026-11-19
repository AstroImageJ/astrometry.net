#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <wasi/api.h>

#include "kdtree.h"
#include "starkd.h"
#include "starutil.h"
#include "fitstable.h"

#ifdef __wasm__
#define EXPORT(name) __attribute__((export_name(#name)))
#else
#define EXPORT(name)
#endif

#define MAXTAGS 8

static fitstable_t* tagtab;         // the table holding the tag-along columns, left on the right extension
static startree_t* st;
static kdtree_t* kd;
static int ntags;
static char* tagname[MAXTAGS];      // the real column names in the file (matched case-insensitively)

static int nhits;
static int* hit_index;
static double* hit_ra;
static double* hit_dec;
static double* hit_tag[MAXTAGS];

static void clear_hits(void) {
    free(hit_index); hit_index = NULL;
    free(hit_ra);    hit_ra = NULL;
    free(hit_dec);   hit_dec = NULL;
    for (int k = 0; k < MAXTAGS; k++) {
        free(hit_tag[k]);
        hit_tag[k] = NULL;
    }
    nhits = 0;
}

static int read_args(int* pargc, char*** pargv) {
    __wasi_size_t argc, bufsz;
    if (__wasi_args_sizes_get(&argc, &bufsz) != 0) return -1;
    char** argv = malloc((argc + 1) * sizeof(char*));
    char* buf = malloc(bufsz ? bufsz : 1);
    if (!argv || !buf) return -1;
    if (__wasi_args_get((uint8_t**) argv, (uint8_t*) buf) != 0) return -1;
    argv[argc] = NULL;
    *pargc = (int) argc;
    *pargv = argv;
    return 0;
}

// Find the extension that holds all the requested columns (like plotann.py's fits_table(fn, hdu=6)).
// libkd's own lookup (startree_get_tagalong) only accepts an extension whose header says AN_FILE = TAGALONG,
// which not every file has.
static int open_tag_table(const char* fn, char** want, int nwant) {
    fitstable_t* tab = fitstable_open(fn);
    if (!tab) return 3;
    int next = fitstable_n_extensions(tab);
    int seen[MAXTAGS] = {0};
    for (int ext = 1; ext < next; ext++) {
        if (fitstable_open_extension(tab, ext) != 0) continue;
        int ncol = fitstable_get_N_fits_columns(tab);
        const char* real[MAXTAGS] = {0};
        int all = 1;
        for (int k = 0; k < nwant; k++) {
            for (int j = 0; j < ncol; j++) {
                const char* n = fitstable_get_fits_column_name(tab, j);
                if (n && strcasecmp(n, want[k]) == 0) {
                    real[k] = n;
                    break;
                }
            }
            if (real[k]) seen[k] = 1; else all = 0;
        }
        if (all) {
            for (int k = 0; k < nwant; k++) tagname[k] = strdup(real[k]);
            tagtab = tab;
            return 0;
        }
    }
    for (int k = 0; k < nwant; k++)
        if (!seen[k]) return 4 + k;
    return 3;   // every column exists, but not all in the same table
}

// 0 = ok, 1 = cannot open, 2 = not a double 3-D tree,
// = no tag-along data (or too many columns), 4+k = requested tag-along column k does not exist
EXPORT(kd_open)
int kd_open(void) {
    int argc;
    char** argv;
    if (read_args(&argc, &argv) != 0 || argc < 2) return 1;

    st = startree_open(argv[1]);
    if (!st) return 1;
    kd = st->tree;
    if (kdtree_exttype(kd) != KDT_EXT_DOUBLE || kd->ndim != 3) return 2;

    ntags = argc - 2;
    if (ntags > MAXTAGS) return 3;
    if (ntags > 0) {
        int rc = open_tag_table(argv[1], argv + 2, ntags);
        if (rc != 0) return rc;
    }
    return 0;
}

// runs the search; returns the number of hits, or a negative error
EXPORT(kd_cone)
int kd_cone(double ra, double dec, double radius) {
    if (!kd) return -1;
    clear_hits();

    double xyz[3];
    radecdeg2xyzarr(ra, dec, xyz);
    kdtree_qres_t* res = kdtree_rangesearch_options(kd, xyz, deg2distsq(radius), KD_OPTIONS_RETURN_POINTS);
    if (!res) return -2;

    int n = (int) res->nres;
    if (n == 0) {
        kdtree_free_query(res);
        return 0;
    }

    int* inds = malloc((size_t) n * sizeof(int));
    hit_index = malloc((size_t) n * sizeof(int));
    hit_ra = malloc((size_t) n * sizeof(double));
    hit_dec = malloc((size_t) n * sizeof(double));
    if (!inds || !hit_index || !hit_ra || !hit_dec) {
        free(inds);
        kdtree_free_query(res);
        clear_hits();
        return -2;
    }
    for (int i = 0; i < n; i++) {
        // inds[i] is already the ORIGINAL catalog index: libkd applies the permutation when it records a result
        inds[i] = (int) res->inds[i];
        hit_index[i] = inds[i];
        xyzarr2radecdeg(res->results.d + 3 * i, &hit_ra[i], &hit_dec[i]);
    }
    kdtree_free_query(res);

    // tag-along tables are stored in original catalog order, so the same indices address them
    for (int k = 0; k < ntags; k++) {
        hit_tag[k] = fitstable_read_column_inds(tagtab, tagname[k], fitscolumn_double_type(), inds, n);
        if (!hit_tag[k]) {
            free(inds);
            clear_hits();
            return -3 - k;
        }
    }
    free(inds);
    nhits = n;
    return n;
}

//catalog index of hit i
EXPORT(kd_index)
int kd_index(int i) {
    return (i >= 0 && i < nhits) ? hit_index[i] : -1;
}

// degrees
EXPORT(kd_ra)
double kd_ra(int i) {
    return (i >= 0 && i < nhits) ? hit_ra[i] : NAN;
}

// degrees
EXPORT(kd_dec)
double kd_dec(int i) {
    return (i >= 0 && i < nhits) ? hit_dec[i] : NAN;
}

// value of the k-th requested tag-along column for hit i
EXPORT(kd_tag)
double kd_tag(int i, int k) {
    return (i >= 0 && i < nhits && k >= 0 && k < ntags && hit_tag[k]) ? hit_tag[k][i] : NAN;
}