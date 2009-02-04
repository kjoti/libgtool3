/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  write.c  -- writing data in GT3_Var.
 *
 */
#include "internal.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gtool3.h"
#include "myutils.h"
#include "talloc.h"
#include "int_pack.h"

#define BUFLEN  (IO_BUF_SIZE / 4)
#define BUFLEN8 (IO_BUF_SIZE / 8)

/* a pointer to urc1_packing or urc2_packing */
typedef void (*PACKING_FUNC)(uint32_t *, const float *, int, double,
							 double, double, double);


static uint32_t
maxval_uint32(const uint32_t *vals, size_t num)
{
	uint32_t mv = 0;
	size_t i;

	for (i = 0; i < num; i++)
		if (vals[i] > mv)
			mv = vals[i];
	return mv;
}


static int
write_record_sep(fort_size_t size, FILE *fp)
{
	if (IS_LITTLE_ENDIAN)
		reverse_words(&size, 1);

	if (fwrite(&size, sizeof(fort_size_t), 1, fp) != 1) {
		gt3_error(SYSERR, NULL);
		return -1;
	}
	return 0;
}


static int
write_data_into_record(const void *ptr,
					   size_t size,
					   size_t nelem,
					   void *(*reverse)(void *, int),
					   FILE *fp)
{
	char data[IO_BUF_SIZE];
	const char *ptr2;
	size_t nelem2, len, maxelems;


	/* HEADER */
	if (write_record_sep(size * nelem, fp) < 0)
		return -1;

	if (IS_LITTLE_ENDIAN && size > 1) {
		ptr2 = ptr;
		maxelems = sizeof data / size;
		nelem2 = nelem;

		while (nelem2 > 0) {
			len = nelem2 > maxelems ? maxelems : nelem2;

			memcpy(data, ptr2, size * len);
			reverse(data, len);

			if (fwrite(data, size, len, fp) != len) {
				gt3_error(SYSERR, NULL);
				return -1;
			}
			ptr2 += size * len;
			nelem2 -= len;
		}
	} else
		if (fwrite(ptr, size, nelem, fp) != nelem) {
			gt3_error(SYSERR, NULL);
			return -1;
		}

	/* TRAILER */
	if (write_record_sep(size * nelem, fp) < 0)
		return -1;

	return 0;
}


static int
write_words_into_record(const void *ptr, size_t nelem, FILE *fp)
{
	return write_data_into_record(ptr, 4, nelem, reverse_words, fp);
}

static int
write_dwords_into_record(const void *ptr, size_t nelem, FILE *fp)
{
	return write_data_into_record(ptr, 8, nelem, reverse_dwords, fp);
}


static int
write_ur4_via_double(const double *data, size_t len, FILE *fp)
{
	float buf[BUFLEN];
	int i, num;
	fort_size_t siz;

	siz = 4 * len;
	if (IS_LITTLE_ENDIAN)
		reverse_words(&siz, 1);

	if (fwrite(&siz, 4, 1, fp) != 1) {  /* HEADER */
		gt3_error(SYSERR, NULL);
		return -1;
	}

	while (len > 0) {
		num = (len > BUFLEN) ? BUFLEN : len;

		for (i = 0; i < num; i++)
			buf[i] = (float)data[i];

		if (IS_LITTLE_ENDIAN)
			reverse_words(buf, num);

		if (fwrite(buf, 4, num, fp) != num) {
			gt3_error(SYSERR, NULL);
			return -1;
		}

		data += num;
		len  -= num;
	}
	assert(len == 0);

	if (fwrite(&siz, 4, 1, fp) != 1) {  /* TRAILER */
		gt3_error(SYSERR, NULL);
		return -1;
	}
	return 0;
}


static int
write_ur4_via_float(const void *ptr, size_t len, FILE *fp)
{
	return write_words_into_record(ptr, len, fp);
}


static int
write_ur8_via_double(const void *ptr, size_t len, FILE *fp)
{
	return write_dwords_into_record(ptr, len, fp);
}


static int
write_ur8_via_float(const float *data, size_t nelems, FILE *fp)
{
	double copied[BUFLEN8];
	size_t ncopy, len;
	int i;

	if (write_record_sep(8 * nelems, fp) < 0)
		return -1;

	len = nelems;
	while (len > 0) {
		ncopy = (len > BUFLEN8) ? BUFLEN8 : len;

		for (i = 0; i < ncopy; i++)
			copied[i] = (double)data[i];

		if (IS_LITTLE_ENDIAN)
			reverse_dwords(copied, ncopy);

		if (fwrite(copied, 8, ncopy, fp) != ncopy) {
			gt3_error(SYSERR, NULL);
			return -1;
		}

		len -= ncopy;
		data += ncopy;
	}

	if (write_record_sep(8 * nelems, fp) < 0)
		return -1;

	return 0;
}


static int
write_urc_zslice(const float *data, int len, double miss,
				 PACKING_FUNC packing, FILE *fp)
{
	char siz4[] = { 0, 0, 0, 4 };
	char siz8[] = { 0, 0, 0, 8 };
	uint32_t packed[1024];
	fort_size_t siz;
	unsigned char parambuf[8 + 4 + 4 + 3 * 2 * 4];
	double rmin, ref, fac_e, fac_d;
	int ne, nd;
	int len_pack;
	int maxelem;

	calc_urc_param(data, len, miss, &rmin, &fac_e, &fac_d, &ne, &nd);

	/*
	 *  three packing parameters (REF, ND, and NE)
	 */
	ref = rmin * fac_d;
	memcpy(parambuf,       siz8, 4); /* REF head */
	memcpy(parambuf + 12,  siz8, 4); /* REF tail */
	memcpy(parambuf + 16,  siz4, 4); /* ND  head */
	memcpy(parambuf + 24,  siz4, 4); /* ND  tail */
	memcpy(parambuf + 28,  siz4, 4); /* NE  head */
	memcpy(parambuf + 36,  siz4, 4); /* NE  tail */
	if (IS_LITTLE_ENDIAN) {
		memcpy(parambuf +  4, reverse_dwords(&ref, 1), 8);
		memcpy(parambuf + 20, reverse_words(&nd,   1), 4);
		memcpy(parambuf + 32, reverse_words(&ne,   1), 4);
	} else {
		memcpy(parambuf +  4, &ref, 8);
		memcpy(parambuf + 20, &nd,  4);
		memcpy(parambuf + 32, &ne,  4);
	}
	if (fwrite(parambuf, 1, sizeof parambuf, fp) != sizeof parambuf) {
		gt3_error(SYSERR, NULL);
		return -1;
	}

	/* header of data body */
	siz = 2 * len;
	if (IS_LITTLE_ENDIAN) {
		reverse_words(&siz, 1);
	}
	if (fwrite(&siz, 1, 4, fp) != 4) {
		gt3_error(SYSERR, NULL);
		return -1;
	}

	/*
	 *  data body
	 */
	maxelem = sizeof packed / 2;
	while (len > 0) {
		len_pack = (len > maxelem) ? maxelem : len;

		/*
		 *  2-byte packing
		 */
		packing(packed, data, len_pack, miss, rmin, fac_e, fac_d);

		if (IS_LITTLE_ENDIAN) {
			reverse_words(packed, (len_pack + 1) / 2);
		}

		/* write packed data */
		if (fwrite(packed, 2, len_pack, fp) != len_pack) {
			gt3_error(SYSERR, NULL);
			return -1;
		}

		data += len_pack;
		len  -= len_pack;
	}

	/* trailer */
	if (fwrite(&siz, 1, 4, fp) != 4) {
		gt3_error(SYSERR, NULL);
		return -1;
	}

	return 0;
}


static int
write_urc_via_float(const float *data, int len, int nz, double miss,
					PACKING_FUNC packing, FILE *fp)
{
	int i;

	for (i = 0; i < nz; i++) {
		write_urc_zslice(data, len, miss, packing, fp);
		data += len;
	}
	return 0;
}


static int
write_urc_via_double(const double *input, int len, int nz, double miss,
					 PACKING_FUNC packing, FILE *fp)
{
	int i, n;
	float *data;

	if ((data = (float *)malloc(sizeof(float) * len)) == NULL) {
		gt3_error(SYSERR, NULL);
		return -1;
	}
	for (i = 0; i < nz; i++) {
		for (n = 0; n < len; n++)
			data[n] = (float)input[n];

		write_urc_zslice(data, len, miss, packing, fp);
		input += len;
	}
	free(data);
	return 0;
}


static void
get_urx_parameterf(double *dma,
				   const float *data, size_t nelem, double miss)
{
	int n, x;
	float missf = (float)missf;

	n = idx_min_float(data, nelem, &missf);

	if (n < 0) {
		dma[0] = 0.;
		dma[1] = 0.;
	} else {
		x = idx_max_float(data, nelem, &missf);

		assert(x >= 0);

		dma[0] = data[n];
		dma[1] = data[x] - data[n];
	}
}


static void
get_urx_parameter(double *dma,
				  const double *data, size_t nelem, double miss)
{
	int n, x;

	n = idx_min_double(data, nelem, &miss);

	if (n < 0) {
		dma[0] = 0.;
		dma[1] = 0.;
	} else {
		x = idx_max_double(data, nelem, &miss);

		assert(x >= 0);

		dma[0] = data[n];
		dma[1] = data[x] - data[n];
	}
}


static int
write_urx(const void *ptr,
		  size_t size,			/* 4(float) or 8(double) */
		  size_t zelem,			/* # of elements in a z-plane */
		  size_t nz,			/* # of z-planes */
		  int nbits, double miss,
		  FILE *fp)
{
	double dma_buf[256];
	double *dma = dma_buf;
	double scale0;
	unsigned imiss;
	size_t nelems, len, plen, packed_len;
	int i;
	const char *ptr2;
#define URXBUFSIZ (32 * 1024)
	unsigned idata[URXBUFSIZ];
	uint32_t packed[URXBUFSIZ];


	assert(URXBUFSIZ % 32 == 0);

	if ((dma = (double *)
		 tiny_alloc(dma_buf,
					sizeof dma_buf,
					2 * nz * sizeof(double))) == NULL)
		goto error;

	/*
	 *  determine scaling parameters (auto-scaling)
	 */
	if (size == 4) {
		const float *data = ptr;

		for (i = 0; i < nz; i++)
			get_urx_parameterf(dma + 2 * i,
							   data + i * zelem,
							   zelem, miss);
	} else {
		const double *data = ptr;

		for (i = 0; i < nz; i++)
			get_urx_parameter(dma + 2 * i,
							  data + i * zelem,
							  zelem, miss);
	}

	/*
	 *  write scaling parameters
	 */
	if (write_dwords_into_record(dma, 2 * nz, fp) < 0)
		goto error;

	imiss = (1U << nbits) - 1;
	scale0 = (imiss == 1) ? 1. : 1. / (imiss - 1);
	packed_len = pack32_len(zelem, nbits);

	/*
	 *  write a header of data-body.
	 */
	if (write_record_sep(4 * packed_len * nz, fp) < 0)
		goto error;

	/*
	 *  write data-body (packed)
	 */
	for (i = 0; i < nz; i++) {
		ptr2 = (const char *)ptr + i *zelem * size;
		nelems = zelem;

		while (nelems > 0) {
			len = (nelems > URXBUFSIZ) ? URXBUFSIZ : nelems;

			if (size == 4)
				scalingf(idata,
						 (float *)ptr2,
						 len,
						 dma[2*i], dma[1+2*i] * scale0,
						 imiss, miss);
			else
				scaling(idata,
						(double *)ptr2,
						len,
						dma[2*i], dma[1+2*i] * scale0,
						imiss, miss);

			plen = pack_bits_into32(packed, idata, len, nbits);

			if (IS_LITTLE_ENDIAN)
				reverse_words(packed, plen);

			if (fwrite(packed, 4, plen, fp) != plen)
				goto error;

			ptr2 += len * size;
			nelems -= len;
		}
	}

	/* write a trailer of data-body */
	if (write_record_sep(4 * packed_len * nz, fp) < 0)
		goto error;

	tiny_free(dma, dma_buf);
	return 0;

error:
	gt3_error(SYSERR, NULL);
	tiny_free(dma, dma_buf);
	return -1;
}


static int
write_urx_via_double(const void *ptr,
					 size_t zelem, size_t nz,
					 int nbits, double miss, FILE *fp)
{
	return write_urx(ptr, sizeof(double), zelem, nz, nbits, miss, fp);
}

static int
write_urx_via_float(const void *ptr,
					size_t zelem, size_t nz,
					int nbits, double miss, FILE *fp)
{
	return write_urx(ptr, sizeof(float), zelem, nz, nbits, miss, fp);
}


/*
 *  some tricky staff to support both 'double' and 'float'.
 */


static unsigned
masked_count(const void *ptr, size_t size, size_t nelems, double miss)
{
	unsigned cnt;
	int i;

	if (size == 4) {
		const float *data = ptr;
		float missf = (float)miss;

		for (cnt = 0, i = 0; i < nelems; i++)
			if (data[i] != missf)
				cnt++;
	} else {
		const double *data = ptr;

		for (cnt = 0, i = 0; i < nelems; i++)
			if (data[i] != miss)
				cnt++;
	}
	return cnt;
}


static size_t
masked_copyf(size_t *nread,
			 float *dest,
			 const void *srcptr,
			 size_t size,		/* 4 or 8 */
			 size_t destlen, size_t srclen,
			 double miss)
{
	size_t cnt;
	int i;

	if (size == 4) {
		const float *data = srcptr;
		float missf = (float)miss;

		for (cnt = 0, i = 0; i < srclen && cnt < destlen; i++)
			if (data[i] != missf) {
				dest[cnt] = data[i];
				cnt++;
			}
	} else {
		const double *data = srcptr;

		for (cnt = 0, i = 0; i < srclen && cnt < destlen; i++)
			if (data[i] != miss) {
				dest[cnt] = (float)data[i];
				cnt++;
			}
	}

	*nread = i;
	return cnt;
}


static size_t
masked_copy(size_t *nread,
			double *dest,
			const void *srcptr,
			size_t size,		/* 4 or 8 */
			size_t destlen, size_t srclen,
			double miss)
{
	size_t cnt;
	int i;

	if (size == 4) {
		const float *data = srcptr;
		float missf = (float)miss;

		for (cnt = 0, i = 0; i < srclen && cnt < destlen; i++)
			if (data[i] != missf) {
				dest[cnt] = data[i];
				cnt++;
			}
	} else {
		const double *data = srcptr;

		for (cnt = 0, i = 0; i < srclen && cnt < destlen; i++)
			if (data[i] != miss) {
				dest[cnt] = data[i];
				cnt++;
			}
	}

	*nread = i;
	return cnt;
}


static void
get_flag_for_mask(unsigned *flag,
				  size_t len,
				  const void *ptr,
				  size_t size, double miss)
{
	int i;

	if (size == 4) {
		const float *data = ptr;
		float missf = (float)miss;

		for (i = 0; i < len; i++)
			flag[i] = (data[i] != missf) ? 1 : 0;
	} else {
		const double *data = ptr;

		for (i = 0; i < len; i++)
			flag[i] = (data[i] != miss) ? 1 : 0;
	}
}


/*
 *  write MASK (common to MR4, MR8, MRX).
 */
static int
write_mask(const void *ptr2,
		   size_t size, /* 4(float) or 8(double) */
		   size_t nelems, size_t nsets,
		   double miss, FILE *fp)
{
	uint32_t mask[BUFLEN];
	unsigned flag[32 * BUFLEN];
	size_t num, masklen, len, mlen;
	int n;
	const char *ptr = ptr2;

	masklen = pack32_len(nelems, 1);
	if (write_record_sep(4 * masklen * nsets, fp) < 0)
		return -1;

	for (n = 0; n < nsets; n++) {
		num = nelems;

		while (num > 0) {
			len = num > 32 * BUFLEN ? 32 * BUFLEN : num;

			get_flag_for_mask(flag, len, ptr, size, miss);

			mlen = pack_bits_into32(mask, flag, len, 1);
			assert(mlen > 0 && mlen <= BUFLEN);

			if (IS_LITTLE_ENDIAN)
				reverse_words(mask, mlen);

			if (fwrite(mask, 4, mlen, fp) != mlen) {
				gt3_error(SYSERR, NULL);
				return -1;
			}
			num -= len;
			ptr += len * size;
		}
	}

	if (write_record_sep(4 * masklen * nsets, fp) < 0)
		return -1;

	return 0;
}


static int
write_mr4(const void *ptr2,
		  size_t size,			/* ptr2: 4(float) or 8(double) */
		  size_t nelems, double miss, FILE *fp)
{
	const char *ptr;
	uint32_t cnt;
	size_t ncopy, nread;
	float copied[BUFLEN];

	/*
	 *  write the # of not-missing value.
	 */
	cnt = (uint32_t)masked_count(ptr2, size, nelems, miss);
	if (write_words_into_record(&cnt, 1, fp) < 0)
		return -1;

	/*
	 *  write MASK.
	 */
	if (write_mask(ptr2, size, nelems, 1, miss, fp) < 0)
		return -1;

	/*
	 *  write DATA-BODY.
	 */
	if (write_record_sep(sizeof(float) * cnt, fp) < 0)
		return -1;

	ptr = ptr2;
	while (nelems > 0) {
		ncopy = masked_copyf(&nread, copied, ptr, size, BUFLEN, nelems, miss);

		if (IS_LITTLE_ENDIAN)
			reverse_words(copied, ncopy);

		if (fwrite(copied, sizeof(float), ncopy, fp) != ncopy) {
			gt3_error(SYSERR, NULL);
			return -1;
		}

		nelems -= nread;
		ptr += nread * size;
	}

	if (write_record_sep(sizeof(float) * cnt, fp) < 0)
		return -1;
	return 0;
}


static int
write_mr8(const void *ptr2,
		  size_t size,			/* ptr2: 4(float) or 8(double) */
		  size_t nelems, double miss, FILE *fp)
{
	const char *ptr;
	uint32_t cnt;
	size_t ncopy, nread;
	double copied[BUFLEN];


	/*
	 *  write the # of not-missing value.
	 */
	cnt = (uint32_t)masked_count(ptr2, size, nelems, miss);
	if (write_words_into_record(&cnt, 1, fp) < 0)
		return -1;

	/*
	 *  write MASK.
	 */
	if (write_mask(ptr2, size, nelems, 1, miss, fp) < 0)
		return -1;

	/*
	 *  write DATA-BODY.
	 */
	if (write_record_sep(sizeof(double) * cnt, fp) < 0)
		return -1;

	ptr = ptr2;
	while (nelems > 0) {
		ncopy = masked_copy(&nread, copied, ptr, size, BUFLEN, nelems, miss);

		if (IS_LITTLE_ENDIAN)
			reverse_dwords(copied, ncopy);

		if (fwrite(copied, sizeof(double), ncopy, fp) != ncopy) {
			gt3_error(SYSERR, NULL);
			return -1;
		}

		nelems -= nread;
		ptr += nread * size;
	}

	if (write_record_sep(sizeof(double) * cnt, fp) < 0)
		return -1;
	return 0;
}


static int
write_mr4_via_double(const double *data, size_t nelems, double miss, FILE *fp)
{
	return write_mr4(data, sizeof(double), nelems, miss, fp);
}

static int
write_mr4_via_float(const float *data, size_t nelems, double miss, FILE *fp)
{
	return write_mr4(data, sizeof(float), nelems, miss, fp);
}

static int
write_mr8_via_double(const double *data, size_t nelems, double miss, FILE *fp)
{
	return write_mr8(data, sizeof(double), nelems, miss, fp);
}

static int
write_mr8_via_float(const float *data, size_t nelems, double miss, FILE *fp)
{
	return write_mr8(data, sizeof(float), nelems, miss, fp);
}


static int
write_mrx(const void *ptr2,
		  size_t size,			/* 4 or 8 */
		  size_t zelems, size_t nz,
		  int nbits, double miss,
		  FILE *fp)
{
	const char *ptr = ptr2;
	uint32_t cnt_buf[128];
	uint32_t plen_buf[128];
	double dma_buf[256];
	uint32_t *cnt = cnt_buf;
	uint32_t *plen = plen_buf;
	double *dma = dma_buf;
	uint32_t plen_all;
	unsigned imiss;
	double scale0;
	unsigned i;


	if ((cnt = (uint32_t *)
		 tiny_alloc(cnt_buf,
					sizeof cnt_buf,
					sizeof(uint32_t) * nz)) == NULL
		|| (plen = (uint32_t *)
			tiny_alloc(plen_buf,
					   sizeof plen_buf,
					   sizeof(uint32_t) * nz)) == NULL
		|| (dma = (double *)
			tiny_alloc(dma_buf,
					   sizeof dma_buf,
					   sizeof(double) * 2 * nz)) == NULL)
		goto error;

	for (ptr = ptr2, i = 0; i < nz; i++, ptr += zelems * size) {
		cnt[i] = masked_count(ptr, size, zelems, miss);

		plen[i] = (uint32_t)pack32_len(cnt[i], nbits);

		if (size == 4)
			get_urx_parameterf(dma + 2 * i, (float *)ptr, zelems, miss);
		else
			get_urx_parameter(dma + 2 * i, (double *)ptr, zelems, miss);
	}

	for (plen_all = 0, i = 0; i < nz; i++)
		plen_all += plen[i];

	if (write_words_into_record(&plen_all, 1, fp) < 0
		|| write_words_into_record(cnt, nz, fp) < 0
		|| write_words_into_record(plen, nz, fp) < 0
		|| write_dwords_into_record(dma, 2 * nz, fp) < 0
		|| write_mask(ptr2, size, zelems, nz, miss, fp) < 0)
		goto error;


	imiss = (1U << nbits) - 1;
	scale0 = (imiss == 1) ? 1. : 1. / (imiss - 1);

	/*
	 *  write packed array.
	 */
	if (write_record_sep(4 * plen_all, fp) < 0)
		goto error;
	{
		size_t ncopied, len;
		unsigned idata_buf[32 * 1024];
		uint32_t packed_buf[32 * 1024];
		unsigned *idata = idata_buf;
		uint32_t *packed = packed_buf;

		if ((idata = tiny_alloc(
				 idata_buf,
				 sizeof idata_buf,
				 sizeof(unsigned) * maxval_uint32(cnt, nz))) == NULL
			|| (packed = tiny_alloc(
					packed_buf,
					sizeof packed_buf,
					sizeof(uint32_t) * maxval_uint32(plen, nz))) == NULL) {

			tiny_free(idata, idata_buf);
			tiny_free(packed, packed_buf);
			goto error;
		}

		for (i = 0; i < nz; i++) {
			ptr = (const char *)ptr2 + i * zelems * size;

			if (size == 4)
				ncopied = masked_scalingf(idata,
										  (float *)ptr,
										  zelems,
										  dma[2*i], dma[1+2*i] * scale0,
										  imiss, miss);
			else
				ncopied = masked_scaling(idata,
										 (double *)ptr,
										 zelems,
										 dma[2*i], dma[1+2*i] * scale0,
										 imiss, miss);

			assert(ncopied == cnt[i]);
			len = pack_bits_into32(packed, idata, ncopied, nbits);
			if (IS_LITTLE_ENDIAN)
				reverse_words(packed, len);

			if (fwrite(packed, 4, len, fp) != len)
				goto error;
		}

		tiny_free(idata, idata_buf);
		tiny_free(packed, packed_buf);
	}
	if (write_record_sep(4 * plen_all, fp) < 0)
		goto error;

	tiny_free(dma, dma_buf);
	tiny_free(plen, plen_buf);
	tiny_free(cnt, cnt_buf);
	return 0;

error:
	gt3_error(SYSERR, NULL);
	tiny_free(dma, dma_buf);
	tiny_free(plen, plen_buf);
	tiny_free(cnt, cnt_buf);
	return -1;
}


static int
write_mrx_via_double(const void *ptr,
					 size_t zelems, size_t nz,
					 int nbits, double miss,
					 FILE *fp)
{
	return write_mrx(ptr, sizeof(double), zelems, nz, nbits, miss, fp);
}


static int
write_mrx_via_float(const void *ptr,
					 size_t zelems, size_t nz,
					 int nbits, double miss,
					 FILE *fp)
{
	return write_mrx(ptr, sizeof(float), zelems, nz, nbits, miss, fp);
}


/*
 *  GT3_output_format() gives actual output format from user-specified name.
 */
int
GT3_output_format(char *dfmt, const char *str)
{
	int fmt;

	if (strcmp(str, "URC1") == 0)
		fmt = GT3_FMT_URC1;		/* deprecated format */
	else if (strcmp(str, "URC") == 0)
		/*
		 *  "URC" specified by user is treated as "URC2".
		 */
		fmt = GT3_FMT_URC;
	else
		fmt = GT3_format(str);

	if (fmt < 0)
		return -1;

	GT3_format_string(dfmt, fmt);
	return fmt;
}


/*
 *  GT3_write() writes data into a stream.
 *
 *  ptr:    a pointer to data.
 *  type:   element type (GT3_TYPE_FLOAT or GT3_TYPE_DOUBLE)
 *  nx:     data length for X-dimension.
 *  ny:     data length fo  Y-dimension.
 *  nz:     data length for Z-dimension.
 *  headin: a pointer to header.
 *  dfmt:   format name (if NULL is specified, UR4 or UR8  is selected)
 */
int
GT3_write(const void *ptr, int type,
		  int nx, int ny, int nz,
		  const GT3_HEADER *headin, const char *dfmt, FILE *fp)
{
	char fmtstr[17];
	const char *astr[] = { "ASTR1", "ASTR2", "ASTR3" };
	const char *aend[] = { "AEND1", "AEND2", "AEND3" };
	int str, end, i, dim[3];
	GT3_HEADER head;
	int fmt, rval = -1;
	double miss = -999.0;		/* -999.0: default value */
	size_t asize, zsize;
	int nbits;


	/* parameter check */
	if (ptr == NULL) {
		gt3_error(GT3_ERR_CALL, "GT3_write(): null pointer passed");
		return -1;
	}
	if (nx < 1 || ny < 1 || nz < 1) {
		gt3_error(GT3_ERR_CALL, "GT3_write(): %d %d %d", nx, ny, nz);
		return -1;
	}

	if (type != GT3_TYPE_DOUBLE
		&& type != GT3_TYPE_FLOAT) {
		gt3_error(GT3_ERR_CALL, "GT3_write(): unknown datatype");
		return -1;
	}

	if (!dfmt) {
		if (type == GT3_TYPE_FLOAT) {
			fmt = GT3_FMT_UR4;
			strcpy(fmtstr, "UR4");
		} else {
			fmt = GT3_FMT_UR8;
			strcpy(fmtstr, "UR8");
		}
	} else
		if ((fmt = GT3_output_format(fmtstr, dfmt)) < 0)
			return -1;

	/*
	 *  copy the gtool3-header and modify it.
	 */
	GT3_copyHeader(&head, headin);
	GT3_setHeaderString(&head, "DFMT", fmtstr);
	GT3_setHeaderInt(&head, "SIZE", nx * ny * nz);

	/*
	 *  set "AEND1", "AEND2", and "AEND3".
	 *  "ASTR[1-3]" is determined by 'headin'.
	 */
	dim[0] = nx;
	dim[1] = ny;
	dim[2] = nz;
	for (i = 0; i < 3; i++) {
		if (GT3_decodeHeaderInt(&str, &head, astr[i]) < 0) {
			str = 1;
			GT3_setHeaderInt(&head, astr[i], str);
		}
		end = str - 1 + dim[i];
		GT3_setHeaderInt(&head, aend[i], end);
	}

	/*
	 *  write gtool header.
	 */
	if (write_data_into_record(&head.h, 1, GT3_HEADER_SIZE, NULL, fp) < 0)
		return -1;

	/*
	 *  write data-body.
	 */
	asize = nx * ny * nz;
	zsize = nx * ny;
	GT3_decodeHeaderDouble(&miss, &head, "MISS");
	nbits = (unsigned)fmt >> GT3_FMT_MBIT;

	if (type == GT3_TYPE_DOUBLE)
		switch (fmt & GT3_FMT_MASK) {
		case GT3_FMT_UR4:
			rval = write_ur4_via_double(ptr, asize, fp);
			break;
		case GT3_FMT_URC:
			rval = write_urc_via_double(ptr, zsize, nz, miss,
									   urc2_packing, fp);
			break;
		case GT3_FMT_URC1:
			rval = write_urc_via_double(ptr, zsize, nz, miss,
										urc1_packing, fp);
			break;
		case GT3_FMT_UR8:
			rval = write_ur8_via_double(ptr, asize, fp);
			break;
		case GT3_FMT_URX:
			rval = write_urx_via_double(ptr, zsize, nz, nbits, miss, fp);
			break;
		case GT3_FMT_MR4:
			rval = write_mr4_via_double(ptr, asize, miss, fp);
			break;
		case GT3_FMT_MR8:
			rval = write_mr8_via_double(ptr, asize, miss, fp);
			break;
		case GT3_FMT_MRX:
			rval = write_mrx_via_double(ptr, zsize, nz, nbits, miss, fp);
			break;

		}
	else
		switch (fmt & GT3_FMT_MASK) {
		case GT3_FMT_UR4:
			rval = write_ur4_via_float(ptr, asize, fp);
			break;
		case GT3_FMT_URC:
			rval = write_urc_via_float(ptr, zsize, nz, miss,
									   urc2_packing, fp);
			break;
		case GT3_FMT_URC1:
			rval = write_urc_via_float(ptr, zsize, nz, miss,
									   urc1_packing, fp);
			break;
		case GT3_FMT_UR8:
			rval = write_ur8_via_float(ptr, asize, fp);
			break;
		case GT3_FMT_URX:
			rval = write_urx_via_float(ptr, zsize, nz, nbits, miss, fp);
			break;
		case GT3_FMT_MR4:
			rval = write_mr4_via_float(ptr, asize, miss, fp);
			break;
		case GT3_FMT_MR8:
			rval = write_mr8_via_float(ptr, asize, miss, fp);
			break;
		case GT3_FMT_MRX:
			rval = write_mrx_via_float(ptr, zsize, nz, nbits, miss, fp);
			break;
		}

	fflush(fp);
	return rval;
}


#ifdef TEST_MAIN

void
test2(void)
{
	double miss = 1e20;
	float dest[4];
	double src1[8];
	float src2[8];
	size_t nread, ncopied;
	size_t srclen, destlen;
	int i;

	srclen = sizeof src1 / sizeof src1[0];
	destlen = sizeof dest / sizeof dest[0];

	/*
	 *  part 1
	 */
	for (i = 0; i < srclen; i++) {
		src1[i] = miss;
		src2[i] = (float)miss;
	}

	assert(masked_count(src1, sizeof(double), srclen, miss) == 0);
	assert(masked_count(src2, sizeof(float), srclen, miss) == 0);

	ncopied = masked_copyf(&nread,
						   dest,
						   src1, sizeof(double),
						   destlen, srclen, miss);
	assert(ncopied == 0 && nread == srclen);

	ncopied = masked_copyf(&nread,
						   dest,
						   src2, sizeof(float),
						   destlen, srclen, miss);
	assert(ncopied == 0 && nread == srclen);


	/*
	 *  part 2
	 */
	for (i = 0; i < srclen; i++) {
		src1[i] = (double)i;
		src2[i] = (float)i;
	}

	assert(masked_count(src1, sizeof(double), srclen, miss) == srclen);
	assert(masked_count(src2, sizeof(float), srclen, miss) == srclen);

	ncopied = masked_copyf(&nread,
						   dest,
						   src1, sizeof(double),
						   destlen, srclen, miss);

	assert(ncopied == nread);
	assert(ncopied <= destlen);
	assert(nread   <= srclen);
	assert(dest[0] == 0.f && dest[1] == 1.f && dest[2] == 2.f);

	ncopied = masked_copyf(&nread,
						   dest,
						   src2, sizeof(float),
						   destlen, srclen, miss);

	assert(ncopied == nread);
	assert(ncopied <= destlen);
	assert(nread   <= srclen);
	assert(dest[0] == 0.f && dest[1] == 1.f && dest[2] == 2.f);
}


int
main(int argc, char **argv)
{
	char dfmt[17];
	int fmt;

	assert(sizeof(float) == 4);
	assert(sizeof(double) == 8);
	assert(sizeof(fort_size_t) == 4);

	fmt = GT3_output_format(dfmt, "URC");
	assert(fmt == GT3_FMT_URC);
	assert(strcmp(dfmt, "URC2") == 0);

	fmt = GT3_output_format(dfmt, "URC1");
	assert(fmt == GT3_FMT_URC1);
	assert(strcmp(dfmt, "URC") == 0);

	fmt = GT3_output_format(dfmt, "MR8");
	assert(fmt == GT3_FMT_MR8);
	assert(strcmp(dfmt, "MR8") == 0);

	fmt = GT3_output_format(dfmt, "URX12");
	assert((fmt & GT3_FMT_MASK) == GT3_FMT_URX);
	assert((fmt >> GT3_FMT_MBIT) == 12);
	assert(strcmp(dfmt, "URX12") == 0);

	test2();
	return 0;
}
#endif
