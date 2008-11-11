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

#define RESERVE_SIZE (640*320)
#define RESERVE_NZ   256

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

static int read_UR4(GT3_Varbuf *var,  int, size_t, size_t nelem, FILE *fp);
static int read_UR8(GT3_Varbuf *var,  int, size_t, size_t nelem, FILE *fp);
static int read_URC1(GT3_Varbuf *var, int, size_t, size_t nelem, FILE *fp);
static int read_URC2(GT3_Varbuf *var, int, size_t, size_t nelem, FILE *fp);
static int read_URX(GT3_Varbuf *var,  int, size_t, size_t nelem, FILE *fp);
static int read_MR4(GT3_Varbuf *var,  int, size_t, size_t nelem, FILE *fp);
static int read_MR8(GT3_Varbuf *var,  int, size_t, size_t nelem, FILE *fp);
static int read_MRX(GT3_Varbuf *var,  int, size_t, size_t nelem, FILE *fp);

typedef int (*RFptr)(GT3_Varbuf *, int, size_t, size_t, FILE *);
static RFptr read_fptr[] = {
	read_UR4,
	read_URC2,
	read_URC1,
	read_UR8,
	read_URX,
	read_MR4,
	read_MR8,
	read_MRX,
	NULL
};

typedef void (*UNPACK_FUNC)(const unsigned *packed, int packed_len,
							double ref, int ne, int nd,
							double miss, float *data);


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
read_from_record(void *ptr, size_t skip, size_t nelem,
				 size_t size, FILE *fp)
{
	fort_size_t recsiz;			/* record size */
	off_t eor;
	size_t nelem_record;		/* # of elements in the record. */


	if (fread(&recsiz, sizeof(fort_size_t), 1, fp) != 1)
		return -1;
	if (IS_LITTLE_ENDIAN)
		reverse_words(&recsiz, 1);

	if (recsiz % size != 0)
		return -1;

	/* eor: end of record (position) */
	eor = ftello(fp) + recsiz + sizeof(fort_size_t);

	nelem_record = recsiz / size;

	if (skip > nelem_record)
		skip = nelem_record;
	if (nelem > nelem_record - skip)
		nelem = nelem_record - skip;

	if (nelem > 0) {
		if (skip != 0 && fseeko(fp, size * skip, SEEK_CUR) < 0)
			return -1;

		if (fread(ptr, size, nelem, fp) != nelem)
			return -1;
	}

	return fseeko(fp, eor, SEEK_SET);
}


/*
 *  read_words_from_record() reads words from a fortran-unformatted record,
 *  and stores them in 'ptr'.
 *
 *  WORD: 4-byte in size.
 */
static int
read_words_from_record(void *ptr, size_t skip, size_t nelem, FILE *fp)
{
	if (read_from_record(ptr, skip, nelem, 4, fp) < 0)
		return -1;

	if (IS_LITTLE_ENDIAN)
		reverse_words(ptr, nelem);

	return 0;
}

/*
 *  read_dwords_from_record() reads dwords from a fortran-unformatted record,
 *  and stores them in 'ptr'.
 *
 *  DWORD: 8-byte in size.
 */
static int
read_dwords_from_record(void *ptr, size_t skip, size_t nelem, FILE *fp)
{
	if (read_from_record(ptr, skip, nelem, 8, fp) < 0)
		return -1;

	if (IS_LITTLE_ENDIAN)
		reverse_dwords(ptr, nelem);

	return 0;
}


static int
read_UR4(GT3_Varbuf *var, int zpos, size_t skip, size_t nelem, FILE *fp)
{
	float *ptr;
	off_t off;

	off = var->fp->off
		+ GT3_HEADER_SIZE + 2 * sizeof(fort_size_t)
		+ sizeof(float) * (zpos * var->dimlen[0] * var->dimlen[1] + skip)
		+ sizeof(fort_size_t);

	if (fseeko(fp, off, SEEK_SET) < 0) {
		gt3_error(SYSERR, NULL);
		return -1;
	}

	assert(var->type == GT3_TYPE_FLOAT);
	assert(var->bufsize >= sizeof(float) * nelem);

	ptr = (float *)var->data;
	ptr += skip;

	if (xfread(ptr, sizeof(float), nelem, fp, var->fp->path) < 0)
		return -1;

	if (IS_LITTLE_ENDIAN)
		reverse_words(ptr, nelem);

	return 0;
}


static int
read_UR8(GT3_Varbuf *var, int zpos, size_t skip, size_t nelem, FILE *fp)
{
	double *ptr;
	off_t off;

	off = var->fp->off
		+ GT3_HEADER_SIZE + 2 * sizeof(fort_size_t)
		+ sizeof(double) * (zpos * var->dimlen[0] * var->dimlen[1] + skip)
		+ sizeof(fort_size_t);


	if (fseeko(fp, off, SEEK_SET) < 0) {
		gt3_error(SYSERR, NULL);
		return -1;
	}

	assert(var->type == GT3_TYPE_DOUBLE);
	ptr = (double *)var->data;
	ptr += skip;

	if (xfread(ptr, sizeof(double), nelem, fp, var->fp->path) < 0)
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
read_URCv(GT3_Varbuf *var, int zpos, size_t skip, size_t nelem, FILE *fp,
		  UNPACK_FUNC unpack_func)
{
	unsigned packed[1024];
	unsigned char pbuf[8 + 4 + 4 + 7 * sizeof(fort_size_t)];
	off_t off;
	double ref;
	int nd, ne;
	fort_size_t sizh;
	size_t num;
	int i;
	float *outp;

	off = var->fp->off
		+ GT3_HEADER_SIZE + 2 * sizeof(fort_size_t)
		+ (8 + 4 + 4
		   + 2 * var->dimlen[0] * var->dimlen[1]
		   + 8 * sizeof(fort_size_t)) * zpos;

	if (fseeko(fp, off, SEEK_SET) < 0) {
		gt3_error(SYSERR, NULL);
		return -1;
	}

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
read_URC1(GT3_Varbuf *var, int zpos, size_t skip, size_t nelem, FILE *fp)
{
	return read_URCv(var, zpos, skip, nelem, fp, urc1_unpack);
}


static int
read_URC2(GT3_Varbuf *var, int zpos, size_t skip, size_t nelem, FILE *fp)
{
	return read_URCv(var, zpos, skip, nelem, fp, urc2_unpack);
}


/*
 *  XXX: 'skip' and 'nelem' are ignored for now.
 *  read_URX() reads all data in a z-plane.
 */
static int
read_URX(GT3_Varbuf *var, int zpos, size_t skip, size_t nelem, FILE *fp)
{
#define URXBUFSIZ 1024
	uint32_t packed[32 * URXBUFSIZ];
	unsigned idata[32 * URXBUFSIZ];
	off_t off;
	double dma[2], scale;
	unsigned nbits, imiss;
	float *outp;
	size_t npack_per_read, ndata_per_read;
	size_t npack, ndata, nrest, nrest_packed;
	int i;

	/*
	 * XXX: read_URX() always reads all data in a z-plane.
	 * 'skip' and 'nelem' passed as an argument are ignored.
	 */
	nelem = (size_t)(var->dimlen[0] * var->dimlen[1]);
	nbits = (unsigned)var->fp->fmt >> GT3_FMT_MBIT;


	/*
	 *  read packing parameters for URX.
	 */
	off = var->fp->off + GT3_HEADER_SIZE + 2 * sizeof(fort_size_t);
	if (fseeko(fp, off, SEEK_SET) < 0)
		return -1;
	if (read_dwords_from_record(dma, 2 * zpos, 2, fp) < 0)
		return -1;

	nrest = nelem;
	nrest_packed = pack32_len(nrest, nbits);

	/*
	 *  skip to zpos.
	 */
	off = sizeof(fort_size_t) + 4 * zpos * nrest_packed;
	if (fseeko(fp, off, SEEK_CUR) < 0)
		return -1;

	/*
	 *  read packed DATA-BODY in zpos.
	 */
	imiss = (1U << nbits) - 1;
	scale = (imiss == 1) ? 0. : dma[1] / (imiss - 1);

	npack_per_read = URXBUFSIZ * nbits;
	ndata_per_read = 32 * URXBUFSIZ;

	outp = (float *)var->data;
	while (nrest > 0) {
		npack = nrest_packed > npack_per_read
			? npack_per_read
			: nrest_packed;

		if (fread(packed, 4, npack, fp) != npack)
			return -1;

		if (IS_LITTLE_ENDIAN)
			reverse_words(packed, npack);

		ndata = nrest > ndata_per_read
			? ndata_per_read
			: nrest;

		assert(npack == pack32_len(ndata, nbits));

		unpack_bits_from32(idata, ndata, packed, nbits);

		for (i = 0; i < ndata; i++)
			outp[i] = (idata[i] != imiss)
				? dma[0] + idata[i] * scale
				: var->miss;

		outp += ndata;
		nrest -= ndata;
		nrest_packed -= npack;
	}

	assert(nrest == 0 && nrest_packed == 0);
	return 0;
}


/*
 *  common to MR4 and MR8.
 */
static int
read_MRN_pre(void *temp,
			 GT3_Varbuf *var,
			 size_t size,		/* size of each data (4 or 8) */
			 int zpos, size_t skip, size_t nelem)
{
	GT3_Datamask *mask;
	size_t nread;
	int idx0;
	off_t off;

	mask = var->fp->mask;
	if (!mask && (mask = GT3_newMask()) == NULL)
		return -1;

	/*
	 *  load mask data.
	 */
	if (GT3_loadMask(mask, var->fp) != 0)
		return -1;
	var->fp->mask = mask;
	GT3_updateMaskIndex(mask);

	/*
	 *  seek to the begging of the data-body.
	 */
	idx0 = zpos * var->dimlen[0] * var->dimlen[1] + skip;
	off = var->fp->off + 6 * sizeof(fort_size_t)
		+ GT3_HEADER_SIZE		/* header */
		+ 4						/* NNN */
		+ 4 * ((mask->nelem + 31) / 32)	/* MASK */
		+ sizeof(fort_size_t)
		+ size * mask->index[idx0];

	if (fseeko(var->fp->fp, off, SEEK_SET) < 0)
		return -1;

	/*
	 *  nread: the # of MASK-ON elements.
	 */
	nread = mask->index[idx0 + nelem] - mask->index[idx0];
	assert(nread <= nelem);

	if (xfread(temp, size, nread, var->fp->fp, var->fp->path) < 0)
		return -1;

	return nread;
}


static int
read_MR4(GT3_Varbuf *var, int zpos, size_t skip, size_t nelem, FILE *fp)
{
	float temp_buf[RESERVE_SIZE];
	float *outp, *temp = NULL;
	int nread;
	int offnum;
	int i, n;

	temp = (float *)tiny_alloc(temp_buf,
							   sizeof(temp_buf),
							   sizeof(float) * nelem);
	if (!temp)
		return -1;

	if ((nread = read_MRN_pre(temp, var, sizeof(float),
							  zpos, skip, nelem)) < 0) {
		tiny_free(temp, temp_buf);
		return -1;
	}

	if (IS_LITTLE_ENDIAN)
		reverse_words(temp, nread);

	offnum = var->dimlen[0] * var->dimlen[1] * zpos + skip;
	outp = (float *)var->data;
	outp += skip;
	for (i = 0, n = 0; i < nelem; i++) {
		if (GT3_getMaskValue(var->fp->mask, offnum + i)) {
			outp[i] = temp[n];
			n++;
		} else
			outp[i] = (float)(var->miss);
	}
	assert(n == nread);

	tiny_free(temp, temp_buf);

	return 0;
}


static int
read_MR8(GT3_Varbuf *var, int zpos, size_t skip, size_t nelem, FILE *fp)
{
	double temp_buf[RESERVE_SIZE];
	double *outp, *temp = NULL;
	int nread;
	int offnum;
	int i, n;

	assert(var->type == GT3_TYPE_DOUBLE);

	temp = (double *)tiny_alloc(temp_buf,
							   sizeof(temp_buf),
							   sizeof(double) * nelem);
	if (!temp)
		return -1;

	if ((nread = read_MRN_pre(temp, var, sizeof(double),
							  zpos, skip, nelem)) < 0) {
		tiny_free(temp, temp_buf);
		return -1;
	}

	if (IS_LITTLE_ENDIAN)
		reverse_dwords(temp, nread);

	offnum = var->dimlen[0] * var->dimlen[1] * zpos + skip;
	outp = (double *)var->data;
	outp += skip;
	for (i = 0, n = 0; i < nelem; i++) {
		if (GT3_getMaskValue(var->fp->mask, offnum + i)) {
			outp[i] = temp[n];
			n++;
		} else
			outp[i] = var->miss;
	}
	assert(n == nread);

	tiny_free(temp, temp_buf);

	return 0;
}


static int
read_MRX(GT3_Varbuf *var, int zpos, size_t skip, size_t nelem, FILE *fp)
{
	off_t off;
	double dma[2], scale;
	GT3_Datamask *mask;
	unsigned nbit;
	size_t packed_len, skip2;
	unsigned imiss;
	int i, n;
	float *outp;
	int *nnn = NULL, temp_buf[RESERVE_NZ];
	uint32_t *packed = NULL, packed_buf[RESERVE_SIZE];
	unsigned *idata = NULL, idata_buf[RESERVE_SIZE];


	mask = var->fp->mask;
	if (!mask && (mask = GT3_newMask()) == NULL)
		return -1;

	/* read MASK. */
	if (GT3_loadMaskX(mask, zpos, var->fp) != 0)
		return -1;
	var->fp->mask = mask;


	if ((nnn = (int *)tiny_alloc(temp_buf,
								 sizeof(temp_buf),
								 sizeof(int) * var->dimlen[2])) == NULL)
		return -1;


	off = var->fp->off
		+ GT3_HEADER_SIZE + 2 * sizeof(fort_size_t)
		+ 4 + 2 * sizeof(fort_size_t);
	if (fseeko(fp, off, SEEK_SET) < 0)
		goto error;

	/* read NNN. */
	if (read_words_from_record(nnn, 0, var->dimlen[2], fp) < 0)
		goto error;

	/* skip IZLEN */
	if (read_words_from_record(NULL, 0, 0, fp) < 0)
		goto error;

	/* read DMA. */
	if (read_dwords_from_record(dma, 2 * zpos, 2, fp) < 0)
		goto error;

	/* skip MASK. */
	if (read_words_from_record(NULL, 0, 0, fp) < 0)
		goto error;

	/*
	 *  read DATA BODY (masked & packed).
	 */
	nbit = var->fp->fmt >> GT3_FMT_MBIT;
	for (skip2 = 0, i = 0; i < zpos; i++)
		skip2 += pack32_len(nnn[i], nbit);
	packed_len = pack32_len(nnn[zpos], nbit);

	if ((packed = (uint32_t *)
		 tiny_alloc(packed_buf,
					sizeof(packed_buf),
					sizeof(uint32_t) * packed_len)) == NULL
		|| read_words_from_record(packed,
								  skip2,
								  packed_len,
								  fp) < 0)
		goto error;

	/*
	 *  unpack masked & packed data.
	 */
	if ((idata = (unsigned *)
		 tiny_alloc(idata_buf,
					sizeof(idata_buf),
					sizeof(unsigned) * nnn[zpos])) == NULL)
		goto error;

	unpack_bits_from32(idata, nnn[zpos], packed, nbit);

	imiss = (1U << nbit) - 1;
	scale = (imiss == 1) ? 0. : dma[1] / (imiss - 1);

	outp = (float *)var->data;
	for (i = 0, n = 0; i < nelem; i++)
		if (GT3_getMaskValue(mask, i)) {
			outp[i] = (float)(dma[0] + idata[n] * scale);
			n++;
		} else
			outp[i] = (float)var->miss;

	assert(n == nnn[zpos]);

	tiny_free(idata, idata_buf);
	tiny_free(packed, packed_buf);
	tiny_free(nnn, temp_buf);
	return 0;

error:
	tiny_free(idata, idata_buf);
	tiny_free(packed, packed_buf);
	tiny_free(nnn, temp_buf);
	return -1;
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
	case GT3_FMT_MR8:
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
	int fmt;

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

	nelem = var->dimlen[0] * var->dimlen[1];
	fmt = (int)(var->fp->fmt & GT3_FMT_MASK);

	if (read_fptr[fmt](var, zpos, 0, nelem, var->fp->fp) < 0) {
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
	int i, fmt;
	int supported[] = {
		GT3_FMT_UR4,
		GT3_FMT_URC,
		GT3_FMT_URC1,
		GT3_FMT_UR8,
		GT3_FMT_MR4,
		GT3_FMT_MR8
	};

	update2_varbuf(var);
	if (   zpos < 0 || zpos >= var->dimlen[2]
		|| ypos < 0 || ypos >= var->dimlen[1]) {
		gt3_error(GT3_ERR_INDEX, "GT3_readVarZY(): y=%d, z=%d", ypos, zpos);
		return -1;
	}

	/*
	 *  In some format, use GT3_readVarZ().
	 */
	fmt = (int)(var->fp->fmt & GT3_FMT_MASK);
	for (i = 0; i < sizeof supported / sizeof(int); i++)
		if (fmt == supported[i])
			break;
	if (i == sizeof supported / sizeof(int))
		return GT3_readVarZ(var, zpos);

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

	skip  = ypos * var->dimlen[0];
	nelem = var->dimlen[0];
	if (read_fptr[fmt](var, zpos, skip, nelem, var->fp->fp) < 0) {
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
