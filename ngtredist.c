/*
 *  ngtredist.c -- redistribute gtool3-files.
 */
#include "internal.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gtool3.h"
#include "seq.h"
#include "fileiter.h"
#include "myutils.h"
#include "ghprintf.h"
#include "logging.h"

#define PROGNAME "ngtredist"

enum {
    NORMAL_MODE,
    APPEND_MODE,
    OVERWRITE_MODE
};
static int open_mode = NORMAL_MODE;
static int dryrun = 0;


char *
dirname(const char *path)
{
    static char buffer[PATH_MAX + 1];
    const char *tail;
    size_t tpos;

    if (path == NULL)
        return NULL;

    tail = path + strlen(path);
    while (path < tail && *--tail == '/')
        ;

    while (path < tail && *--tail != '/')
        ;

    while (path < tail && *(tail - 1) == '/')
        --tail;

    if (path == tail) {
        buffer[0] = (path[0] == '/') ? '/' : '.';
        buffer[1] = '\0';
    } else {
        tpos = tail - path;
        if (tpos > PATH_MAX)
            tpos = PATH_MAX;

        memcpy(buffer, path, tpos);
        buffer[tpos] = '\0';
    }
    return buffer;
}


int
build_path(const char *path)
{
    struct stat sb;
    char p[PATH_MAX + 1];
    int i, nlevel = 0;
    mode_t mode = 0777;

    if (path == NULL)
        return 0;

    p[0] = path[0];
    path++;
    for (i = 1; i < PATH_MAX; path++) {
        if (*path == '/' && p[i-1] == '/')
            continue;

        if (*path == '/' || *path == '\0') {
            p[i] = '\0';
            if (!(stat(p, &sb) == 0 && S_ISDIR(sb.st_mode))) {
                if (mkdir(p, mode) < 0)
                    return -1;
                nlevel++;
            }
            if (*path == '\0')
                break;

            p[i] = '/';
            i++;
        } else
            p[i++] = *path;
    }
    return nlevel;
}


static FILE *
open_file(const char *path)
{
    struct stat sb;
    FILE *fp = NULL;
    char *dir;
    char mode[] = "wb";

    if (stat(path, &sb) == 0) {
        if (!S_ISREG(sb.st_mode)) {
            logging(LOG_ERR, "%s: Not a regular file", path);
            return NULL;
        }
        if (open_mode == NORMAL_MODE) {
            logging(LOG_ERR, "%s: Already file exists", path);
            return NULL;
        }

        if (open_mode == APPEND_MODE) {
            logging(LOG_INFO, "Opening %s in append-mode", path);
            mode[0] = 'a';
        }
        if (open_mode == OVERWRITE_MODE)
            logging(LOG_INFO, "Opening %s in overwrite-mode", path);

        if (!dryrun && (fp = fopen(path, mode)) == NULL)
            logging(LOG_SYSERR, path);
    } else {
        logging(LOG_INFO, "Creating %s", path);
        if (!dryrun) {
            if ((dir = dirname(path)) == NULL || build_path(dir) < 0) {
                logging(LOG_SYSERR, NULL);
                return NULL;
            }

            if ((fp = fopen(path, "wb")) == NULL)
                logging(LOG_SYSERR, path);
        }
    }
    if (dryrun)
        return stdout;          /* dummy */

    return fp;
}


static int
close_file(FILE *fp)
{
    int rval = 0;

    if (!dryrun && fp) {
        rval = fclose(fp);
        if (rval != 0)
            logging(LOG_SYSERR, NULL);
    }
    return rval;
}


static int
fcopy(FILE *dest, FILE *src, size_t size)
{
    char buf[64 * 1024];
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
copy_chunk(FILE *output, GT3_File *fp)
{
    if (GT3_seek(fp, 0, SEEK_CUR) < 0) {
        GT3_printErrorMessages(stderr);
        return -1;
    }
    if (!dryrun && fcopy(output, fp->fp, fp->chsize) < 0) {
        logging(LOG_SYSERR, NULL);
        return -1;
    }
    return 0;
}


static void
sanitize(char *p)
{
    for (; *p != '\0'; p++) {
        if (isspace(*p))
            *p = '_';
        if (iscntrl(*p))
            *p = '#';
    }
}


int
redist(const char *path, const char *format, struct sequence *seq)
{
    GT3_File *fp;
    GT3_HEADER head;
    FILE *output = NULL;
    int eval, rval = 0;
    int stat;
    int sw = 0;
    char outpath[2][PATH_MAX + 1];


    if ((fp = GT3_open(path)) == NULL) {
        eval = GT3_getLastError();
        if (eval == GT3_ERR_FILE) {
            logging(LOG_INFO, "Ignore %s", path);
            return 0;
        }
        GT3_printErrorMessages(stderr);
        return -1;
    }

    outpath[0][0] = outpath[1][0] = '\0';
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

        if (GT3_readHeader(&head, fp) < 0) {
            GT3_printErrorMessages(stderr);
            rval = -1;
            break;
        }

        if (gh_snprintf(outpath[sw], sizeof outpath[sw], format,
                        &head, fp->path, fp->curr) < 0) {
            rval = -1;
            break;
        }

        sanitize(outpath[sw]);

        if (strcmp(outpath[0], outpath[1]) != 0) {
            if (close_file(output) != 0
                || (output = open_file(outpath[sw])) == NULL) {
                output = NULL;
                rval = -1;
                break;
            }
            sw ^= 1;
        }

        if (copy_chunk(output, fp) < 0) {
            rval = -1;
            break;
        }
    }
    close_file(output);
    GT3_close(fp);
    return rval;
}


void
usage(void)
{
    const char *usage_message =
        "Usage: " PROGNAME " [options] format [files...]\n"
        "\n"
        "Redistribute chunks in GTOOL3 files.\n"
        "\n"
        "Options:\n"
        "    -a        open in append mode\n"
        "    -w        open in overwrite mode\n"
        "    -t LIST   specify data No.\n"
        "    -s        do not shift -1sec in DATE\n"
        "    -n        dryrun\n"
        "    -v        verbose mode\n"
        "    -h        print help message\n"
        "\n";

    fprintf(stderr, "%s\n", GT3_version());
    fprintf(stderr, "%s\n", usage_message);
}


int
main(int argc, char **argv)
{
    struct sequence *seq = NULL;
    int ch, exitval = 0;
    char *format;

    open_logging(stderr, PROGNAME);
    GT3_setProgname(PROGNAME);

    while ((ch = getopt(argc, argv, "anst:vwh")) != -1)
        switch (ch) {
        case 'a':
            open_mode = APPEND_MODE;
            break;

        case 'n':
            set_logging_level("verbose");
            dryrun = 1;
            break;

        case 's':
            ghprintf_shift(0);
            break;

        case 't':
            if ((seq = initSeq(optarg, 1, 0x7fffffff)) == NULL) {
                perror(NULL);
                exit(1);
            }
            break;

        case 'v':
            set_logging_level("verbose");
            break;

        case 'w':
            open_mode = OVERWRITE_MODE;
            break;

        case 'h':
        default:
            usage();
            exit(1);
            break;
        }

    argc -= optind;
    argv += optind;

    if (argc == 0) {
        logging(LOG_ERR, "format string required.");
        usage();
        exit(1);
    }
    if (!seq)
        seq = initSeq(":", 1, 0x7fffffff);

    format = *argv;
    argc--;
    argv++;

    for (; argc > 0 && *argv; argc--, argv++) {
        if (redist(*argv, format, seq) < 0) {
            exitval = 1;
            break;
        }
        reinitSeq(seq, 1, 0x7fffffff);
    }
    return exitval;
}
