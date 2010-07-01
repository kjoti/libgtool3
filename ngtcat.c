/*
 * ngtcat.c -- concatenate gtool files.
 */
#include "internal.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gtool3.h"
#include "seq.h"
#include "fileiter.h"
#include "myutils.h"
#include "logging.h"

#define PROGNAME "ngtcat"

#define FH_SIZE 4
#define URC_PARAMS_SIZE (4 + 4 + 8 + 6 * FH_SIZE)
#define clip(v, l, h) ((v) < (l) ? (l) : ((v) > (h) ? (h) : v))
#ifndef min
#  define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#  define max(a, b) ((a) > (b) ? (a) : (b))
#endif

static int slicing = 0;
static char *zslice_str = NULL;
static int global_xrange[] = { 0, 0x7fffffff };
static int global_yrange[] = { 0, 0x7fffffff };
static FILE *output_stream;


static int
write_header(const GT3_HEADER *head, FILE *fp)
{
    unsigned char siz[] = { 0, 0, 4, 0 };
    unsigned char buf[GT3_HEADER_SIZE + 8];

    memcpy(buf, siz, FH_SIZE);
    memcpy(buf + 4, head->h, GT3_HEADER_SIZE);
    memcpy(buf + 4 + GT3_HEADER_SIZE, siz, FH_SIZE);
    return (fwrite(buf, 1, sizeof buf, fp) != sizeof buf) ? -1 : 0;
}


static int
fcopy(FILE *dest, FILE *src, size_t size)
{
    char buf[BUFSIZ];
    size_t nread;

    while (size > 0) {
        nread = size > sizeof buf ? sizeof buf : size;
        if (fread(buf, 1, nread, src) != nread
            || fwrite(buf, 1, nread, dest) != nread)
            break;
        size -= nread;
    }
    return (size == 0) ? 0 : -1;
}


static int
test_seq(int *first, int *order, struct sequence *seq, int low, int up)
{
    struct sequence temp = *seq;
    int cnt = 0;
    int prev = -1;

    *order = 0;
    while (nextSeq(&temp))
        if (temp.curr >= low && temp.curr <= up) {
            *order |= (cnt > 0 && prev != temp.curr - 1);
            if (cnt == 0)
                *first = temp.curr;

            cnt++;
            prev = temp.curr;
        }
    return cnt;
}


static int
slicecopy2(FILE *dest, GT3_File *fp, size_t esize,
           int xrange[], int yrange[],
           int zstr, int zstep, int znum)
{
    int y, z;
    size_t siz = 0, ssize;
    off_t off, zoff, off0 = 0;


    if (fp->fmt == GT3_FMT_URC || fp->fmt == GT3_FMT_URC1) {
        siz = esize * (xrange[1] - xrange[0]) * (yrange[1] - yrange[0]);
        if (IS_LITTLE_ENDIAN)
            reverse_words(&siz, 1);
        off0 = URC_PARAMS_SIZE + FH_SIZE;
    }

    ssize = esize * (xrange[1] - xrange[0]);
    for (z = zstr; znum > 0; znum--, z += zstep) {
        /*
         * XXX Out-of-range is ignored.
         */
        if (z < 0 || z >= fp->dimlen[2])
            continue;

        if (GT3_skipZ(fp, z) < 0)
            return -1;

        zoff = ftello(fp->fp);

        if (fp->fmt == GT3_FMT_URC || fp->fmt == GT3_FMT_URC1) {
            /* write urc parameters */
            /* & fortran header */
            if (fcopy(dest, fp->fp, URC_PARAMS_SIZE) < 0
                || fwrite(&siz, 1, FH_SIZE, dest) != FH_SIZE)
                return -1;
        }

        for (y = yrange[0]; y < yrange[1]; y++) {
            off = (off_t)fp->dimlen[0] * y + xrange[0];
            off *= esize;
            off += zoff + off0;

            if (fseeko(fp->fp, off, SEEK_SET) < 0
                || fcopy(dest, fp->fp, ssize) < 0)
                return -1;
        }

        if (fp->fmt == GT3_FMT_URC || fp->fmt == GT3_FMT_URC1) {
            /* fortran trailer */
            if (fwrite(&siz, 1, FH_SIZE, dest) != FH_SIZE)
                return -1;
        }
    }
    return 0;
}


static int
slicecopy(FILE *dest, GT3_File *fp)
{
    size_t siz, esize, ssize;
    GT3_HEADER head;
    struct sequence *zseq;
    int xrange[2];
    int yrange[2];
    int xstr0, ystr0, zstr0;
    int zfirst = 0, zorder;
    int xynum, znum, nz;
    int all_flag;


    if (GT3_readHeader(&head, fp) < 0
        || GT3_decodeHeaderInt(&xstr0, &head, "ASTR1") < 0
        || GT3_decodeHeaderInt(&ystr0, &head, "ASTR2") < 0
        || GT3_decodeHeaderInt(&zstr0, &head, "ASTR3") < 0)
        return -1;

    xrange[0] = global_xrange[0];
    yrange[0] = global_yrange[0];
    xrange[1] = min(global_xrange[1], fp->dimlen[0]);
    yrange[1] = min(global_yrange[1], fp->dimlen[1]);

    zseq = initSeq(zslice_str ? zslice_str : ":", 1, fp->dimlen[2]);
    znum = test_seq(&zfirst, &zorder, zseq, 1, fp->dimlen[2]);

    if (znum <= 0
        || xrange[1] - xrange[0] <= 0
        || yrange[1] - yrange[0] <= 0 ) {
        logging(LOG_ERR, "No data in specified domain");
        return -1; /* NO data to copy */
    }
    switch (fp->fmt) {
    case GT3_FMT_UR4:
        esize = 4;
        break;
    case GT3_FMT_URC:
    case GT3_FMT_URC1:
        esize = 2;
        break;
    case GT3_FMT_UR8:
        esize = 8;
        break;
    default:
        /* NOTREACHED */
        esize = 4;
        break;
    }

    xynum = (xrange[1] - xrange[0]) * (yrange[1] - yrange[0]);
    all_flag = xrange[1] - xrange[0] == fp->dimlen[0]
            && yrange[1] - yrange[0] == fp->dimlen[1];

    /*
     * modify header field.
     */
    GT3_setHeaderInt(&head, "ASTR1", xrange[0] + xstr0);
    GT3_setHeaderInt(&head, "AEND1", xrange[1] + xstr0 - 1);
    GT3_setHeaderInt(&head, "ASTR2", yrange[0] + ystr0);
    GT3_setHeaderInt(&head, "AEND2", yrange[1] + ystr0 - 1);
    GT3_setHeaderInt(&head, "SIZE",  xynum * znum);
    if (zorder) {
        /*
         * In this case, the z-axis name cannot remain the same.
         */
        zstr0 = 1;
        GT3_setHeaderString(&head, "AITM3", "NUMBER1000");
    } else
        zstr0 += zfirst - 1;
    GT3_setHeaderInt(&head, "ASTR3", zstr0);
    GT3_setHeaderInt(&head, "AEND3", zstr0 + znum - 1);

    /* write header */
    if (write_header(&head, dest) < 0)
        return -1;

    /*
     * For UR4 or UR8, write Fortran header.
     */
    if (fp->fmt == GT3_FMT_UR4 || fp->fmt == GT3_FMT_UR8) {
        siz = esize * xynum * znum;
        if (IS_LITTLE_ENDIAN)
            reverse_words(&siz, 1);

        if (fwrite(&siz, 1, FH_SIZE, dest) != FH_SIZE)
            return -1;
    }

    /*
     * iterate z-slice.
     */
    reinitSeq(zseq, zseq->first, zseq->last);
    while (nextToken(zseq)) {
        nz = (zseq->step == 0)
            ? 1
            : (zseq->tail - zseq->head + zseq->step) / zseq->step;

        if (all_flag && zseq->step == 1) {
            /*
             * Simple slicing:
             * In this case, data in this slice are contiguous.
             */
            int zpos;

            /*
             * z-index check.
             */
            zpos = clip(zseq->head, 1, fp->dimlen[2]) - 1;
            nz   = clip(zseq->tail, 1, fp->dimlen[2]) - zpos;
            if (nz <= 0)
                continue;

            ssize = esize * xynum * nz;

            if (fp->fmt == GT3_FMT_URC || fp->fmt == GT3_FMT_URC1)
                ssize += (URC_PARAMS_SIZE + 2 * FH_SIZE) * nz;

            if (GT3_skipZ(fp, zpos) < 0 || fcopy(dest, fp->fp, ssize) < 0)
                return -1;
        } else if (all_flag) {
            int z;

            ssize = esize * xynum;
            if (fp->fmt == GT3_FMT_URC || fp->fmt == GT3_FMT_URC1)
                ssize += URC_PARAMS_SIZE + 2 * FH_SIZE;

            for (z = zseq->head - 1; nz-- > 0; z += zseq->step) {
                if (z < 0 || z >= fp->dimlen[2])
                    continue;

                if (GT3_skipZ(fp, z) < 0 || fcopy(dest, fp->fp, ssize) < 0)
                    return -1;
            }
        } else {
            /* more detailed slicing */
            if (slicecopy2(dest, fp, esize,
                           xrange, yrange,
                           zseq->head - 1, zseq->step, nz) < 0)
                return -1;
        }
    }

    /*
     * For UR4 or UR8, write Fortran trailer.
     */
    if (fp->fmt == GT3_FMT_UR4 || fp->fmt == GT3_FMT_UR8)
        if (fwrite(&siz, 1, FH_SIZE, dest) != FH_SIZE)
            return -1;

    freeSeq(zseq);
    free(zseq);
    return 0;
}


static int
mcopy(FILE *dest, GT3_File *fp)
{
    if (slicing) {
        int support_slice[] = {
            GT3_FMT_UR4,
            GT3_FMT_URC,
            GT3_FMT_URC1,
            GT3_FMT_UR8
        };
        int i;

        for (i = 0; i < sizeof support_slice / sizeof(int); i++)
            if (fp->fmt == support_slice[i])
                return slicecopy(dest, fp);

        logging(LOG_ERR, "Slicing unsupported in this format");
        return -1;
    } else
        return fcopy(dest, fp->fp, fp->chsize);
}


static int
set_range(int range[], const char *str)
{
    int nf;

    if ((nf = get_ints(range, 2, str, ':')) < 0)
        return -1;

    /*
     * XXX
     * transform
     *   FROM  1-offset and closed bound    [X,Y] => do i = X, Y.
     *   TO    0-offset and semi-open bound [X,Y) => for (i = X; i < Y; i++).
     */
    range[0]--;
    if (range[0] < 0)
        range[0] = 0;
    if (nf == 1)
        range[1] = range[0] + 1;
    return 0;
}


int
gtcat_cyclic(int num, char *path[], struct sequence *seq)
{
    GT3_File *fp;
    int i, last, stat, errflag;

    if (num < 1)
        return 0;

    if ((last = GT3_countChunk(path[0])) < 0) {
        GT3_printErrorMessages(stderr);
        return -1;
    }
    reinitSeq(seq, 1, last);

    errflag = 0;
    while (nextSeq(seq) == 1 && errflag == 0) {
        for (i = 0; i < num && errflag == 0; i++) {
            if ((fp = GT3_open(path[i])) == NULL) {
                GT3_printErrorMessages(stderr);
                return -1;
            }

            stat = seq->curr > 0
                ? GT3_seek(fp, seq->curr - 1, SEEK_SET)
                : GT3_seek(fp, seq->curr, SEEK_END);

            if (stat == 0) {
                if (mcopy(output_stream, fp) < 0) {
                    GT3_printErrorMessages(stderr);
                    logging(LOG_SYSERR, NULL);
                    errflag = 1;
                }
            } else if (stat != GT3_ERR_INDEX)
                errflag = 1;

            /* GT3_ERR_INDEX (out of range) is ignored. */

            GT3_close(fp);
        }
    }
    return errflag ? -1 : 0;
}


int
gtcat(const char *path, struct sequence *seq)
{
    GT3_File *fp;
    int stat, rval;

    if ((fp = GT3_open(path)) == NULL) {
        GT3_printErrorMessages(stderr);
        return -1;
    }

    rval = 0;
    while ((stat = iterate_chunk(fp, seq)) != ITER_END) {
        if (stat == ITER_OUTRANGE)
            continue;

        if (stat == ITER_ERROR) {
            logging(LOG_ERR, "%s: Invalid -t argument", seq->it);
            break;
        }

        if (stat == ITER_ERRORCHUNK) {
            rval = -1;
            break;
        }

        if (mcopy(output_stream, fp) < 0) {
            GT3_printErrorMessages(stderr);
            logging(LOG_SYSERR, NULL);
            rval = -1;
            break;
        }
    }

    GT3_close(fp);
    return rval;
}


void
usage(void)
{
    const char *usage_message =
        "Usage: " PROGNAME " [options] [files...]\n"
        "\n"
        "Concatenates and/or slices data (output into stdout).\n"
        "\n"
        "Options:\n"
        "    -h        print help message\n"
        "    -c        cyclic mode\n"
        "    -o PATH   specify output filename (default: output into stdout)\n"
        "    -x RANGE  specify X-range\n"
        "    -y RANGE  specify Y-range\n"
        "    -z LIST   specify Z-planes\n"
        "    -t LIST   specify data No.\n"
        "\n"
        "    LIST   := RANGE[,RANGE]*\n"
        "    RANGE  := start[:[end]] | :[end]\n";

    fprintf(stderr, "%s\n", GT3_version());
    fprintf(stderr, "%s\n", usage_message);
}


int
main(int argc, char **argv)
{
    struct sequence *seq = NULL;
    int cyclic = 0;
    int ch, rval = 0;

    output_stream = stdout;

    open_logging(stderr, PROGNAME);
    GT3_setProgname(PROGNAME);
    while ((ch = getopt(argc, argv, "cho:t:x:y:z:")) != -1)
        switch (ch) {
        case 'c':
            cyclic = 1;
            break;

        case 'o':
            if ((output_stream = fopen(optarg, "wb")) == NULL) {
                logging(LOG_SYSERR, optarg);
                exit(1);
            }
            break;

        case 't':
            seq = initSeq(optarg, 1, 0x7fffffff);
            break;

        case 'x':
            if (set_range(global_xrange, optarg) < 0) {
                logging(LOG_ERR, "%s: Invalid argument", optarg);
                exit(1);
            }
            slicing = 1;
            break;

        case 'y':
            if (set_range(global_yrange, optarg) < 0) {
                logging(LOG_ERR, "%s: Invalid argument", optarg);
                exit(1);
            }
            slicing = 1;
            break;

        case 'z':
            zslice_str = strdup(optarg);
            slicing = 1;
            break;

        case 'h':
        default:
            usage();
            exit(1);
            break;
        }

    if (!seq)
        seq = initSeq(":", 1, 0x7fffffff);

    argc -= optind;
    argv += optind;

    if (cyclic) {
        if (gtcat_cyclic(argc, argv, seq) < 0)
            rval = 1;
    } else
        for (; argc > 0 && *argv; argc--, argv++) {
            if (gtcat(*argv, seq) < 0)
                rval = 1;

            reinitSeq(seq, 1, 0x7fffffff);
        }

    return rval;
}
