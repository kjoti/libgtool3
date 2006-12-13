/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  header.c
 *
 *  $Date: 2006/11/07 00:53:11 $
 */
#include "internal.h"

#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "gtool3.h"
#include "debug.h"

#define ELEM_SZ  16				/* size of each element (in byte) */
#define NUM_ELEM 64


/*
 *  Gtool3 header items.
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
	IT_STR,						/* 16-char */
	IT_STR2,					/* 32-char */
	IT_INT,
	IT_FLOAT
};

/*
 *  Dictionary of elements in the gtool-header.
 */
struct ElemDict {
	char *name;
	int id;
	int type;
	char *default_value;
};

#define is_blank(buf) is_blank2((buf), 16)

#define cpZERO  "               0"
#define cpONE   "               1"
#define cpMISS  "  -9.9900000E+02"


/*  XXX This MUST be sorted by its name. */
static struct ElemDict elemdict[] = {
	{ "AEND1", AEND1, IT_INT,   NULL               },
	{ "AEND2", AEND2, IT_INT,   NULL               },
	{ "AEND3", AEND3, IT_INT,   NULL               },
	{ "AITM1", AITM1, IT_STR,   NULL               },
	{ "AITM2", AITM2, IT_STR,   NULL               },
	{ "AITM3", AITM3, IT_STR,   NULL               },
	{ "ASTR1", ASTR1, IT_INT,   cpONE              },
	{ "ASTR2", ASTR2, IT_INT,   cpONE              },
	{ "ASTR3", ASTR3, IT_INT,   cpONE              },
	{ "CDATE", CDATE, IT_STR,   NULL               },
	{ "COPTN", COPTN, IT_STR,   NULL               },
	{ "CSIGN", CSIGN, IT_STR,   NULL               },
	{ "DATE",  DATE,  IT_STR,   NULL               },
	{ "DATE1", DATE1, IT_STR,   NULL               },
	{ "DATE2", DATE2, IT_STR,   NULL               },
	{ "DFMT",  DFMT,  IT_STR,   "UR4             " },
	{ "DIVL",  DIVL,  IT_FLOAT, cpMISS             },
	{ "DIVS",  DIVS,  IT_FLOAT, cpMISS             },
	{ "DMAX",  DMAX,  IT_FLOAT, cpMISS             },
	{ "DMIN",  DMIN,  IT_FLOAT, cpMISS             },
	{ "DNUM",  DNUM,  IT_INT,   cpZERO             },
	{ "DSET",  DSET,  IT_STR,   NULL               },
	{ "FNUM",  FNUM,  IT_INT,   cpZERO             },
	{ "IDFM",  IDFM,  IT_INT,   NULL               },
	{ "IOPTN", IOPTN, IT_INT,   cpZERO             },
	{ "ITEM",  ITEM,  IT_STR,   NULL               },
	{ "MDATE", MDATE, IT_STR,   NULL               },
	{ "MISS",  MISS,  IT_FLOAT, cpMISS             },
	{ "MSIGN", MSIGN, IT_STR,   NULL               },
	{ "ROPTN", ROPTN, IT_FLOAT, "   0.0000000E+00" },
	{ "SIZE",  SIZE,  IT_INT,   cpZERO             },
	{ "STYP",  STYP,  IT_INT,   cpONE              },
	{ "TDUR",  TDUR,  IT_INT,   cpZERO             },
	{ "TIME",  TIME,  IT_INT,   cpZERO             },
	{ "TITLE", TITL1, IT_STR2,  NULL               },
	{ "UNIT",  UNIT,  IT_STR,   NULL               },
	{ "UTIM",  UTIM,  IT_STR,   NULL               }
};


static char *
trimmed_tail(const char *str)
{
	const char *p = str + strlen(str);

	while (p > str && isspace(*(p - 1)))
		--p;

	return (char *)p;
}


static char *
strtrim(char *str)
{
	char *tail = trimmed_tail(str);
	*tail = '\0';

	return str;
}


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
GT3_copyHeaderItem(char *buf, int buflen, const GT3_HEADER *header,
				   const char *key)
{
	const char *strp = header->h;
	char *q = buf;
	struct ElemDict *p;
	int len;

	p = lookup_name(key);
	if (p == NULL) {
		gt3_error(GT3_ERR_CALL, "%s: Unknown header item", key);
		return NULL;
	}
	len = (p->type == IT_STR2) ? 2 * ELEM_SZ : ELEM_SZ;

	strp += ELEM_SZ * p->id;
	if (p->default_value && is_blank(strp)) {
		strp = p->default_value;
		len = ELEM_SZ;
	}

	/* skip preceding white spaces */
	while (isspace(*strp) && len > 0) {
		len--;
		strp++;
	}

	--buflen;					/* for null terminator */
	while (buflen > 0 && len-- > 0) {
		if (iscntrl(*strp))
			continue;

		buflen--;
		*q++ = *strp++;
	}
	*q = '\0';

	return strtrim(buf);
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
	return 0;					/* ok */
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
	char buf[2 * ELEM_SZ + 1];
	char *pfmt;
	int siz;
	struct ElemDict *p;

	if (!str)
		return;

	p = lookup_name(key);
	if (p == NULL) {
		gt3_error(GT3_ERR_CALL, "Unknown header name: %s", key);
		return;
	}

	if (p->type == IT_STR2) {
		siz = 2 * ELEM_SZ;
		pfmt = "%-32s";
	} else {
		siz = ELEM_SZ;
		pfmt = "%-16s";
	}

	snprintf(buf, siz + 1, pfmt, str);
	memcpy(header->h + ELEM_SZ * p->id, buf, siz);
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


void
GT3_mergeHeader(GT3_HEADER *dest, const GT3_HEADER *src)
{
	int id, len;
	char *q;

	for (id = 0; id < NUM_ELEM; id++) {
		/*
		 *  special treatment for "TITLE".
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
	}

	{
		GT3_HEADER header;
		char buf[33], *p;

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

	return 0;
}
#endif
