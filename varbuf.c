/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  varbuf.c -- Buffer to read data from GT3_File.
 *
 *  $Date: 2006/11/07 00:53:11 $
 */
#include "internal.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gtool3.h"
#include "bits_set.h"
#include "int_pack.h"
#include "talloc.h"
#include "debug.h"

#ifndef min
#  define min(a,b) ((a) < (b) ? (a) : (b))
#endif


/*
 *  status staff for GT3_Varbuf.
 *  This is not accessible from clients.
 */
struct varbuf_status {
	GT3_HEADER head;

	int ch;						/* cached chunk-index */
	int z;						/* cached z-index (-1: not cached) */
	bits_set y;					/* cached y-index */
};
typedef struct varbuf_status varbuf_status;



static int read_UR4(GT3_Varbuf *var, size_t skip, size_t nelem, FILE *fp);
static int read_UR8(GT3_Varbuf *var, size_t skip, size_t nelem, FILE *fp);
static int read_URC1(GT3_Varbuf *var, size_t skip, size_t nelem, FILE *fp);
static int read_URC2(GT3_Varbuf *var, size_t skip, size_t nelem, FILE *fp);
static int read_URX(GT3_Varbuf *var, size_t skip, size_t nelem, FILE *fp);

typedef int (*RFptr)(GT3_Varbuf *, size_t, size_t, FILE *);
static RFptr read_fptr[] = {
	read_UR4,
	read_URC2,
	read_URC1,
	read_UR8,
	read_URX
};

typedef void (*UNPACK_FUNC)(const unsigned *packed, int packed_len,
							double ref, int ne, int nd,
							double miss, float *data);


#ifndef IS_LITTLE_ENDIAN
/*
 *  run-time endian check.
 */
#define IS_LITTLE_ENDIAN is_little_endian()
static int
is_little_endian(void)
{
	unsigned a = 1;

	return *((char *)&a) == 1;
}
#endif


#define clip(v, l, h) ((v) < (l) ? (l) : ((v) > (h) ? (h) : v))


/*
 *  To distinguish I/O error and file-format error (unexpected EOF).
 */
static int
xfread(void *ptr, size_t size, size_t nmemb, FILE *fp, const char *str)
{
	if (fread(ptr, size, nmemb, fp) != nmemb) {
		if (feof(fp))
			gt3_error(GT3_ERR_BROKEN, "Unexpected EOF(%s)", str);
		else
			gt3_error(SYSERR, "I/O Error(%s)", str);

		return -1;
	}
	return 0;
}


static void *
sread_word(void *dest, const void *src)
{
	char *q = dest;
	const char *p = src;

	if (IS_LITTLE_ENDIAN) {
		q[0] = p[3];
		q[1] = p[2];
		q[2] = p[1];
		q[3] = p[0];
	} else {
		q[0] = p[0];
		q[1] = p[1];
		q[2] = p[2];
		q[3] = p[3];
	}

	return dest;
}


static void *
sread_dword(void *dest, const void *src)
{
	char *q = dest;
	const char *p = src;

	if (IS_LITTLE_ENDIAN) {
		q[0] = p[7];
		q[1] = p[6];
		q[2] = p[5];
		q[3] = p[4];
		q[4] = p[3];
		q[5] = p[2];
		q[6] = p[1];
		q[7] = p[0];
	} else {
		q[0] = p[0];
		q[1] = p[1];
		q[2] = p[2];
		q[3] = p[3];
		q[4] = p[4];
		q[5] = p[5];
		q[6] = p[6];
		q[7] = p[7];
	}

	return dest;
}


static int
read_UR4(GT3_Varbuf *var, size_t skip, size_t nelem, FILE *fp)
{
	float *ptr;

	if (skip != 0 && fseeko(fp, 4 * skip, SEEK_CUR) < 0)
		return -1;

	assert(var->type == GT3_TYPE_FLOAT);
	ptr = (float *)var->data;
	ptr += skip;

	if (xfread(ptr, 4, nelem, fp, var->fp->path) < 0)
		return -1;

	if (IS_LITTLE_ENDIAN)
		reverse_words(ptr, nelem);

	return 0;
}


static int
read_UR8(GT3_Varbuf *var, size_t skip, size_t nelem, FILE *fp)
{
	double *ptr;

	if (skip != 0 && fseeko(fp, 8 * skip, SEEK_CUR) < 0)
		return -1;

	assert(var->type == GT3_TYPE_DOUBLE);
	ptr = (double *)var->data;
	ptr += skip;

	if (xfread(ptr, 8, nelem, fp, var->fp->path) < 0)
		return -1;

	if (IS_LITTLE_ENDIAN)
		reverse_dwords(ptr, nelem);

	return 0;
}


/*
 *  read_URCv() supports URC1 and URC2 format.
 *
 *  XXX: 'skip' and 'nelem' are not in bytes.
 */
static int
read_URCv(GT3_Varbuf *var, size_t skip, size_t nelem, FILE *fp,
		  UNPACK_FUNC unpack_func)
{
	unsigned packed[1024];
	unsigned char pbuf[8 + 4 + 4 + 7 * sizeof(FTN_HEAD)];
	double ref;
	int nd, ne;
	FTN_HEAD sizh;
	size_t num;
	int i;
	float *outp;

	/*
	 *  Three parameters (ref, D, and E)
	 */
	if (xfread(pbuf, 1, sizeof pbuf, fp, var->fp->path) < 0)
		return -1;

	sread_dword(&ref, pbuf + 4); /* ref (double) */
	sread_word(&nd,   pbuf + 20); /* D (integer) */
	sread_word(&ne,   pbuf + 32); /* E (integer) */

	debug3("URC(ref,nd,ne) = %.4g, %d, %d", ref, nd, ne);

	/* fortran header for packed data... */
	sread_word(&sizh, pbuf + sizeof pbuf - 4);
	if (sizh != 2 * var->dimlen[0] * var->dimlen[1]) {
		gt3_error(GT3_ERR_BROKEN, NULL);
		return -1;
	}

	/*
	 *
	 */
	skip &= ~1U;
	nelem = (nelem + 1) & ~1U;

	if (skip != 0 && fseeko(fp, 2 * skip, SEEK_CUR) < 0) {
		gt3_error(SYSERR, "read_URCv()");
		return -1;
	}

	assert(var->type == GT3_TYPE_FLOAT);
	outp = (float *)var->data + skip;


	/*
	 *  unpack data and store them into var->data.
	 */
	for (i = 0; nelem > 0; i++, nelem -= num) {
		num = min(nelem, sizeof packed / 2);

		if (xfread(packed, 4, num / 2, fp, var->fp->path) < 0)
			return -1;

		/* reversing byte order */
		if (IS_LITTLE_ENDIAN)
			reverse_words(packed, num / 2);

		unpack_func(packed, num / 2, ref, ne, nd,
					var->miss, outp + i * (sizeof packed / 2));
	}
	return 0;
}


static int
read_URC1(GT3_Varbuf *var, size_t skip, size_t nelem, FILE *fp)
{
	return read_URCv(var, skip, nelem, fp, urc1_unpack);
}


static int
read_URC2(GT3_Varbuf *var, size_t skip, size_t nelem, FILE *fp)
{
	return read_URCv(var, skip, nelem, fp, urc2_unpack);
}


/*
 *  XXX: 'skip' and 'nelem' are ignored for now.
 *  read_URX() reads all data in a z-plane.
 */
static int
read_URX(GT3_Varbuf *var, size_t skip, size_t nelem, FILE *fp)
{
#define RESERVE_SIZE (640*320)

	unsigned packed_buf[RESERVE_SIZE];
	unsigned idata_buf[RESERVE_SIZE];
	unsigned *packed;
	unsigned *idata;
	off_t curr;
	int zpos;
	double dma[2];
	unsigned packed_len;
	unsigned nbit;
	unsigned imiss;
	double scale;
	float *outp;
	int i;



	/*
	 * XXX: read_URX() always reads all data in a z-plane.
	 * 'nelem' passed as an argument is ignored.
	 */
	nelem = (size_t)(var->dimlen[0] * var->dimlen[1]);

	curr = ftello(fp);

	nbit = (unsigned)var->fp->fmt >> GT3_FMT_MASKBIT;
	assert(nbit > 0 && nbit < 32);

	packed_len = pack32_len(nelem, nbit);


	/* calculate 'zpos' from 'curr' and 'fp->off' */
	zpos = (int)(curr - var->fp->off);
	zpos -= 5 * sizeof(FTN_HEAD) + GT3_HEADER_SIZE
		+ 2 * sizeof(double) * var->dimlen[2];

	assert(zpos % (packed_len * sizeof(uint32_t)) == 0);
	zpos /= packed_len * sizeof(uint32_t);

	/*
	 *  read DMA(dmin & amp)
	 */
	if (fseeko(fp, var->fp->off
			   + 2 * sizeof(FTN_HEAD) + GT3_HEADER_SIZE
			   + sizeof(FTN_HEAD)
			   + 2 * sizeof(double) * zpos,
			   SEEK_SET) < 0) {
		return -1;
	}
	if (xfread(dma, sizeof(double), 2, fp, var->fp->path) < 0)
		return -1;
	if (IS_LITTLE_ENDIAN)
		reverse_dwords(dma, 2);

	/*
	 *  allocate bufs.
	 */
	packed = (unsigned *)tiny_alloc(packed_buf, sizeof(packed_buf),
									sizeof(unsigned) * packed_len);
	idata  = (unsigned *)tiny_alloc(idata_buf, sizeof(idata_buf),
									sizeof(unsigned) * nelem);

	/*
	 *  read packed data.
	 */
	fseeko(fp, curr, SEEK_SET);
	if (xfread(packed, sizeof(uint32_t), packed_len, fp, var->fp->path) < 0) {
		tiny_free(packed, packed_buf);
		tiny_free(idata, idata_buf);
		return -1;
	}


	if (IS_LITTLE_ENDIAN)
		reverse_words(packed, packed_len);

	unpack_bits_from32(idata, nelem, packed, nbit);


	imiss = (1U << nbit) - 1;
	scale = (imiss == 1) ? 0. : dma[1] / (imiss - 1);

	outp = (float *)var->data;
	for (i = 0; i < nelem; i++)
		outp[i] = (idata[i] != imiss)
			? dma[0] + idata[i] * scale
			: var->miss;

	tiny_free(packed, packed_buf);
	tiny_free(idata, idata_buf);
	return 0;
}


static int
update_varbuf(GT3_Varbuf *vbuf, GT3_File *fp)
{
	int dim[3];
	GT3_HEADER head;
	void *data = NULL;
	size_t newsize, elsize;
	int type;
	double missd;
	varbuf_status *status;

	if (GT3_readHeader(&head, fp) < 0)
		return -1;

	switch (fp->fmt) {
	case GT3_FMT_UR8:
		type = GT3_TYPE_DOUBLE;
		elsize = sizeof(double);
		break;
	default:
		type = GT3_TYPE_FLOAT;
		elsize = sizeof(float);
		break;
	}

	/*
	 *  set missing value.
	 */
	if (GT3_decodeHeaderDouble(&missd, &head, "MISS") < 0) {
		gt3_error(GT3_ERR_HEADER, "MISS");

		missd = -999.0; /* ignore this error... */
	}

	dim[0] = fp->dimlen[0];
	dim[1] = fp->dimlen[1];
	dim[2] = fp->dimlen[2];

	newsize = elsize * ((dim[0] * dim[1] + 1) & ~1U);
	if (newsize > vbuf->bufsize) {
		/*
		 *  reallocation of data buffer.
		 */
		if ((data = realloc(vbuf->data, newsize)) == NULL) {
			gt3_error(SYSERR, NULL);
			return -1;
		}
	}

	if (vbuf->stat_ == NULL) {
		if ((status = malloc(sizeof(varbuf_status))) == NULL) {
			gt3_error(SYSERR, NULL);
			free(data);
			return -1;
		}
		memset(status, 0, sizeof(varbuf_status));
		debug0("update_varbuf(): status allocated");
	} else
		status = (varbuf_status *)vbuf->stat_;

	/*
	 *  clear 'status'.
	 */
	if (resize_bits_set(&status->y, dim[1] + 1) < 0) {
		gt3_error(SYSERR, NULL);
		return -1;
	}
	BS_CLSALL(status->y);
	GT3_copyHeader(&status->head, &head);
	status->ch = fp->curr;
	status->z  = -1;

	/*
	 *  all checks passed.
	 */
	vbuf->fp   = fp;
	vbuf->type = type;
	vbuf->dimlen[0] = dim[0];
	vbuf->dimlen[1] = dim[1];
	vbuf->dimlen[2] = dim[2];
	vbuf->miss   = missd;

	if (data) {
		vbuf->bufsize = newsize;
		vbuf->data = data;
		debug1("update_varbuf(): reallocated: %d-byte", vbuf->bufsize);
	}

	if (vbuf->stat_ == NULL)
		vbuf->stat_ = status;

	debug1("update_varbuf(): type  = %d", type);
	debug1("update_varbuf(): bufsize  = %d", vbuf->bufsize);
	debug3("update_varbuf(): dim = %d, %d, %d", dim[0], dim[1], dim[2]);
	debug1("update_varbuf(): miss  = %g", vbuf->miss);
	return 0;
}


static void
update2_varbuf(GT3_Varbuf *var)
{
	if (!GT3_isHistfile(var->fp)) {
		varbuf_status *stat = (varbuf_status *)var->stat_;

		if (stat->ch != var->fp->curr)
			update_varbuf(var, var->fp);
	}
}


static GT3_Varbuf *
new_varbuf(void)
{
	GT3_Varbuf *temp;

	if ((temp = malloc(sizeof(GT3_Varbuf))) == NULL) {
		gt3_error(SYSERR, NULL);
		return NULL;
	}

	memset(temp, 0, sizeof(GT3_Varbuf));
	temp->fp    = NULL;
	temp->data  = NULL;
	temp->stat_ = NULL;
	return temp;
}


void
GT3_freeVarbuf(GT3_Varbuf *var)
{
	if (var) {
		varbuf_status *stat = (varbuf_status *)var->stat_;

		/* XXX GT_File is not closed.  */
		free(var->data);
		free_bits_set(&stat->y);
		free(var->stat_);
		free(var);
	}
}


GT3_Varbuf *
GT3_getVarbuf(GT3_File *fp)
{
	GT3_Varbuf *temp;

	if ((temp = new_varbuf()) == NULL)
		return NULL;

	if (update_varbuf(temp, fp) < 0) {
		GT3_freeVarbuf(temp);
		return NULL;
	}
	return temp;
}


int
GT3_readVarZ(GT3_Varbuf *var, int zpos)
{
	size_t nelem;
	varbuf_status *stat = (varbuf_status *)var->stat_;

	update2_varbuf(var);

	if (zpos < 0 || zpos >= var->dimlen[2]) {
		gt3_error(GT3_ERR_INDEX, "GT3_readVarZ(): z=%d", zpos);
		return -1;
	}

	/*
	 *  check if cached.
	 */
	if (stat->ch == var->fp->curr
		&& stat->z == zpos
		&& BS_TEST(stat->y, var->dimlen[1])) {
		debug2("cached: t=%d, z=%d", var->fp->curr, zpos);
		return 0;
	}

	/*
	 *  move to z-slice position.
	 */
	if (GT3_skipZ(var->fp, zpos) < 0)
		return -1;

	nelem = var->dimlen[0] * var->dimlen[1];
	if (read_fptr[var->fp->fmt & 255U](var, 0, nelem, var->fp->fp) < 0) {
		debug2("read failed: t=%d, z=%d", var->fp->curr, zpos);

		stat->z  = -1;
		return -1;
	}

	/* set flags */
	stat->ch = var->fp->curr;
	stat->z  = zpos;
	BS_SET(stat->y, var->dimlen[1]);

	return 0;
}


int
GT3_readVarZY(GT3_Varbuf *var, int zpos, int ypos)
{
	size_t skip, nelem;
	varbuf_status *stat = (varbuf_status *)var->stat_;

	update2_varbuf(var);
	if (   zpos < 0 || zpos >= var->dimlen[2]
		|| ypos < 0 || ypos >= var->dimlen[1]) {
		gt3_error(GT3_ERR_INDEX, "GT3_readVarZY(): y=%d, z=%d", ypos, zpos);
		return -1;
	}

	/*
	 *  for small buffer-size.
	 */
	if (var->dimlen[0] * var->dimlen[1] < 1024)
		return GT3_readVarZ(var, zpos);

	/*
	 *  check if cached.
	 */
	if (stat->ch == var->fp->curr
		&& stat->z  == zpos
		&& (BS_TEST(stat->y, ypos) || BS_TEST(stat->y, var->dimlen[1]))) {

		debug3("cached: t=%d, z=%d, y=%d", var->fp->curr, zpos, ypos);
		return 0;
	}

	if (GT3_skipZ(var->fp, zpos) < 0)
		return -1;

	skip  = ypos * var->dimlen[0];
	nelem = var->dimlen[0];
	if (read_fptr[var->fp->fmt & 255U](var, skip, nelem, var->fp->fp) < 0) {
		debug3("read failed: t=%d, z=%d, y=%d", var->fp->curr, zpos, ypos);

		stat->z  = -1;
		return -1;
	}

	/*
	 *  set flags
	 */
	if (stat->z != zpos || stat->ch != var->fp->curr) {
		BS_CLSALL(stat->y);
	}
	stat->ch = var->fp->curr;
	stat->z  = zpos;
	BS_SET(stat->y, ypos);

	return 0;
}


int
GT3_readVar(double *rval, GT3_Varbuf *var, int x, int y, int z)
{
	if (GT3_readVarZY(var, z, y) < 0)
		return -1;

	if (x < 0 || x >= var->dimlen[0]) {
		gt3_error(GT3_ERR_INDEX, "GT3_readVar(): x=%d", x);
		return -1;
	}

	if (var->type == GT3_TYPE_FLOAT) {
		float *ptr;

		ptr = (float *)var->data;
		*rval = ptr[x + var->dimlen[0] * y];
	} else {
		double *ptr;

		ptr = (double *)var->data;
		*rval = ptr[x + var->dimlen[0] * y];
	}
	return 0;
}


/*
 *  NOTE
 *  GT3_{copy,get}XXX() functions do not update GT3_Varbuf.
 */

/*
 *  GT3_copyVarDouble() copies data stored in GT3_Varbuf into 'buf'.
 *
 *  This function does not guarantee that GT3_Varbuf is filled.
 *  Users are responsible for calling GT3_readZ() to read data.
 *
 *  return value: The number of copied elements.
 *
 *  example:
 *   GT3_copyVarDouble(buf, buflen, var, 0, 1);
 *   -> all z-slice data are copied.
 *
 *   GT3_copyVarDouble(buf, buflen, var, 0, var->dimlen[0]);
 *   ->  some meridional data are copied.
 */
int
GT3_copyVarDouble(double *buf, size_t buflen,
				  const GT3_Varbuf *var, int begin, int skip)
{
	int maxlen = var->dimlen[0] * var->dimlen[1];
	int end, nelem;

	if (skip > 0) {
		begin = clip(begin, 0, maxlen);

		end = maxlen;
		nelem = (end - begin + (skip - 1)) / skip;
	} else if (skip < 0) {
		begin = clip(begin, -1, maxlen - 1);

		end = -1;
		nelem = (end - begin + (skip + 1)) / skip;
	} else {
		if (begin < 0 || begin >= maxlen)
			nelem = 0;
		else
			nelem = buflen;
	}

	if (nelem > buflen)
		nelem = buflen;

	assert(begin + (nelem - 1) * skip >= 0);
	assert(begin + (nelem - 1) * skip < maxlen);

	if (var->type == GT3_TYPE_DOUBLE) {
		int i;
		double *ptr = var->data;

		ptr += begin;
		for (i = 0; i < nelem; i++)
			buf[i] = ptr[i * skip];
	} else {
		int i;
		float *ptr = var->data;

		ptr += begin;
		for (i = 0; i < nelem; i++)
			buf[i] = ptr[i * skip];
	}
	return nelem;
}


/*
 *  copy data stored in GT3_Varbuf into the float buffer.
 *  See also GT3_copyVarDouble().
 */
int
GT3_copyVarFloat(float *buf, size_t buflen,
				 const GT3_Varbuf *var, int begin, int skip)
{
	int maxlen = var->dimlen[0] * var->dimlen[1];
	int end, nelem;

	if (skip > 0) {
		begin = clip(begin, 0, maxlen);

		end = maxlen;
		nelem = (end - begin + (skip - 1)) / skip;
	} else if (skip < 0) {
		begin = clip(begin, -1, maxlen - 1);

		end = -1;
		nelem = (end - begin + (skip + 1)) / skip;
	} else {
		if (begin < 0 || begin >= maxlen)
			nelem = 0;
		else
			nelem = buflen;
	}

	if (nelem > buflen)
		nelem = buflen;

	assert(begin + (nelem - 1) * skip >= 0);
	assert(begin + (nelem - 1) * skip < maxlen);

	if (var->type == GT3_TYPE_DOUBLE) {
		int i;
		double *ptr = var->data;

		ptr += begin;
		for (i = 0; i < nelem; i++)
			buf[i] = (float)ptr[i * skip];
	} else {
		int i;
		float *ptr = var->data;

		ptr += begin;
		for (i = 0; i < nelem; i++)
			buf[i] = ptr[i * skip];
	}
	return nelem;
}


char *
GT3_getVarAttrStr(char *attr, int len, const GT3_Varbuf *var, const char *key)
{
	varbuf_status *stat = (varbuf_status *)var->stat_;

	return GT3_copyHeaderItem(attr, len, &stat->head, key);
}


int
GT3_getVarAttrInt(int *attr, const GT3_Varbuf *var, const char *key)
{
	varbuf_status *stat = (varbuf_status *)var->stat_;

	return GT3_decodeHeaderInt(attr, &stat->head, key);
}


int
GT3_getVarAttrDouble(double *attr, const GT3_Varbuf *var, const char *key)
{
	varbuf_status *stat = (varbuf_status *)var->stat_;

	return GT3_decodeHeaderDouble(attr, &stat->head, key);
}


/*
 *  replace a file pointer in Varbuf.
 */
int
GT3_reattachVarbuf(GT3_Varbuf *var, GT3_File *fp)
{
	return update_varbuf(var, fp);
}


#ifdef TEST
int
test(const char *path)
{
	GT3_File *fp;
	GT3_Varbuf *var;
	int i, j, k;
	double val;
	int dim1, dim2, dim3;

	if ((fp = GT3_open(path)) == NULL
		|| (var = GT3_getVarbuf(fp)) == NULL) {
		return -1;
	}

	while (!GT3_eof(fp)) {
		printf("**** %d\n", fp->curr);

		dim1 = fp->dimlen[0];
		dim2 = fp->dimlen[1];
		dim3 = fp->dimlen[2];
		for (k = 0; k < dim3; k++)
			for (j = 0; j < dim2; j++)
				for (i = 0; i < dim1; i++) {
					printf("%3d %3d %3d ", i, j, k);

					if (GT3_readVar(&val, var, i, j, k) < 0)
						printf("(NaN)\n");
					else
						printf("%20.8g\n", val);
				}

		if (GT3_next(fp) < 0)
			break;
	}
	GT3_freeVarbuf(var);
	GT3_close(fp);
	return 0;
}


int
main(int argc, char **argv)
{
	GT3_setPrintOnError(1);
	while (--argc > 0 && *++argv)
		test(*argv);

	return 0;
}
#endif
