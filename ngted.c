/*
 * ngted.c -- gtool3 header editor.
 */
#include "internal.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gtool3.h"
#include "seq.h"
#include "myutils.h"
#include "fileiter.h"
#include "logging.h"

#define PROGNAME "ngted"


#define ELEMLEN   16

enum {
    TITLE = 13,
    AITM1 = 28,
    AEND3 = 36,
    MISS = 38,
    SIZE = 63
};

enum {
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_STR
};


enum {
    CHANGE_STR,
    CHANGE,
    APPEND,
    INSERT,
    SUBST,
    TOUPPER,
    TOLOWER
};

struct { char key; int value; } cmd_type_map[] = {
    { 'C', CHANGE_STR  },
    { 'c', CHANGE  },
    { 'a', APPEND  },
    { 'i', INSERT  },
    { 's', SUBST   },
    { 'l', TOLOWER },
    { 'u', TOUPPER },
};


int itemtype[] = {
    TYPE_INT,
    TYPE_STR,
    TYPE_STR,
    TYPE_STR,
    TYPE_STR,
    TYPE_STR,
    TYPE_STR,
    TYPE_STR,
    TYPE_STR,
    TYPE_STR,
    TYPE_STR,
    TYPE_INT,
    TYPE_INT,
    TYPE_STR,
    TYPE_STR,
    TYPE_STR,
    TYPE_STR,
    TYPE_STR,
    TYPE_STR,
    TYPE_STR,
    TYPE_STR,
    TYPE_STR,
    TYPE_STR,
    TYPE_STR,
    TYPE_INT,
    TYPE_STR,
    TYPE_STR,
    TYPE_INT,
    TYPE_STR,
    TYPE_INT,
    TYPE_INT,
    TYPE_STR,
    TYPE_INT,
    TYPE_INT,
    TYPE_STR,
    TYPE_INT,
    TYPE_INT,
    TYPE_STR,
    TYPE_FLOAT,
    TYPE_FLOAT,
    TYPE_FLOAT,
    TYPE_FLOAT,
    TYPE_FLOAT,
    TYPE_INT,
    TYPE_STR,
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_STR,
    TYPE_STR,
    TYPE_STR,
    TYPE_STR,
    TYPE_STR,
    TYPE_STR,
    TYPE_STR,
    TYPE_STR,
    TYPE_STR,
    TYPE_STR,
    TYPE_STR,
    TYPE_STR,
    TYPE_STR,
    TYPE_STR,
    TYPE_STR,
    TYPE_STR,
    TYPE_INT
};

/* forbidden item: */
static int forbidden_addr[] = {
    29, 30, /* ASTR1, AEND1 */
    32, 33, /* ASTR2, AEND2 */
    35, 36, /* ASTR3, AEND3 */
    37      /* DFMT */
};


struct edit_command;
typedef void (*edit_function)(GT3_HEADER *, struct edit_command *);

struct edit_command {
    int addr;                   /* 0-63 (starting with 0) */
    int cmd_type;
    edit_function func;
    char *arg1;                 /* 1st argument, if any */
    char *arg2;                 /* 2nd argument, if any */
    int len;
    int ival;
    struct edit_command *next;
};


static void
change(GT3_HEADER *head, struct edit_command *ec)
{
    memcpy(head->h + ec->addr * ELEMLEN,
           ec->arg1,
           ec->len * ELEMLEN);
}


static void
elem_upper(GT3_HEADER *head, struct edit_command *ec)
{
    char *p = head->h + ec->addr * ELEMLEN;
    int len = ec->len * ELEMLEN;

    while (len-- > 0) {
        *p = toupper(*p);
        p++;
    }
}


static void
elem_lower(GT3_HEADER *head, struct edit_command *ec)
{
    char *p = head->h + ec->addr * ELEMLEN;
    int len = ec->len * ELEMLEN;

    while (len-- > 0) {
        *p = tolower(*p);
        p++;
    }
}


static void
set_elem(GT3_HEADER *head, int addr, const char *str)
{
    memcpy(head->h + ELEMLEN * addr, str, ELEMLEN);
}


static void
set_miss(GT3_HEADER *head, struct edit_command *ec)
{
    double miss_old, temp;
    int addr = ec->addr;


    GT3_decodeHeaderDouble(&miss_old, head, "MISS");
    set_elem(head, addr, ec->arg1);

    GT3_decodeHeaderDouble(&temp, head, "DMIN");
    if (temp == miss_old)
        set_elem(head, addr + 1, ec->arg1);

    GT3_decodeHeaderDouble(&temp, head, "DMAX");
    if (temp == miss_old)
        set_elem(head, addr + 2, ec->arg1);

    GT3_decodeHeaderDouble(&temp, head, "DIVS");
    if (temp == miss_old)
        set_elem(head, addr + 3, ec->arg1);

    GT3_decodeHeaderDouble(&temp, head, "DIVL");
    if (temp == miss_old)
        set_elem(head, addr + 4, ec->arg1);
}


/*
 * change ASTR[1-3] and AEND[1-3], while the axis-length is conserved.
 */
static void
change_axis_range(GT3_HEADER *head, struct edit_command *ec)
{
    const char *astr[] = { "ASTR1", "ASTR2", "ASTR3" };
    const char *aend[] = { "AEND1", "AEND2", "AEND3" };
    int iax, istr, iend;

    iax = (ec->addr - AITM1) / 3;
    assert(iax >= 0 && iax < 3);

    (void)GT3_decodeHeaderInt(&istr, head, astr[iax]);
    (void)GT3_decodeHeaderInt(&iend, head, aend[iax]);

    if (ec->addr - (AITM1 + 3 * iax) == 1) {
        /* change ASTR[1-3] */
        iend += ec->ival - istr;
        istr = ec->ival;
    } else {
        /* change AEND[1-3] */
        istr += ec->ival - iend;
        iend = ec->ival;
    }

    GT3_setHeaderInt(head, astr[iax], istr);
    GT3_setHeaderInt(head, aend[iax], iend);
}


static void
append_str(GT3_HEADER *head, struct edit_command *ec)
{
    char *curr, *tail, *p, *src;

    /* tracking backward */
    curr = head->h + ec->addr * ELEMLEN;
    tail = curr + ec->len * ELEMLEN;
    p = tail;
    while (p > curr && *(p - 1) == ' ' )
        p--;

    /* appending */
    src = ec->arg1;
    while (p < tail && *src != '\0')
        *p++ = *src++;
}


static void
insert_str(GT3_HEADER *head, struct edit_command *ec)
{
    char *curr;
    char buf[33];

    curr = head->h + ec->addr * ELEMLEN;
    memcpy(buf, curr, ec->len * ELEMLEN);
    memcpy(curr, ec->arg1, ec->ival);
    memcpy(curr + ec->ival, buf, ec->len * ELEMLEN - ec->ival);
}


static void
subst_str(GT3_HEADER *head, struct edit_command *ec)
{
    char src[33], dest[33], dest2[33];

    memcpy(src, head->h + ec->addr * ELEMLEN, ec->len * ELEMLEN);
    src[ec->len * ELEMLEN] = '\0';
    copysubst(dest, sizeof dest, src, ec->arg1, ec->arg2);
    snprintf(dest2, sizeof dest2, "%-32s", dest);
    memcpy(head->h + ec->addr * ELEMLEN, dest2, ec->len * ELEMLEN);
}


static char *
strcpy_to_char(char *dest, size_t len, const char *src, char stop)
{
    if (len > 0) {
        while (*src != '\0' && *src != stop && --len > 0)
            *dest++ = *src++;
        *dest = '\0';
    }
    return (char *)src;
}


static char *
strtoupper(char *str)
{
    char *p;

    for (p = str; *p != '\0'; p++)
        *p = toupper(*p);
    return str;
}


static int
get_addr(const char *str, char **endptr)
{
    char name[8];
    char *p;
    int addr;

    if (isdigit(*str)) {
        addr = (int)strtol(str, &p, 10) - 1;
        if (addr < 0 || addr >= 64) {
            logging(LOG_ERR, "%d: Out of range.", addr);
            return -1;
        }
        *endptr = p;
    } else {
        p = strcpy_to_char(name, sizeof name, str, ':');
        if (*p != ':') {
            logging(LOG_ERR, "\':\' is missing.");
            return -1;
        }
        strtoupper(name);
        if ((addr = GT3_getHeaderItemID(name)) < 0) {
            logging(LOG_ERR, "%s: Unknown ITEM.", name);
            return -1;
        }
        *endptr = p + 1;
    }
    return addr;
}


static int
setup_edit_func_int(struct edit_command *ec, const char *args)
{
    char buf[17];
    int ival;
    char *endptr;

    ival = (int)strtol(args, &endptr, 10);
    if (args == endptr) {
        logging(LOG_ERR, "Argument should be a integer.");
        return -1;
    }
    if (ec->addr > AITM1 && ec->addr <= AEND3) {
        ec->func = change_axis_range;
        ec->ival = ival;
    } else {
        snprintf(buf, sizeof buf, "%16d", ival);
        ec->func = change;
        ec->arg1 = strdup(buf);
    }
    return 0;
}


static int
setup_edit_func_float(struct edit_command *ec, const char *args)
{
    char buf[17];
    float fval;
    char *endptr;

    fval = (float)strtod(args, &endptr);
    if (args == endptr) {
        logging(LOG_ERR, "Argument should be a floating-number.");
        return -1;
    }
    snprintf(buf, sizeof buf, "%16.7E", fval);

    ec->func = (ec->addr == MISS) ? set_miss : change;
    ec->arg1 = strdup(buf);
    return 0;
}


static int
setup_edit_func_str(struct edit_command *ec, const char *args)
{
    int i;

    /*
     * check forbidden operation.
     */
    for (i = 0; i < sizeof forbidden_addr / sizeof(int); i++)
        if (ec->addr == forbidden_addr[i]) {
            logging(LOG_ERR, "Forbidden operation.");
            return -1;
        }

    if (ec->cmd_type == TOUPPER) {
        ec->func = elem_upper;
        return 0;
    }

    if (ec->cmd_type == TOLOWER) {
        ec->func = elem_lower;
        return 0;
    }

    if (ec->cmd_type == CHANGE || ec->cmd_type == CHANGE_STR) {
        char buf[33];
        char fmt[8];

        snprintf(fmt, sizeof fmt, "%%-%ds", ec->len * ELEMLEN);
        snprintf(buf, sizeof buf, fmt, args);
        ec->arg1 = strdup(buf);
        ec->func = change;
        return 0;
    }

    if (ec->cmd_type == SUBST) {
        char old[33], new[33];
        char delim, *p;

        delim = *args++;
        p = strcpy_to_char(old, sizeof old, args, delim);
        if (*p != delim) {
            logging(LOG_ERR, "%s: End-delimiter not found", args);
            return -1;
        }

        args = p + 1;
        p = strcpy_to_char(new, sizeof new, args, delim);
        if (*p != delim) {
            logging(LOG_ERR, "%s: End-delimiter not found", args);
            return -1;
        }

        ec->arg1 = strdup(old);
        ec->arg2 = strdup(new);
        ec->func = subst_str;
        return 0;
    }

    if (ec->cmd_type == APPEND || ec->cmd_type == INSERT) {
        ec->ival = strlen(args);
        if (ec->ival > ec->len * ELEMLEN) {
            logging(LOG_ERR, "Too long argument.");
            return -1;
        }
        ec->arg1 = strdup(args);
        ec->func = (ec->cmd_type == APPEND) ? append_str : insert_str;
        return 0;
    }
    assert("NOT REACHED");
    return -1;
}


/*
 * setup new edit-command.
 */
static struct edit_command *
new_command(const char *str)
{
    struct edit_command *temp;
    const char *curr = str;
    char *endptr;
    int i, addr, cmd, type, stat;


    if ((temp = (struct edit_command *)
         malloc(sizeof(struct edit_command))) == NULL) {
        logging(LOG_SYSERR, NULL);
        return NULL;
    }

    if ((addr = get_addr(curr, &endptr)) < 0) {
        free(temp);
        return NULL;
    }

    if (addr == 0) {
        logging(LOG_ERR, "IDFM is not allowed to change.");
        free(temp);
        return NULL;
    }

    curr = endptr;
    cmd = -1;
    for (i = 0; i < sizeof cmd_type_map / sizeof cmd_type_map[0]; i++)
        if (cmd_type_map[i].key == *curr) {
            cmd = cmd_type_map[i].value;
            break;
        }

    if (cmd == -1) {
        logging(LOG_ERR, "%s: Syntax error in the edit command.", curr);
        free(temp);
        return NULL;
    }

    temp->addr = addr;
    temp->cmd_type = cmd;
    temp->func = NULL;
    temp->arg1 = NULL;
    temp->arg2 = NULL;
    temp->len  = (addr == TITLE) ? 2 : 1;
    temp->ival = 0;
    temp->next = NULL;

    /*
     * setup edit function.
     */
    curr++;
    type = itemtype[addr];
    if (type == TYPE_STR || cmd == CHANGE_STR)
        stat = setup_edit_func_str(temp, curr);
    else if (type == TYPE_INT)
        stat = setup_edit_func_int(temp, curr);
    else if (type == TYPE_FLOAT)
        stat = setup_edit_func_float(temp, curr);
    else {
        assert(!"NOT REACHED");
        stat = -1;
    }
    if (stat < 0) {
        free(temp);
        temp = NULL;
    }
    return temp;
}


static int
edit(GT3_File *fp, struct edit_command *clist)
{
    GT3_HEADER head, head_copy;

    if (GT3_readHeader(&head, fp) < 0) {
        GT3_printErrorMessages(stderr);
        return -1;
    }

    memcpy(&head_copy, &head, GT3_HEADER_SIZE);

    /*
     * do edit.
     */
    while (clist) {
        (*clist->func)(&head, clist);
        clist = clist->next;
    }

    if (memcmp(&head, &head_copy, GT3_HEADER_SIZE))
        /* overwrite */
        if (fseeko(fp->fp, fp->off + 4, SEEK_SET) < 0
            || fwrite(head.h, 1, GT3_HEADER_SIZE, fp->fp) != GT3_HEADER_SIZE) {
            logging(LOG_SYSERR, NULL);
            return -1;
        }

    return 0;
}


static int
edit_file(const char *path, struct edit_command *list,
          struct sequence *tseq)
{
    GT3_File *fp;
    file_iterator it;
    int rval = 0;
    int stat;

    if ((fp = GT3_openRW(path)) == NULL) {
        GT3_printErrorMessages(stderr);
        return -1;
    }

    setup_file_iterator(&it, fp, tseq);
    while ((stat = iterate_chunk2(&it)) != ITER_END) {
        if (stat == ITER_ERROR || stat == ITER_ERRORCHUNK) {
            rval = -1;
            break;
        }
        if (stat == ITER_OUTRANGE)
            continue;

        if (edit(fp, list) < 0) {
            rval = -1;
            break;
        }
    }

    GT3_close(fp);
    return rval;
}


static void
usage(void)
{
    const char *messages =
        "Usage: " PROGNAME " [options] file...\n"
        "\n"
        "edit header fields.\n"
        "\n"
        "Options:\n"
        "    -h          print help message\n"
        "    -t LIST     specify data No.\n"
        "    -e COMMAND  specify edit commmand"
        " (can be appeared in any number of times)\n"
        "\n"
        "Examples:\n"
        "  " PROGNAME " -e aitm2:s/GGLA320I/GGLA160Ix2/ file\n"
        "  " PROGNAME " -e \'title:c2m surface temperature\' file\n";

    fprintf(stderr, "%s\n", GT3_version());
    fprintf(stderr, messages);
}


int
main(int argc, char **argv)
{
    struct sequence *tseq = NULL;
    struct edit_command *clist = NULL;
    struct edit_command *temp;
    int ch;

    open_logging(stderr, PROGNAME);
    GT3_setProgname(PROGNAME);
    while ((ch = getopt(argc, argv, "e:ht:")) != -1)
        switch (ch) {
        case 'e':
            if ((temp = new_command(optarg)) == NULL)
                exit(1);
            temp->next = clist;
            clist = temp;
            break;

        case 't':
            tseq = initSeq(optarg, 1, 0x7ffffff);
            break;

        case 'h':
        default:
            usage();
            exit(1);
            break;
        }

    if (!clist) {
        logging(LOG_ERR, "No edit-command specified.");
        usage();
        exit(1);
    }

    argc -= optind;
    argv += optind;
    for (; argc > 0 && *argv; argc--, argv++) {
        if (edit_file(*argv, clist, tseq) < 0) {
            logging(LOG_ERR, "%s: error has occurred.", *argv);
            exit(1);
        }
        if (tseq)
            reinitSeq(tseq, 1, 0x7fffffff);
    }
    return 0;
}
