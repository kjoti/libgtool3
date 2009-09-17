/*
 *  ngthead.c -- display a header info
 */
#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PROGNAME "ngthead"
#define ELEMLEN 16

const char *hlabel[] = {
    "IDFM",
    "DSET",
    "ITEM",
    "EDIT1",
    "EDIT2",
    "EDIT3",
    "EDIT4",
    "EDIT5",
    "EDIT6",
    "EDIT7",
    "EDIT8",
    "FNUM",
    "DNUM",
    "TITL1",
    "TITL2",
    "UNIT",
    "ETTL1",
    "ETTL2",
    "ETTL3",
    "ETTL4",
    "ETTL5",
    "ETTL6",
    "ETTL7",
    "ETTL8",
    "TIME",
    "UTIM",
    "DATE",
    "TDUR",
    "AITM1",
    "ASTR1",
    "AEND1",
    "AITM2",
    "ASTR2",
    "AEND2",
    "AITM3",
    "ASTR3",
    "AEND3",
    "DFMT",
    "MISS",
    "DMIN",
    "DMAX",
    "DIVS",
    "DIVL",
    "STYP",
    "COPTN",
    "IOPTN",
    "ROPTN",
    "DATE1",
    "DATE2",
    "MEMO1",
    "MEMO2",
    "MEMO3",
    "MEMO4",
    "MEMO5",
    "MEMO6",
    "MEMO7",
    "MEMO8",
    "MEMO9",
    "MEMO10",
    "CDATE",
    "CSIGN",
    "MDATE",
    "MSIGN",
    "SIZE"
};

#define ISCNTRL(c) ((c) < 040 || (c) == 0177)

void
copy(char *dest, const char *src)
{
    int i;
    for (i = 0; i < ELEMLEN; i++, src++, dest++)
        *dest = ISCNTRL(*src) ? '#' : *src;
}


int
display(FILE *fp)
{
    char buf[1024 + 4];
    char temp[ELEMLEN + 1], temp2[ELEMLEN + 1];
    int i;

    if (fread(buf, 1, sizeof buf, fp) != sizeof buf)
        return -1;

    temp[ELEMLEN] = '\0';
    temp2[ELEMLEN] = '\0';
    for (i = 0; i < 64 / 2; i++) {
        copy(temp,  buf + 4 + ELEMLEN * i);
        copy(temp2, buf + 4 + ELEMLEN * (i + 32));
        printf("%5d %-7s (%s)  %5d %-7s (%s)\n",
               i + 1, hlabel[i], temp,
               i + 33, hlabel[i + 32], temp2);

    }
    return 0;
}


void
usage(void)
{
    const char *usage_message =
        "Usage: " PROGNAME " file\n"
        "Usage: ngtcat -t N file | " PROGNAME "\n"
        "\n"
        "print header fields.\n\n"
        "Options:\n"
        "    -h        print help message\n";

    fprintf(stderr, "%s\n", usage_message);
}


int
main(int argc, char **argv)
{
    FILE *fp = stdin;
    int ch;

    while ((ch = getopt(argc, argv, "h")) != -1)
        switch (ch) {
        case 'h':
            usage();
            exit(0);
        default:
            usage();
            exit(1);
        }

    argc -= optind;
    argv += optind;

    if (argc > 0 && (fp = fopen(*argv, "rb")) == NULL) {
        perror(*argv);
        exit(1);
    }
    if (display(fp) < 0) {
        fprintf(stderr, "%s: invalid input.\n", PROGNAME);
        exit(1);
    }
    return 0;
}
