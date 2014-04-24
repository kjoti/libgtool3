/*
 * strman.c
 */
#include <ctype.h>


char *
toupper_string(char *str)
{
    char *p = str;

    while ((*p = toupper(*p)))
        p++;
    return str;
}
