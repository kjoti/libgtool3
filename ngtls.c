/*
 * ngtls.c -- list gtool3 file (like 'gtshow -ls').
 */
#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gtool3.h"
#include "seq.h"
#include "fileiter.h"


static int (*print_item)(int cnt, GT3_File *fp);


void
print_error(int cnt)
{
    printf("%4d **** BROKEN CHUNK *****\n", cnt);
}


int
print_item1(int cnt, GT3_File *fp)
{
    GT3_HEADER head;
    char item[17];
    char time[17];
    char utim[17];
    char tdur[17];
    char date[17];
    char dim1[17];
    char dim2[17];
    char dim3[17];
    char dfmt[17];

    if (GT3_readHeader(&head, fp) < 0)
        return -1;

    (void)GT3_copyHeaderItem(item, sizeof item, &head, "ITEM");
    (void)GT3_copyHeaderItem(time, sizeof time, &head, "TIME");
    (void)GT3_copyHeaderItem(utim, sizeof utim, &head, "UTIM");
    (void)GT3_copyHeaderItem(tdur, sizeof tdur, &head, "TDUR");
    (void)GT3_copyHeaderItem(date, sizeof date, &head, "DATE");
    (void)GT3_copyHeaderItem(dim1, sizeof dim1, &head, "AITM1");
    (void)GT3_copyHeaderItem(dim2, sizeof dim2, &head, "AITM2");
    (void)GT3_copyHeaderItem(dim3, sizeof dim3, &head, "AITM3");
    (void)GT3_copyHeaderItem(dfmt, sizeof dfmt, &head, "DFMT");

    if (utim[0] == '\0')
        utim[0] = '?';

    printf("%4d %-8s %8s%c %5s %5s %15s %s,%s,%s\n",
           cnt, item, time, utim[0], tdur, dfmt, date, dim1, dim2, dim3);
    return 0;
}


int
print_item2(int cnt, GT3_File *fp)
{
    const char *astr[] = { "ASTR1", "ASTR2", "ASTR3" };
    const char *aend[] = { "AEND1", "AEND2", "AEND3" };
    GT3_HEADER head;
    char item[17];
    char time[17];
    char utim[17];
    char tdur[17];
    char date[17];
    char dfmt[17];
    char dim[3][12];
    int i, str, end;

    if (GT3_readHeader(&head, fp) < 0)
        return -1;

    (void)GT3_copyHeaderItem(item, sizeof item, &head, "ITEM");
    (void)GT3_copyHeaderItem(time, sizeof time, &head, "TIME");
    (void)GT3_copyHeaderItem(utim, sizeof utim, &head, "UTIM");
    (void)GT3_copyHeaderItem(tdur, sizeof tdur, &head, "TDUR");
    (void)GT3_copyHeaderItem(date, sizeof date, &head, "DATE");
    (void)GT3_copyHeaderItem(dfmt, sizeof dfmt, &head, "DFMT");

    for (i = 0; i < 3; i++) {
        (void)GT3_decodeHeaderInt(&str, &head, astr[i]);
        (void)GT3_decodeHeaderInt(&end, &head, aend[i]);
        snprintf(dim[i], sizeof dim[i], "%d:%d", str, end);
    }

    if (utim[0] == '\0')
        utim[0] = '?';

    printf("%4d %-8s %8s%c %5s %5s %15s  %-8s %-8s %-8s\n",
           cnt, item, time, utim[0], tdur, dfmt, date,
           dim[0], dim[1], dim[2]);
    return 0;
}


int
print_item3(int cnt, GT3_File *fp)
{
    GT3_HEADER head;
    char item[17];
    char title[33];
    char unit[17];

    if (GT3_readHeader(&head, fp) < 0)
        return -1;

    (void)GT3_copyHeaderItem(item, sizeof item, &head, "ITEM");
    (void)GT3_copyHeaderItem(title, sizeof title, &head, "TITLE");
    (void)GT3_copyHeaderItem(unit, sizeof unit, &head, "UNIT");
    printf("%4d %-16s (%-32s) [%-13s]\n", cnt, item, title, unit);
    return 0;
}


int
print_list(const char *path, struct sequence *seq, int name_flag)
{
    GT3_File *fp;
    int stat, rval = 0;
    file_iterator it;

    if ((fp = GT3_open(path)) == NULL) {
        GT3_printErrorMessages(stderr);
        return -1;
    }
    if (name_flag)
        printf("# Filename: %s\n", path);

    setup_file_iterator(&it, fp, seq);
    while ((stat = iterate_chunk2(&it)) != ITER_END) {
        if (stat == ITER_ERROR) {
            rval = -1;
            break;
        }
        if (stat == ITER_OUTRANGE)
            continue;

        if (stat == ITER_ERRORCHUNK) {
            print_error(fp->curr + 1);
            rval = -1;
            break;
        }

        if ((*print_item)(fp->curr + 1, fp) < 0) {
            print_error(fp->curr + 1);
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
        "Usage: ngtls [options] [files...]\n"
        "\n"
        "Options:\n"
        "    -h          print help message\n"
        "    -n          print axis-length instead of axis-name\n"
        "    -u          print title and unit\n"
        "    -v          print filename\n"
        "    -t LIST     specify data No.\n";

    fprintf(stderr, "%s\n", GT3_version());
    fprintf(stderr, "%s\n", usage_message);
}


int
main(int argc, char **argv)
{
    int ch, rval;
    int name_flag = 0;
    struct sequence *seq = NULL;

    print_item = print_item1;

    while ((ch = getopt(argc, argv, "nht:uv")) != -1)
        switch (ch) {
        case 'n':
            print_item = print_item2;
            break;

        case 't':
            seq = initSeq(optarg, 1, 0x7ffffff);
            break;

        case 'u':
            print_item = print_item3;
            break;

        case 'v':
            name_flag = 1;
            break;

        case 'h':
        default:
            usage();
            exit(1);
            break;
        }

    argc -= optind;
    argv += optind;
    GT3_setProgname("ngtls");

    rval = 0;
    while (argc > 0 && *argv) {
        if (seq)
            reinitSeq(seq, 1, 0x7fffffff);
        if (print_list(*argv, seq, name_flag) < 0)
            rval = 1;

        --argc;
        ++argv;
    }
    return rval;
}
