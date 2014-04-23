/*
 * ngtdump.c -- print data.
 */
#include "internal.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gtool3.h"
#include "seq.h"
#include "fileiter.h"
#include "myutils.h"
#include "logging.h"
#include "range.h"

#ifndef min
#  define min(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#  define max(a,b) ((a) > (b) ? (a) : (b))
#endif


#define PROGNAME "ngtdump"

#define DATA(vbuf, i) \
    (((vbuf)->type == GT3_TYPE_DOUBLE) \
    ? *((double *)((vbuf)->data) + (i)) \
    : *((float *) ((vbuf)->data) + (i)) )

#define ISMISS(vbuf, i) \
    (((vbuf)->type == GT3_TYPE_DOUBLE) \
    ? *((double *)((vbuf)->data) + (i)) == vbuf->miss \
    : *((float *) ((vbuf)->data) + (i)) == (float)vbuf->miss )

/*
 * global range setting.
 */
#define RANGE_MAX 0x7fffffff
static struct range g_range[] = {
    { 0, RANGE_MAX },
    { 0, RANGE_MAX },
    { 0, RANGE_MAX }
};
static struct sequence *g_zseq = NULL;

static int use_index_flag = 1;
static int quick_mode = 0;


char *
snprintf_date(char *buf, size_t len, const GT3_Date *date)
{
    snprintf(buf, len, "%04d-%02d-%02d %02d:%02d:%02d",
            date->year, date->mon, date->day,
            date->hour, date->min, date->sec);

    return buf;
}


int
dump_info(GT3_File *fp, const GT3_HEADER *head)
{
    char hbuf[33];
    int i;

    printf("#\n");
    printf("# %14s: %d\n", "Data No.", fp->curr + 1);
    {
        const char *keys[] = {"DSET", "ITEM",
                              "TITLE", "UNIT",
                              "DFMT"};

        for (i = 0; i < sizeof keys / sizeof keys[0]; i++) {
            GT3_copyHeaderItem(hbuf, sizeof hbuf, head, keys[i]);
            printf("# %14s: %s\n", keys[i], hbuf);
        }
    }
    printf("# %14s: %dx%dx%d\n", "Data Shape",
           fp->dimlen[0], fp->dimlen[1], fp->dimlen[2]);
    {
        const char *keys[] = {"DATE", "DATE1", "DATE2"};
        GT3_Date date;

        for (i = 0; i < sizeof keys / sizeof keys[0]; i++) {
            if (GT3_decodeHeaderDate(&date, head, keys[i]) == 0) {
                snprintf_date(hbuf, sizeof hbuf, &date);
                printf("# %14s: %s\n", keys[i], hbuf);
            }
        }
    }
    printf("#\n");
    return 0;
}


void
set_dimvalue(char *hbuf, size_t len, const GT3_Dim *dim, int idx)
{
    if (use_index_flag || dim == NULL) {
        snprintf(hbuf, len, "%13d", idx + 1);
        return;
    }

    if (idx == -1)
        snprintf(hbuf, len, "%13s", "Averaged");
    else if (idx < -1 || idx >= dim->len)
        snprintf(hbuf, len, "%13s", "OutOfRange");
    else
        snprintf(hbuf, len, "%13.6g", dim->values[idx]);
}


int
dump_var(GT3_Varbuf *var, const GT3_HEADER *head)
{
    int x, y, z, n, nz, ij;
    GT3_Dim *dim[] = { NULL, NULL, NULL };
    char key2[] = { '1', '2', '3' };
    char key[17];
    char vstr[32];
    unsigned missf;
    double val;
    struct range range[3];
    int off[] = { 1, 1, 1 };
    char hbuf[17];
    char dimv[3][32];
    char items[3][32];
    char vfmt[16];
    int nwidth, nprec;
    int newline_z, newline_y;
    int rval = 0;

    for (n = 0; n < 3; n++) {
        snprintf(key, sizeof key, "AITM%c", key2[n]);
        GT3_copyHeaderItem(hbuf, sizeof hbuf, head, key);
        snprintf(items[n], sizeof items[n], "%13s",
                 hbuf[0] == '\0' ? "(No axis)" : hbuf);

        if (!use_index_flag && (dim[n] = GT3_getDim(hbuf)) == NULL) {
            GT3_printErrorMessages(stderr);
            logging(LOG_ERR, "%s: Unknown axis name.", hbuf);
            snprintf(items[n], sizeof items[n], "%12s?", hbuf);
        }

        /* off */
        snprintf(key, sizeof key, "ASTR%c", key2[n]);
        GT3_decodeHeaderInt(off + n, head, key);
        off[n]--;

        /* range */
        range[n].str = max(0, g_range[n].str);
        range[n].end = min(var->fp->dimlen[n], g_range[n].end);
    }

    if (g_zseq) {
        reinitSeq(g_zseq, 1, var->fp->dimlen[2]);
        nz = countSeq(g_zseq);
    } else
        nz = range[2].end - range[2].str;

    if (   range[0].end - range[0].str <= 0
        || range[1].end - range[1].str <= 0
        || nz <= 0) {
        printf("#%s\n", "No Data in specified region.\n");
        goto finish;
    }

    /*
     * set printf-format for variables
     */
    switch (var->fp->fmt) {
    case GT3_FMT_UR8:
        nprec = 17;
        break;
    case GT3_FMT_URC:
    case GT3_FMT_URC1:
        nprec = 7;
        break;
    default:
        nprec = 8;
        break;
    }
    nwidth = nprec + 9;
    snprintf(vfmt, sizeof vfmt, "%%%d.%dg", nwidth, nprec);

    GT3_copyHeaderItem(hbuf, sizeof hbuf, head, "ITEM");
    printf("#%s%s%s%*s\n",
           items[0], items[1], items[2], nwidth, hbuf);

    newline_y = range[0].end - range[0].str > 1;
    newline_z = newline_y || range[1].end - range[1].str > 1;

    for (n = 0; n < nz; n++) {
        if (g_zseq) {
            if (nextSeq(g_zseq) < 0) {
                logging(LOG_WARN, "NOTREACHED");
                break;
            }
            z = g_zseq->curr - 1;
        } else
            z = n + range[2].str;

        if (GT3_readVarZ(var, z) < 0) {
            GT3_printErrorMessages(stderr);
            rval = -1;
            break;
        }
        if (n > 0 && newline_z)
            putchar('\n');

        set_dimvalue(dimv[2], sizeof dimv[2], dim[2], z + off[2]);

        for (y = range[1].str; y < range[1].end; y++) {
            if (y > range[1].str && newline_y)
                putchar('\n');

            set_dimvalue(dimv[1], sizeof dimv[1], dim[1], y + off[1]);

            for (x = range[0].str; x < range[0].end; x++) {
                set_dimvalue(dimv[0], sizeof dimv[0], dim[0], x + off[0]);

                ij = x + var->dimlen[0] * y;
                missf = ISMISS(var, ij);
                vstr[0] = '_';
                vstr[1] = '\0';
                if (!missf) {
                    val = DATA(var, ij);
                    snprintf(vstr, sizeof vstr, vfmt, val);
                }
                printf(" %s%s%s%*s\n",
                       dimv[0], dimv[1], dimv[2], nwidth, vstr);
            }
        }
    }

finish:
    GT3_freeDim(dim[0]);
    GT3_freeDim(dim[1]);
    GT3_freeDim(dim[2]);
    return rval;
}


int
ngtdump(const char *path, struct sequence *seq)
{
    GT3_File *fp;
    GT3_Varbuf *var;
    GT3_HEADER head;
    file_iterator it;
    int rval = -1;
    int stat;

    if ((fp = quick_mode ? GT3_openHistFile(path) : GT3_open(path)) == NULL
        || (var = GT3_getVarbuf(fp)) == NULL) {
        GT3_printErrorMessages(stderr);
        return -1;
    }

    printf("###\n# Filename: %s\n", path);
    setup_file_iterator(&it, fp, seq);
    while ((stat = iterate_file(&it)) != ITER_END) {
        if (stat == ITER_ERROR || stat == ITER_ERRORCHUNK)
            goto finish;
        if (stat == ITER_OUTRANGE)
            continue;

        if (GT3_readHeader(&head, fp) < 0) {
            GT3_printErrorMessages(stderr);
            goto finish;
        }
        if (dump_info(fp, &head) < 0 || dump_var(var, &head) < 0)
            goto finish;
    }
    rval = 0;

finish:
    GT3_freeVarbuf(var);
    GT3_close(fp);
    return rval;
}


void
usage(void)
{
    const char *usage_message =
        "Usage: " PROGNAME " [options] files...\n"
        "\n"
        "View data.\n"
        "\n"
        "Options:\n"
        "    -Q        quick access mode\n"
        "    -h        print help message\n"
        "    -a        print grid-value instead of grid-index\n"
        "    -t LIST   specify data No.\n"
        "    -x RANGE  specify X-range\n"
        "    -y RANGE  specify Y-range\n"
        "    -z RANGE  specify Z-range\n"
        "\n"
        "    RANGE  := start[:[end]] | :[end]\n"
        "    LIST   := RANGE[,RANGE]*\n";

    fprintf(stderr, "%s\n", GT3_version());
    fprintf(stderr, "%s\n", usage_message);
}


int
main(int argc, char **argv)
{
    struct sequence *seq = NULL;
    int ch;
    int rval = 0;

    open_logging(stderr, PROGNAME);
    GT3_setProgname(PROGNAME);
    while ((ch = getopt(argc, argv, "Qat:x:y:z:h")) != -1)
        switch (ch) {
        case 'Q':
            quick_mode = 1;
            break;

        case 'a':
            use_index_flag = 0;
            break;

        case 't':
            if ((seq = initSeq(optarg, 1, 0x7fffffff)) == NULL) {
                logging(LOG_SYSERR, NULL);
                exit(1);
            }
            break;

        case 'x':
            if (get_range(g_range, optarg, 1, RANGE_MAX) < 0) {
                logging(LOG_ERR, "-x: invalid x-range (%s)", optarg);
                exit(1);
            }
            break;

        case 'y':
            if (get_range(g_range + 1, optarg, 1, RANGE_MAX) < 0) {
                logging(LOG_ERR, "-y: invalid y-range (%s)", optarg);
                exit(1);
            }
            break;
        case 'z':
            if (get_seq_or_range(g_range + 2, &g_zseq,
                                 optarg, 1, RANGE_MAX) < 0) {
                logging(LOG_ERR, "-z: invalid z-layers (%s)", optarg);
                exit(1);
            }
            break;
        case 'h':
        default:
            usage();
            exit(1);
            break;
        }

    argc -= optind;
    argv += optind;
    for (; argc > 0 && *argv; argc--, argv++) {
        if (seq)
            reinitSeq(seq, 1, 0x7fffffff);

        if (ngtdump(*argv, seq) < 0) {
            rval = 1;
            continue;
        }
    }
    return rval;
}
