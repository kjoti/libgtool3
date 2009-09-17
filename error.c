/*
 *  error.c -- error message stack
 */
#include "internal.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gtool3.h"

static int err_count = 0;
static int exit_on_err  = 0;    /* flag */
static int print_on_err = 0;    /* flag */

static char *progname = NULL;


/*
 *  ring.
 */
#define MSGBUF_LEN 256
#define NUM_ESTACK 16
static int err_sp = 0;
static int err_code[NUM_ESTACK];
static int my_errno[NUM_ESTACK];
static char auxmsg[NUM_ESTACK][MSGBUF_LEN];

static const char *messages[] = {
    "No error",
    "System error",
    "Broken file",
    "Illegal API call",
    "Not a gtool file",
    "Invalid data in the header",
    "Index out of range",
    "Undefined error" /* sentinel */
};


static void
push_errcode(int code, const char *aux)
{
    if (err_count <= 0x7fffffff)
        ++err_count;

    err_code[err_sp] = code;
    my_errno[err_sp] = (code == SYSERR) ? errno : 0;

    snprintf(auxmsg[err_sp], sizeof auxmsg[err_sp], "%s", aux);

    err_sp = (err_sp + 1) % NUM_ESTACK;
}


/* XXX use last_error() to get a last error-code. */
static void
pop_errcode(void)
{
    if (err_count == 0)
        return;

    --err_count;
    --err_sp;
    if (err_sp < 0)
        err_sp = NUM_ESTACK - 1;

    err_code[err_sp] = 0;       /* clean up */
}


/* last_error() does not pop. */
static int
last_error(char **msg, char **aux)
{
    int sp, code;

    if (err_count <= 0)
        return 0;

    sp = err_sp - 1;
    if (sp < 0)
        sp = NUM_ESTACK - 1;

    code = err_code[sp];

    *msg = (code == SYSERR)
        ? strerror(my_errno[sp])
        : (char *)messages[code];

    *aux = auxmsg[sp];

    return code;
}


void
GT3_clearLastError(void)
{
    pop_errcode();
}


void
GT3_printLastErrorMessage(FILE *output)
{
    int code;
    char *msg, *aux;

    code = last_error(&msg, &aux);
    if (code == 0)
        return;

    if (output) {
        if (progname)
            fprintf(output, "%s: ", progname);

        fprintf(output, "%s", msg);
        if (aux)
            fprintf(output, ": %s", aux);

        fprintf(output, "\n");
    }
}


void
GT3_printErrorMessages(FILE *output)
{
    int num;

    num = err_count;
    if (num > NUM_ESTACK)
        num = NUM_ESTACK;

    while (num-- > 0) {
        GT3_printLastErrorMessage(output);
        GT3_clearLastError();
    }
}


void
gt3_error(int code, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);


    if (code > GT3_ERR_UNDEF || code < 0)
        code = GT3_ERR_UNDEF;

    if (code != 0) {
        char info[MSGBUF_LEN];

        /*
         *  set auxiliary message.
         */
        if (fmt)
            vsnprintf(info, sizeof info, fmt, ap);
        else
            info[0] = '\0';

        push_errcode(code, info);

        if (exit_on_err) {
            GT3_printLastErrorMessage(stderr);
            exit(code);
        }

        if (print_on_err)
            GT3_printLastErrorMessage(stderr);
    }
    va_end(ap);
}


int
GT3_ErrorCount(void)
{
    return err_count;
}


int
GT3_getLastError(void)
{
    char *msg, *aux;
    return last_error(&msg, &aux);
}


int
GT3_copyLastErrorMessage(char *buf, size_t buflen)
{
    int code;
    char *msg, *aux;

    code = last_error(&msg, &aux);
    if (code == 0)
        return -1;

    if (aux[0] != '\0')
        snprintf(buf, buflen, "%s: %s", msg, aux);
    else
        snprintf(buf, buflen, "%s", msg);

    return 0;
}


void
GT3_setExitOnError(int onoff)
{
    exit_on_err = onoff;
}


void
GT3_setPrintOnError(int onoff)
{
    print_on_err = onoff;
}


void
GT3_setProgname(const char *name)
{
    if (progname)
        free(progname);
    progname = strdup(name);
}


#ifdef TEST
#include <assert.h>

int
main(int argc, char **argv)
{
    int i;

    GT3_setProgname("errortest");

    for (i = 0; i < 20; i++)
        gt3_error(GT3_ERR_FILE, "%s %d", "foobar", i);

    assert(GT3_ErrorCount() == 20);

    GT3_printErrorMessages(stderr);


    return 0;
}
#endif
