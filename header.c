/*
 * header.c
 */
#include "internal.h"

#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "gtool3.h"
#include "debug.h"

#define ELEM_SZ  16             /* size of each element (in byte) */
#define NUM_ELEM 64

#define ISCNTRL(c) ((c) < 040 || (c) == 0177)
#ifndef min
#  define min(a,b) ((a) < (b) ? (a) : (b))
#endif

/*
 * Gtool3 header items.
 */
#define G_(X) X
enum {
    G_(IDFM), G_(DSET), G_(ITEM),
    G_(FNUM) = 11, G_(DNUM), G_(TITL1),
    G_(UNIT) = 15,

    G_(TIME) = 24, G_(UTIM), G_(DATE), G_(TDUR),
    G_(AITM1), G_(ASTR1), G_(AEND1),
    G_(AITM2), G_(ASTR2), G_(AEND2),
    G_(AITM3), G_(ASTR3), G_(AEND3),
    G_(DFMT), G_(MISS),
    G_(DMIN), G_(DMAX), G_(DIVS), G_(DIVL), G_(STYP),
    G_(COPTN), G_(IOPTN), G_(ROPTN),
    G_(DATE1), G_(DATE2),

    G_(CDATE) = 59, G_(CSIGN), G_(MDATE), G_(MSIGN), G_(SIZE)
};


/* Elem Type */
enum {
    IT_STR,                     /* 16-char */
    IT_STR2,                    /* 32-char */
    IT_INT,
    IT_FLOAT
};

/*
 * Dictionary of elements in the gtool-header.
 */
struct ElemDict {
    const char *name;
    int id;
    int type;
    char *default_value;
};

#define is_blank(buf) is_blank2((buf), 16)

#define cpZERO  "               0"
#define cpONE   "               1"
#define cpMISS  "  -9.9900000E+02"
#define DATE_FORMAT "%0*d%02d%02d %02d%02d%02d "

/* XXX This MUST be sorted by its name. */
static struct ElemDict elemdict[] = {
    { "AEND1",  30, IT_INT,   NULL               },
    { "AEND2",  33, IT_INT,   NULL               },
    { "AEND3",  36, IT_INT,   NULL               },
    { "AITM1",  28, IT_STR,   NULL               },
    { "AITM2",  31, IT_STR,   NULL               },
    { "AITM3",  34, IT_STR,   NULL               },
    { "ASTR1",  29, IT_INT,   cpONE              },
    { "ASTR2",  32, IT_INT,   cpONE              },
    { "ASTR3",  35, IT_INT,   cpONE              },
    { "CDATE",  59, IT_STR,   NULL               },
    { "COPTN",  44, IT_STR,   NULL               },
    { "CSIGN",  60, IT_STR,   NULL               },
    { "DATE",   26, IT_STR,   NULL               },
    { "DATE1",  47, IT_STR,   NULL               },
    { "DATE2",  48, IT_STR,   NULL               },
    { "DFMT",   37, IT_STR,   "UR4             " },
    { "DIVL",   42, IT_FLOAT, cpMISS             },
    { "DIVS",   41, IT_FLOAT, cpMISS             },
    { "DMAX",   40, IT_FLOAT, cpMISS             },
    { "DMIN",   39, IT_FLOAT, cpMISS             },
    { "DNUM",   12, IT_INT,   cpZERO             },
    { "DSET",   1,  IT_STR,   NULL               },
    { "EDIT1",  3,  IT_STR,   NULL               },
    { "EDIT2",  4,  IT_STR,   NULL               },
    { "EDIT3",  5,  IT_STR,   NULL               },
    { "EDIT4",  6,  IT_STR,   NULL               },
    { "EDIT5",  7,  IT_STR,   NULL               },
    { "EDIT6",  8,  IT_STR,   NULL               },
    { "EDIT7",  9,  IT_STR,   NULL               },
    { "EDIT8",  10, IT_STR,   NULL               },
    { "ETTL1",  16, IT_STR,   NULL               },
    { "ETTL2",  17, IT_STR,   NULL               },
    { "ETTL3",  18, IT_STR,   NULL               },
    { "ETTL4",  19, IT_STR,   NULL               },
    { "ETTL5",  20, IT_STR,   NULL               },
    { "ETTL6",  21, IT_STR,   NULL               },
    { "ETTL7",  22, IT_STR,   NULL               },
    { "ETTL8",  23, IT_STR,   NULL               },
    { "FNUM",   11, IT_INT,   cpZERO             },
    { "IDFM",   0,  IT_INT,   NULL               },
    { "IOPTN",  45, IT_INT,   cpZERO             },
    { "ITEM",   2,  IT_STR,   NULL               },
    { "MDATE",  61, IT_STR,   NULL               },
    { "MEMO1",  49, IT_STR,   NULL               },
    { "MEMO10", 58, IT_STR,   NULL               },
    { "MEMO2",  50, IT_STR,   NULL               },
    { "MEMO3",  51, IT_STR,   NULL               },
    { "MEMO4",  52, IT_STR,   NULL               },
    { "MEMO5",  53, IT_STR,   NULL               },
    { "MEMO6",  54, IT_STR,   NULL               },
    { "MEMO7",  55, IT_STR,   NULL               },
    { "MEMO8",  56, IT_STR,   NULL               },
    { "MEMO9",  57, IT_STR,   NULL               },
    { "MISS",   38, IT_FLOAT, cpMISS             },
    { "MSIGN",  62, IT_STR,   NULL               },
    { "ROPTN",  46, IT_FLOAT, "   0.0000000E+00" },
    { "SIZE",   63, IT_INT,   cpZERO             },
    { "STYP",   43, IT_INT,   cpONE              },
    { "TDUR",   27, IT_INT,   cpZERO             },
    { "TIME",   24, IT_INT,   cpZERO             },
    { "TITL1",  13, IT_STR,   NULL               },
    { "TITL2",  14, IT_STR,   NULL               },
    { "TITLE",  13, IT_STR2,  NULL               }, /* alias */
    { "UNIT",   15, IT_STR,   NULL               },
    { "UTIM",   25, IT_STR,   NULL               }
};


static int
elemnamecmp(const void *key, const void *p)
{
    return strcmp((const char *)key, ((struct ElemDict *)p)->name);
}


static struct ElemDict *
lookup_name(const char *key)
{
    return bsearch(key, elemdict,
                   sizeof elemdict / sizeof(struct ElemDict),
                   sizeof(struct ElemDict),
                   elemnamecmp);
}


static int
is_blank2(const char *buf, size_t len)
{
    const char *blank = "                                ";

    return memcmp(buf, blank, len) == 0;
}


char *
GT3_copyHeaderItem(char *buf, size_t buflen, const GT3_HEADER *header,
                   const char *key)
{
    const char *strp, *last;
    char *q;
    struct ElemDict *p;

    if (buflen == 0)
        return NULL;

    p = lookup_name(key);
    if (p == NULL) {
        gt3_error(GT3_ERR_CALL, "%s: Unknown header item", key);
        return NULL;
    }

    strp = header->h + ELEM_SZ * p->id;
    if (p->default_value && is_blank(strp))
        strp = p->default_value;

    last = strp + (p->type == IT_STR2 ? 2 : 1) * ELEM_SZ;

    /* skip leading white spaces */
    while (isspace(*strp) && strp < last)
        strp++;

    /* trim trailing white spaces */
    while (strp < last && isspace(*(last - 1)))
        last--;

    --buflen;                   /* for a null terminator */
    if (buflen < last - strp)
        last = strp + buflen;

    for (q = buf; strp < last; strp++)
        *q++ = ISCNTRL(*strp) ? '#' : *strp;
    *q = '\0';

    return buf;
}


int
GT3_decodeHeaderInt(int *rval, const GT3_HEADER *header, const char *key)
{
    const char *strp = header->h;
    int ival;
    char buf[ELEM_SZ + 1];
    char *endptr;
    struct ElemDict *p;

    p = lookup_name(key);
    if (p == NULL) {
        gt3_error(GT3_ERR_CALL, "%s: Unknown header item", key);
        return -1;
    }
    if (p->type != IT_INT) {
        gt3_error(GT3_ERR_CALL, "%s: Not an integer item", key);
        return -1;
    }

    strp += ELEM_SZ * p->id;
    if (p->default_value && is_blank(strp))
        strp = p->default_value;

    memcpy(buf, strp, ELEM_SZ);
    buf[ELEM_SZ] = '\0';
    ival = (int)strtol(buf, &endptr, 10);
    if (endptr == buf) {
        gt3_error(GT3_ERR_HEADER, "%s: %s", key, buf);
        return -1;
    }
    *rval = ival;
    return 0;                   /* ok */
}


int
GT3_decodeHeaderDouble(double *rval, const GT3_HEADER *header, const char *key)
{
    const char *strp = header->h;
    double val;
    char buf[ELEM_SZ + 1];
    char *endptr;
    struct ElemDict *p;

    p = lookup_name(key);
    if (p == NULL) {
        gt3_error(GT3_ERR_CALL, "%s: Unknown header item", key);
        return -1;
    }
    if (p->type != IT_FLOAT) {
        gt3_error(GT3_ERR_CALL, "%s: Not an float item", key);
        return -1;
    }

    strp += ELEM_SZ * p->id;
    if (p->default_value && is_blank(strp))
        strp = p->default_value;

    memcpy(buf, strp, ELEM_SZ);
    buf[ELEM_SZ] = '\0';
    val = strtod(buf, &endptr);
    if (endptr == buf) {
        gt3_error(GT3_ERR_HEADER, buf);
        return -1;
    }
    *rval = val;
    return 0;
}


int
GT3_decodeHeaderDate(GT3_Date *date, const GT3_HEADER *header,
                    const char *key)
{
    const char *strp = header->h;
    struct ElemDict *p;
    int val[6] = { 0, 1, 1, 0, 0, 0 };
    int num, year_width;
    char buf[ELEM_SZ + 1];
    char datefmt[] = DATE_FORMAT;

    p = lookup_name(key);
    if (p == NULL) {
        gt3_error(GT3_ERR_CALL, "%s: Unknown header item", key);
        return -1;
    }

    strp += ELEM_SZ * p->id;
    if (is_blank(strp)) {
        gt3_error(GT3_ERR_HEADER, "%s: Empty field", key);
        return -1;
    }

    memcpy(buf, strp, ELEM_SZ);
    buf[ELEM_SZ] = '\0';

    year_width = (buf[9] == ' ' && buf[15] != ' ') ? 5 : 4;
    datefmt[2] = year_width == 5 ? '5' : '4';

    num = sscanf(buf, datefmt,
                 val, val + 1, val + 2,
                 val + 3, val + 4, val + 5);
    if (num != 6) {
        gt3_error(GT3_ERR_CALL, "%s: Invalid DATE field.\n", buf);
        return -1;
    }
    date->year = val[0];
    date->mon  = val[1];
    date->day  = val[2];
    date->hour = val[3];
    date->min  = val[4];
    date->sec  = val[5];
    return 0;
}


int
GT3_decodeHeaderTunit(const GT3_HEADER *header)
{
    struct { const char *key; size_t len; int val; } tab[] = {
        { "HOUR", 4, GT3_UNIT_HOUR },
        { "DAY",  3, GT3_UNIT_DAY  },
        { "MIN",  3, GT3_UNIT_MIN  },
        { "SEC",  3, GT3_UNIT_SEC  }
    };
    int unit, i;
    const char *p;

    p = header->h + ELEM_SZ * UTIM;
    unit = -1;
    for (i = 0; i < sizeof tab / sizeof(tab[0]); i++)
        if (strncmp(p, tab[i].key, tab[i].len) == 0) {
            unit = tab[i].val;
            break;
        }

    if (unit == -1) {
        char hbuf[17];

        GT3_copyHeaderItem(hbuf, sizeof hbuf, header, "UTIM");
        gt3_error(GT3_ERR_HEADER, "%s: Unknown time-unit", hbuf);
    }
    return unit;
}


void
GT3_initHeader(GT3_HEADER *header)
{
    const char *magic = "            9010";
    int i, id;
    char *hp = header->h;

    memset(hp, ' ', GT3_HEADER_SIZE);

    for (i = 0; i < sizeof elemdict / sizeof(struct ElemDict); i++) {
        if (elemdict[i].default_value) {
            id = elemdict[i].id;

            memcpy(hp + ELEM_SZ * id,
                   elemdict[i].default_value,
                   ELEM_SZ);
        }
    }
    memcpy(hp, magic, ELEM_SZ);
}


void
GT3_setHeaderString(GT3_HEADER *header, const char *key, const char *str)
{
    int siz;
    struct ElemDict *p;
    char *h;

    if (!str)
        return;

    p = lookup_name(key);
    if (p == NULL) {
        gt3_error(GT3_ERR_CALL, "Unknown header name: %s", key);
        return;
    }

    siz = (p->type == IT_STR2) ? 2 : 1;
    siz *= ELEM_SZ;
    h = header->h + ELEM_SZ * p->id;

    memset(h, ' ', siz);
    memcpy(h, str, min(strlen(str), siz));
}


int
GT3_setHeaderInt(GT3_HEADER *header, const char *key, int val)
{
    char buf[ELEM_SZ + 1];
    struct ElemDict *p;

    p = lookup_name(key);
    if (p == NULL || p->type != IT_INT) {
        gt3_error(GT3_ERR_CALL, "GT3_setHeaderInt(%s)", key);
        return -1;
    }
    snprintf(buf, sizeof buf, "%16d", val);
    memcpy(header->h + ELEM_SZ * p->id, buf, ELEM_SZ);

    return 0;
}


int
GT3_setHeaderDouble(GT3_HEADER *header, const char *key, double val)
{
    char buf[ELEM_SZ + 1];
    struct ElemDict *p;

    p = lookup_name(key);
    if (p == NULL || p->type != IT_FLOAT) {
        return -1;
    }
    snprintf(buf, sizeof buf, "%16.7E", val);
    memcpy(header->h + ELEM_SZ * p->id, buf, ELEM_SZ);

    return 0;
}


int
GT3_setHeaderDate(GT3_HEADER *header, const char *key, const GT3_Date *date)
{
    char buf[ELEM_SZ + 1];
    struct ElemDict *p;
    int year_width;

    p = lookup_name(key);
    if (p == NULL || p->type != IT_STR) {
        return -1;
    }
    year_width = date->year > 9999 ? 5 : 4;
    snprintf(buf, sizeof buf, DATE_FORMAT,
             year_width,
             date->year, date->mon, date->day,
             date->hour, date->min, date->sec);
    memcpy(header->h + ELEM_SZ * p->id, buf, ELEM_SZ);
    return 0;
}


/* last in last out */
static void
edit_header_lilo(GT3_HEADER *head, int pos, int count, const char *str)
{
    char *p;

    p = head->h + ELEM_SZ * pos;
    memmove(p + ELEM_SZ, p, ELEM_SZ * (count - 1));
    memset(p, ' ', ELEM_SZ);
    memcpy(p, str, min(strlen(str), ELEM_SZ));
}


void
GT3_setHeaderEdit(GT3_HEADER *head, const char *str)
{
    edit_header_lilo(head, 3, 8, str);
}

void
GT3_setHeaderEttl(GT3_HEADER *head, const char *str)
{
    edit_header_lilo(head, 16, 8, str);
}

void
GT3_setHeaderMemo(GT3_HEADER *head, const char *str)
{
    edit_header_lilo(head, 49, 10, str);
}


void
GT3_mergeHeader(GT3_HEADER *dest, const GT3_HEADER *src)
{
    int id, len;
    char *q;

    for (id = 0; id < NUM_ELEM; id++) {
        /*
         * special treatment for "TITLE".
         */
        if (id == TITL1 + 1)
            continue;
        len = id == TITL1 ? 2 * ELEM_SZ : ELEM_SZ;

        q = dest->h + id * ELEM_SZ;
        if (is_blank2(q, len))
            memcpy(q, src->h + id * ELEM_SZ, len);
    }
}


void
GT3_copyHeader(GT3_HEADER *dest, const GT3_HEADER *src)
{
    memcpy(dest->h, src->h, GT3_HEADER_SIZE);
}


int
GT3_getHeaderItemID(const char *name)
{
    struct ElemDict *p;

    p = lookup_name(name);
    return p ? p->id : -1;
}


#ifdef TEST_MAIN
void
print_header(const char *header)
{
    int i;

    for (i = 0; i < GT3_HEADER_SIZE; i++) {
        if (i > 0 && (i % ELEM_SZ) == 0)
            putchar('\n');
        putchar(header[i]);
    }
    putchar('\n');
}


int
main(int argc, char **argv)
{
    {
        int i;
        struct ElemDict *p;
        int dictlen = sizeof elemdict / sizeof(struct ElemDict);

        for (i = 0; i < dictlen; i++) {
            p = lookup_name(elemdict[i].name);

            assert(p  && strcmp(p->name, elemdict[i].name) == 0);
        }

        assert(GT3_getHeaderItemID("IDFM") == 0);
        assert(GT3_getHeaderItemID("TITLE") == 13);
        assert(GT3_getHeaderItemID("SIZE") == 63);
        assert(GT3_getHeaderItemID("IDFMX") == -1);
        for (i = 0; i < dictlen; i++)
            assert(GT3_getHeaderItemID(elemdict[i].name) == elemdict[i].id);
    }

    {
        GT3_HEADER header;
        GT3_Date date;
        char buf[33], *p;
        int rval;

        GT3_initHeader(&header);

        p = GT3_copyHeaderItem(buf, sizeof buf, &header, "AITM1");
        assert(p && buf[0] == '\0');

        GT3_setHeaderString(&header, "ITEM", "GLTS");
        p = GT3_copyHeaderItem(buf, sizeof buf, &header, "ITEM");
        assert(p && strcmp(buf, "GLTS") == 0);

        p = GT3_copyHeaderItem(buf, 2, &header, "ITEM");
        assert(p && strcmp(buf, "G") == 0);

        p = GT3_copyHeaderItem(buf, 1, &header, "ITEM");
        assert(p && buf[0] == '\0');

        GT3_setHeaderString(&header, "DSET", "0123456789ABCDEFGHI");
        p = GT3_copyHeaderItem(buf, sizeof buf, &header, "DSET");
        assert(p && strcmp(buf, "0123456789ABCDEF") == 0);

        GT3_setHeaderString(&header, "TITLE", "Surface Air Temperature");
        p = GT3_copyHeaderItem(buf, sizeof buf, &header, "TITLE");
        assert(p && strcmp(buf, "Surface Air Temperature") == 0);

        p = GT3_copyHeaderItem(buf, sizeof buf, &header, "DFMT");
        assert(p && strcmp(buf, "UR4") == 0);

        GT3_setHeaderInt(&header, "AEND1", 320);
        p = GT3_copyHeaderItem(buf, sizeof buf, &header, "AEND1");
        assert(p && strcmp(p, "320") == 0 && strcmp(buf, "320") == 0);

        GT3_setHeaderString(&header, "DATE", "20380119 031407");
        rval = GT3_decodeHeaderDate(&date, &header, "DATE");
        assert(rval == 0);
        assert(date.year == 2038 && date.mon == 1 && date.day == 19
              && date.hour == 3 && date.min == 14 && date.sec == 7);

        date.year = 10;
        rval = GT3_setHeaderDate(&header, "DATE2", &date);
        assert(rval == 0);
        p = GT3_copyHeaderItem(buf, sizeof buf, &header, "DATE2");
        assert(p && strcmp(buf, "00100119 031407") == 0);

        date.year = 40010;
        rval = GT3_setHeaderDate(&header, "DATE2", &date);
        assert(rval == 0);
        p = GT3_copyHeaderItem(buf, sizeof buf, &header, "DATE2");
        assert(p && strcmp(buf, "400100119 031407") == 0);
    }

    {
        GT3_HEADER head1, head2;
        char buf[33], *p;

        GT3_initHeader(&head1);
        GT3_initHeader(&head2);

        GT3_setHeaderString(&head1, "TITLE",
                            "................................");
        GT3_setHeaderString(&head1, "DSET", "CNTL");

        GT3_setHeaderString(&head2, "TITLE", "Air Temperature");
        GT3_mergeHeader(&head2, &head1);

        p = GT3_copyHeaderItem(buf, sizeof buf, &head2, "TITLE");
        assert(p && strcmp(buf, "Air Temperature") == 0);

        p = GT3_copyHeaderItem(buf, sizeof buf, &head2, "DSET");
        assert(p && strcmp(buf, "CNTL") == 0);
    }

    {
        GT3_HEADER head;
        char buf[17];

        GT3_initHeader(&head);

        GT3_setHeaderString(&head, "DATE2", "20000101 000000");
        GT3_setHeaderString(&head, "CDATE", "19991231 235959");

        GT3_setHeaderMemo(&head, "one");
        GT3_setHeaderMemo(&head, "two");
        GT3_setHeaderMemo(&head, "thre");
        GT3_setHeaderMemo(&head, "four");
        GT3_setHeaderMemo(&head, "five");
        GT3_setHeaderMemo(&head, "six");
        GT3_setHeaderMemo(&head, "seven");
        GT3_setHeaderMemo(&head, "eight");
        GT3_setHeaderMemo(&head, "nine");
        GT3_setHeaderMemo(&head, "ten");
        GT3_setHeaderMemo(&head, "eleven");

        GT3_copyHeaderItem(buf, sizeof buf, &head, "DATE2");
        assert(strcmp(buf, "20000101 000000") == 0);

        GT3_copyHeaderItem(buf, sizeof buf, &head, "CDATE");
        assert(strcmp(buf, "19991231 235959") == 0);

        GT3_copyHeaderItem(buf, sizeof buf, &head, "MEMO1");
        assert(strcmp(buf, "eleven") == 0);
        GT3_copyHeaderItem(buf, sizeof buf, &head, "MEMO2");
        assert(strcmp(buf, "ten") == 0);

        GT3_copyHeaderItem(buf, sizeof buf, &head, "MEMO9");
        assert(strcmp(buf, "thre") == 0);
        GT3_copyHeaderItem(buf, sizeof buf, &head, "MEMO10");
        assert(strcmp(buf, "two") == 0);
    }
    return 0;
}
#endif
