/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  ngtdiff.c -- Compare files.
 */
#include "internal.h"

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gtool3.h"
#include "seq.h"
#include "fileiter.h"
#include "myutils.h"
#include "logging.h"

#ifndef min
#  define min(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#  define max(a,b) ((a) > (b) ? (a) : (b))
#endif

#define PROGNAME "ngtdiff"
#define ELEMLEN 16

#define DATA(vbuf, i) \
	(((vbuf)->type == GT3_TYPE_DOUBLE) \
	? *((double *)((vbuf)->data) + (i)) \
	: *((float *) ((vbuf)->data) + (i)) )

#define ISMISS(vbuf, i) \
	(((vbuf)->type == GT3_TYPE_DOUBLE) \
	? *((double *)((vbuf)->data) + (i)) == vbuf->miss \
	: *((float *) ((vbuf)->data) + (i)) == (float)vbuf->miss )

static int (*equal)(double, double) = NULL;
static double tolerance = 0.;
static unsigned ignored_item[64];
static int zrange[] = { 0, 0x7ffffff };
static unsigned detail_print = 1;

#define ISCNTRL(c) ((c) < 040 || (c) == 0177)


static void
copy(char *dest, const char *src)
{
	int i;
	for (i = 0; i < ELEMLEN; i++, src++, dest++)
		*dest = ISCNTRL(*src) ? '#' : *src;
}


static int
equal_rel(double a, double b)
{
	double err;

	if (a == b)
		return 1;
	err = (a != .0) ? fabs((a - b) / a) : 1.;
	return err < tolerance;
}

static int
equal_abs(double a, double b)
{
	return fabs(a - b) < tolerance;
}

void
print_header(const GT3_Varbuf *var1, const GT3_Varbuf *var2)
{
	printf("###\n");
	printf("# FileA: %s (No.%d)\n", var1->fp->path, 1 + var1->fp->curr);
	printf("# FileB: %s (No.%d)\n", var2->fp->path, 1 + var2->fp->curr);
}


void
diff_header(const GT3_HEADER *head1, const GT3_HEADER *head2)
{
	char h1[17], h2[17];
	int i;
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

	h1[16] = h2[16] = '\0';
	printf("#\n# Difference in header fields:\n");
	printf("#%17s %20s %20s\n", "ITEM", "FileA", "FileB");
	for (i = 0; i < 64; i++) {
		if (ignored_item[i])
			continue;
		copy(h1, head1->h + i * ELEMLEN);
		copy(h2, head2->h + i * ELEMLEN);
		if (memcmp(h1, h2, ELEMLEN) != 0)
			printf(" %17s   (%16s)   (%16s)\n", hlabel[i], h1, h2);
	}
}


int
is_same_shape(const GT3_HEADER *head1, const GT3_HEADER *head2)
{
	int astr1[2], aend1[2];
	int astr2[2], aend2[2];

	GT3_decodeHeaderInt(&astr1[0], head1, "ASTR1");
	GT3_decodeHeaderInt(&aend1[0], head1, "AEND1");
	GT3_decodeHeaderInt(&astr1[1], head1, "ASTR2");
	GT3_decodeHeaderInt(&aend1[1], head1, "AEND2");

	GT3_decodeHeaderInt(&astr2[0], head2, "ASTR1");
	GT3_decodeHeaderInt(&aend2[0], head2, "AEND1");
	GT3_decodeHeaderInt(&astr2[1], head2, "ASTR2");
	GT3_decodeHeaderInt(&aend2[1], head2, "AEND2");

	return astr1[0] == astr2[0]
		&& aend1[0] == aend2[0]
		&& astr1[1] == astr2[1]
		&& aend1[1] == aend2[1];
}


int
diff_var(GT3_Varbuf *var1, GT3_Varbuf *var2)
{
	GT3_HEADER head1, head2;
	char item1[19], item2[19];
	char vstr1[21], vstr2[21];
	unsigned miss1, miss2;
	unsigned flag = 0;
	int i, j, ij, z, z1;
	int cnt = 0, total = 0;
	double v1, v2;
	double err, maxerr = 0.;
	double rms = 0.;
	int nrms = 0;
	int ioff = 1, joff = 1, koff = 1;
	int numA = 0, numB = 0;
	double sumA = 0., sumB = 0.;
	int sameshape;

	if (   GT3_readHeader(&head1, var1->fp) < 0
		|| GT3_readHeader(&head2, var2->fp) < 0) {
		GT3_printErrorMessages(stderr);
		return -1;
	}
	item1[0] = 'A';
	item2[0] = 'B';
	item1[1] = item2[1] = ':';
	GT3_copyHeaderItem(item1 + 2, sizeof item1 - 2, &head1, "ITEM");
	GT3_copyHeaderItem(item2 + 2, sizeof item2 - 2, &head2, "ITEM");
	GT3_decodeHeaderInt(&ioff, &head1, "ASTR1");
	GT3_decodeHeaderInt(&joff, &head1, "ASTR2");
	GT3_decodeHeaderInt(&koff, &head1, "ASTR3");
	/*
	 *  check data shape
	 */
	sameshape = is_same_shape(&head1, &head2);

	/*  overwrite ignored item in GT3_HEADER */
	for (i = 0; i < 64; i++)
		if (ignored_item[i]) {
			memset(head1.h + i * ELEMLEN, ' ', ELEMLEN);
			memset(head2.h + i * ELEMLEN, ' ', ELEMLEN);
		}

	if (memcmp(&head1, &head2, GT3_HEADER_SIZE) != 0) {
		print_header(var1, var2);
		diff_header(&head1, &head2);
		flag = 1;
	}

	if (!sameshape) {
		printf("# Different shape. Skip...\n");
		return 1;
	}

	/*
	 *  Compare Data body.
	 */
	z1 = min(max(var1->fp->dimlen[2], var2->fp->dimlen[2]), zrange[1]);
	for (z = zrange[0]; z < z1; z++) {
		if (z >= var1->fp->dimlen[2] || z >= var2->fp->dimlen[2])
			continue;

		if (GT3_readVarZ(var1, z) < 0 || GT3_readVarZ(var2, z) < 0) {
			GT3_printErrorMessages(stderr);
			return -1;
		}

		for (ij = 0; ij < var1->dimlen[0] * var1->dimlen[1]; ij++) {
			v1 = DATA(var1, ij);
			v2 = DATA(var2, ij);
			miss1 = ISMISS(var1, ij);
			miss2 = ISMISS(var2, ij);

			if (miss1 & miss2)
				continue;

			if (miss1 ^ miss2 || !(v1 == v2 || (equal && equal(v1, v2)))) {
				if (detail_print) {
					if ((flag & 1) == 0) {
						print_header(var1, var2);
						flag |= 1;
					}
					if ((flag & 2) == 0) {
						printf("#\n# Data:\n");
						printf("#%5s %5s %5s %20s %20s\n",
							   "X", "Y", "Z", item1, item2);
						flag |= 2;
					}
					i = ioff + ij % var1->dimlen[0];
					j = joff + ij / var1->dimlen[0];

					vstr1[0] = vstr2[0] = '_';
					vstr1[1] = vstr2[1] = '\0';
					if (!miss1)
						snprintf(vstr1, sizeof vstr1, "%20.7g", v1);
					if (!miss2)
						snprintf(vstr2, sizeof vstr2, "%20.7g", v2);

					printf(" %5d %5d %5d %20s %20s\n",
							i, j, koff + z, vstr1, vstr2);
				}
				cnt++;
			}
			if (miss1 == 0) {
				numA++;
				sumA += v1;
			}
			if (miss2 == 0) {
				numB++;
				sumB += v2;
			}
			if ((miss1 | miss2) == 0) {
				err = v1 - v2;
				rms += err * err;
				nrms++;
				err = fabs(err);
				if (err > maxerr)
					maxerr = err;
			}
		}
		total += var1->dimlen[0] * var1->dimlen[1];
	}
	if (cnt > 0) {
		if ((flag & 1) == 0) {
			print_header(var1, var2);
			flag |= 1;
		}
		printf("#\n# Summary:\n");
		printf("%18s: %s vs %s\n", "ITEMS", item1 + 2, item2 + 2);
		printf("%18s: %d / %d grids\n", "differ.", cnt, total);
		if (numA > 0) {
			sumA /= numA;
			printf("%18s: %.7g\n", "ave(A)", sumA);
		}
		if (numB > 0) {
			sumB /= numB;
			printf("%18s: %.7g\n", "ave(B)", sumB);
		}
		if (numA > 0 && numB > 0)
			printf("%18s: %.4g\n", "ave(B)-ave(A)", sumB - sumA);

		printf("%18s: %.7g\n", "max(|A-B|)", maxerr);
		if (nrms > 0) {
			rms = sqrt(rms / nrms);
			printf("%18s: %.7g\n", "RMS", rms);
			if (sumA != 0.)
				printf("%18s: %.4g%%\n", "RMS/|ave(A)|",
					   100. * (rms / fabs(sumA)));
		}
	}
	return flag ? 1 : 0;
}


/*
 *  diff_file() compares two GTOOL3-files.
 *
 *  Return value:
 *    0  no difference
 *    1  if difference
 *   -1  error
 */
int
diff_file(const char *path1, const char *path2,
		  struct sequence *seq1, struct sequence *seq2)
{
	GT3_File *fp1, *fp2;
	GT3_Varbuf *var1, *var2;
	int rc, rval = 0;

	if (   (fp1 = GT3_open(path1)) == NULL
		|| (fp2 = GT3_open(path2)) == NULL
		|| (var1 = GT3_getVarbuf(fp1)) == NULL
		|| (var2 = GT3_getVarbuf(fp2)) == NULL) {
		GT3_printErrorMessages(stderr);
		return -1;
	}
	if (seq1 && seq2) {
		int stat1, stat2;

		for (;;) {
			stat1 = iterate_chunk(fp1, seq1);
			stat2 = iterate_chunk(fp2, seq2);
			if (stat1 == ITER_END || stat2 == ITER_END)
				break;

			if (   stat1 == ITER_ERROR || stat1 == ITER_ERRORCHUNK
				|| stat2 == ITER_ERROR || stat2 == ITER_ERRORCHUNK)
				break;

			if (stat1 == ITER_OUTRANGE || stat2 == ITER_OUTRANGE)
				continue;

			if ((rc = diff_var(var1, var2)) < 0) {
				rval = -1;
				break;
			}
			rval |= rc;
		}
	} else {
		for (;;) {
			if ((rc = diff_var(var1, var2)) < 0) {
				rval = -1;
				break;
			}
			rval |= rc;

			if (GT3_next(fp1) < 0 || GT3_next(fp2) < 0) {
				GT3_printErrorMessages(stderr);
				rval = -1;
				break;
			}

			if (GT3_eof(fp2))
				break;

			if (GT3_eof(fp1))
				GT3_rewind(fp1);
		}
	}
	GT3_freeVarbuf(var1);
	GT3_freeVarbuf(var2);
	GT3_close(fp1);
	GT3_close(fp2);
	return rval;
}


static int
set_range(int range[], const char *str)
{
	int nf;

	if ((nf = get_ints(range, 2, str, ':')) < 0)
		return -1;

	/*
	 *  XXX
	 *  transform
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

void
usage(void)
{
	const char *usage_message =
		"Usage: " PROGNAME " [options] FileA FileB\n"
		"\n"
		"Compare files.\n"
		"\n"
		"Options:\n"
		"    -h        print help message\n"
		"    -A LIST   specify data No. for FileA\n"
		"    -B LIST   specify data No. for FileB\n"
		"    -S        ignore CDATE, CSIGN, MDATE, MSIGN\n"
		"    -T        ignore TIME, DATE, TDUR, DATE1, DATE2\n"
		"    -a value  specify tolerance (absolute error)\n"
		"    -r value  specify tolerance (relative error)\n"
		"    -s        output summarized form\n"
		"    -t LIST   specify data No. for both FileA and FileB\n"
		"    -z RANGE  specify Z-range\n";

	fprintf(stderr, "%s\n", GT3_version());
	fprintf(stderr, "%s\n", usage_message);
}


int
main(int argc, char **argv)
{
	struct sequence *seq1 = NULL;
	struct sequence *seq2 = NULL;
	int ch;
	int rval;
	char *endptr;

	open_logging(stderr, PROGNAME);
	GT3_setProgname(PROGNAME);
	while ((ch = getopt(argc, argv, "A:B:STa:hr:st:z:")) != -1)
		switch (ch) {
		case 'A':
			if ((seq1 = initSeq(optarg, 1, 0x7fffffff)) == NULL) {
				logging(LOG_SYSERR, NULL);
				exit(1);
			}
			break;
		case 'B':
			if ((seq2 = initSeq(optarg, 1, 0x7fffffff)) == NULL) {
				logging(LOG_SYSERR, NULL);
				exit(1);
			}
			break;
		case 'S':
			/* CDATE, CSIGN, MDATE, MSIGN */
			ignored_item[59] = ignored_item[60] = 1;
			ignored_item[61] = ignored_item[62] = 1;
			break;
		case 'T':
			/* TIME, DATE, TDUR, DATE1, DATE2 */
			ignored_item[24] = ignored_item[26] = ignored_item[27] = 1;
			ignored_item[47] = ignored_item[48] = 1;
			break;
		case 'a':
		case 'r':
			tolerance = strtod(optarg, &endptr);
			if (optarg == endptr) {
				logging(LOG_ERR, "%s: Invalid argument", optarg);
				usage();
				exit(1);
			}
			equal = ch == 'a' ? equal_abs : equal_rel;
			break;
		case 's':
			detail_print = 0;
			break;
		case 't':
			seq1 = initSeq(optarg, 1, 0x7fffffff);
			seq2 = initSeq(optarg, 1, 0x7fffffff);
			break;
		case 'z':
			if (set_range(zrange, optarg) < 0) {
				logging(LOG_ERR, "%s: Invalid argument", optarg);
				usage();
				exit(1);
			}
			break;
		case 'h':
		default:
			usage();
			exit(0);
			break;
		}
	argc -= optind;
	argv += optind;

	if (argc != 2) {
		usage();
		exit(1);
	}

	if (seq1 != NULL && seq2 == NULL)
		if ((seq2 = initSeq(":", 1, 0x7fffffff)) == NULL) {
			logging(LOG_SYSERR, NULL);
			exit(1);
		}

	if (seq1 == NULL && seq2 != NULL)
		if ((seq1 = initSeq(":", 1, 0x7fffffff)) == NULL) {
			logging(LOG_SYSERR, NULL);
			exit(1);
		}

	rval = diff_file(argv[0], argv[1], seq1, seq2);
	return rval < 0 ? 255 : rval;
}
