/*
 * ghprintf.c - printf-like function with GT3HEADER.
 */
#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>

#include "logging.h"
#include "ghprintf.h"

struct format_element {
    int type;
    char fmt[32];
};
typedef struct format_element format_element;

/* format_element type */
enum {
    DUMMY,
    DATE_YEAR,
    DATE_MONTH,
    DATE_DAY,
    FILENAME,
    DATA_NO,
    ITEM,
    DATE_DECADE,
    PERCENT
};

/*
 * Format elements (such as %y, %m, ...).
 */
struct format {
    int type;
    char key, spec;
    char *help;
};
static struct format format_tab[] = {
    { DATE_YEAR,   'y', 'd', "year of DATE" },
    { DATE_MONTH,  'm', 'd', "month of DATE" },
    { DATE_DAY,    'd', 'd', "day of DATE" },
    { FILENAME,    'f', 's', "input filename" },
    { DATA_NO,     'n', 'd', "data No." },
    { ITEM,        'i', 's', "ITEM" },
    { DATE_DECADE, 'D', 'd', "decade (year / 10)" }
};

static GT3_Duration date_shift = { -1, GT3_UNIT_SEC };


/*
 * copy a format element into fmt->fmt
 */
static int
get_format_element(format_element *fmt, const char *input, char **endptr)
{
    const char allowed[] = "0123456789+-# .";
    char *dest;
    size_t size;
    format_element dummy;
    int i;

    assert(input[0] == '%');
    input++;

    fmt->fmt[0] = '%';
    if (input[0] == '%') {
        fmt->type = PERCENT;
        fmt->fmt[1] = '%';
        fmt->fmt[2] = '\0';
        *endptr = (char *)++input;
        return 0;
    }

    size = sizeof dummy.fmt;
    size--;
    dest = fmt->fmt + 1;
    fmt->type = DUMMY;
    while (size > 1 && *input != '\0') {
        for (i = 0; i < sizeof format_tab / sizeof format_tab[0]; i++)
            if (*input == format_tab[i].key) {
                fmt->type = format_tab[i].type;
                *dest++ = format_tab[i].spec;
                input++;
                break;
            }
        if (fmt->type != DUMMY)
            break;

        if (strchr(allowed, *input) == NULL)
            break;

        *dest++ = *input++;
        size--;
    }
    *dest = '\0';
    *endptr = (char *)input;

    return (size > 0 && fmt->type != DUMMY) ? 0 : -1;
}


static int
get_date(GT3_Date *date, const GT3_HEADER *head, const char *key)
{
    int cal;

    if (GT3_decodeHeaderDate(date, head, key) < 0) {
        GT3_printErrorMessages(stderr);
        return -1;
    }

    if (date_shift.value != 0) {
        cal = GT3_guessCalendarHeader(head);
        if (cal < 0) {
            GT3_printErrorMessages(stderr);
            cal = GT3_CAL_GREGORIAN;
            /* Header is wrong, but continue. */
        }
        if (cal == GT3_CAL_DUMMY) {
            logging(LOG_WARN, "cannot guess calendar type");
            cal = GT3_CAL_GREGORIAN;
        }
        GT3_addDuration(date, &date_shift, cal);
    }
    return 0;
}


/*
 * Return value:
 *    0: successful end.
 *   -1: invalid data in a HEADER field.
 *   -2: overflow output buffer.
 *   -3: invalid format.
 */
int
gh_snprintf(char *str, size_t size, const char *format,
            const GT3_HEADER *head, const char *filename, int curr)
{
    const char *p = format;
    format_element fmt;
    char *endptr = NULL;
    int nstr = 0, date_cache = 0;
    GT3_Date date;
    int rval = 0;
    char buf[33];

    while (size > 1 && *p != '\0') {
        if (p[0] != '%') {
            *str++ = *p++;
            size--;
        } else {
            if (get_format_element(&fmt, p, &endptr) < 0) {
                rval = -3;
                break;
            }

            rval = 0;
            switch (fmt.type) {
            case DATE_YEAR:
                if (!date_cache && (rval = get_date(&date, head, "DATE")) == 0)
                    date_cache = 1;

                nstr = snprintf(str, size, fmt.fmt, date.year);
                break;
            case DATE_MONTH:
                if (!date_cache && (rval = get_date(&date, head, "DATE")) == 0)
                    date_cache = 1;

                nstr = snprintf(str, size, fmt.fmt, date.mon);
                break;
            case DATE_DAY:
                if (!date_cache && (rval = get_date(&date, head, "DATE")) == 0)
                    date_cache = 1;

                nstr = snprintf(str, size, fmt.fmt, date.day);
                break;
            case FILENAME:
                nstr = snprintf(str, size, fmt.fmt, filename);
                break;
            case DATA_NO:
                nstr = snprintf(str, size, fmt.fmt, curr + 1);
                break;
            case ITEM:
                GT3_copyHeaderItem(buf, sizeof buf, head, "ITEM");
                nstr = snprintf(str, size, fmt.fmt, buf);
                break;
            case DATE_DECADE:
                if (!date_cache && (rval = get_date(&date, head, "DATE")) == 0)
                    date_cache = 1;

                nstr = snprintf(str, size, fmt.fmt, (date.year / 10) * 10);
                break;
            case PERCENT:
                nstr = snprintf(str, size, "%%");
                break;
            default:
                assert(!"not implemented yet");
                break;
            }

            if (nstr >= size)
                rval = -2;

            if (rval < 0)
                break;
            str += nstr;
            size -= nstr;
            p = (const char *)endptr;
        }
    }
    *str = '\0';
    return rval;
}


/*
 * turn on/off "-1sec shift".
 */
void
ghprintf_shift(int onoff)
{
    date_shift.value = onoff ? -1 : 0;
    date_shift.unit = GT3_UNIT_SEC;
}


/*
 * print help message.
 */
void
ghprintf_usage(FILE *output)
{
    int i;

    fprintf(output, "Format elements (printf-like):\n");
    for (i = 0; i < sizeof format_tab / sizeof format_tab[0]; i++) {
        fprintf(output, "    %%%c: %s\n",
                format_tab[i].key,
                format_tab[i].help);
    }
}


#ifdef TEST_MAIN
#include <assert.h>

void
test1(void)
{
    format_element fmt;
    char *endptr;
    int rval;

    rval = get_format_element(&fmt, "%yXYZ", &endptr);
    assert(rval == 0);
    assert(fmt.type == DATE_YEAR);
    assert(strcmp(fmt.fmt, "%d") == 0);
    assert(strcmp(endptr, "XYZ") == 0);

    rval = get_format_element(&fmt, "%04yXYZ", &endptr);
    assert(rval == 0);
    assert(fmt.type == DATE_YEAR);
    assert(strcmp(fmt.fmt, "%04d") == 0);
    assert(strcmp(endptr, "XYZ") == 0);

    rval = get_format_element(&fmt, "%+04yXYZ", &endptr);
    assert(rval == 0);
    assert(fmt.type == DATE_YEAR);
    assert(strcmp(fmt.fmt, "%+04d") == 0);
    assert(strcmp(endptr, "XYZ") == 0);

    rval = get_format_element(&fmt, "%02mXYZ", &endptr);
    assert(rval == 0);
    assert(fmt.type == DATE_MONTH);
    assert(strcmp(fmt.fmt, "%02d") == 0);
    assert(strcmp(endptr, "XYZ") == 0);

    rval = get_format_element(&fmt, "%%XYZ", &endptr);
    assert(rval == 0);
    assert(fmt.type == PERCENT);
    assert(strcmp(fmt.fmt, "%%") == 0);
    assert(strcmp(endptr, "XYZ") == 0);

    rval = get_format_element(&fmt, "%Y", &endptr);
    assert(rval == -1);
}


void
test2(void)
{
    GT3_HEADER head;
    GT3_Date date;
    int rval;
    char str[64];


    GT3_initHeader(&head);

    GT3_setDate(&date, 100, 1, 2, 12, 0, 0);
    GT3_setHeaderDate(&head, "DATE", &date);
    GT3_setHeaderString(&head, "UTIM", "HOUR");
    GT3_setHeaderInt(&head, "TIME", 360 * 100 * 24 + 24 + 12);


    GT3_setHeaderString(&head, "ITEM", "T2");

    rval = gh_snprintf(str, sizeof str, "[%%]", &head, "T2", 1);
    assert(strcmp(str, "[%]") == 0);

    rval = gh_snprintf(str, sizeof str, "../y%y", &head, "T2", 1);
    assert(strcmp(str, "../y100") == 0);

    rval = gh_snprintf(str, sizeof str, "../y%04y", &head, "T2", 1);
    assert(strcmp(str, "../y0100") == 0);

    rval = gh_snprintf(str, sizeof str, "../y%04y/%f", &head, "T2", 1);
    assert(strcmp(str, "../y0100/T2") == 0);

    rval = gh_snprintf(str, sizeof str, "data/%f_%04y-%02m-%02d",
                           &head, "T2", 1);
    assert(strcmp(str, "data/T2_0100-01-02") == 0);

    rval = gh_snprintf(str, sizeof str, "data/%i_%04y-%02m-%02d",
                           &head, "T2", 1);
    assert(strcmp(str, "data/T2_0100-01-02") == 0);

    GT3_setDate(&date, 100, 1, 1, 0, 0, 0);
    GT3_setHeaderDate(&head, "DATE", &date);
    GT3_setHeaderInt(&head, "TIME", 360 * 100 * 24);

    rval = gh_snprintf(str, sizeof str, "../y%y/%f", &head, "T2", 1);
    assert(strcmp(str, "../y99/T2") == 0);
}


void
test3(void)
{
    GT3_HEADER head;
    GT3_Date date;
    int rval;
    char str[10];

    GT3_initHeader(&head);

    GT3_setDate(&date, 100, 1, 1, 0, 0, 0);
    GT3_setHeaderDate(&head, "DATE", &date);
    GT3_setHeaderString(&head, "UTIM", "HOUR");
    GT3_setHeaderInt(&head, "TIME", 360 * 100 * 24);

    ghprintf_shift(1);
    rval = gh_snprintf(str, sizeof str, "%y-%02m-%02d", &head, "x", 1);
    assert(rval == 0);
    assert(strcmp(str, "99-12-30") == 0);

    rval = gh_snprintf(str, sizeof str, "%3y-%02m-%02d", &head, "x", 1);
    assert(rval == 0);
    assert(strcmp(str, " 99-12-30") == 0);

    rval = gh_snprintf(str, sizeof str, "%03y-%02m-%02d", &head, "x", 1);
    assert(rval == 0);
    assert(strcmp(str, "099-12-30") == 0);

    rval = gh_snprintf(str, sizeof str, "%04y-%02m-%02d", &head, "x", 1);
    assert(rval == -2);

    ghprintf_shift(0);
    rval = gh_snprintf(str, sizeof str, "%y-%02m-%02d", &head, "x", 1);
    assert(rval == 0);
    assert(strcmp(str, "100-01-01") == 0);
}


int
main(int argc, char **argv)
{
    test1();
    test2();
    test3();
    return 0;
}
#endif
