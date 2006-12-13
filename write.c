/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  write.c  -- writing data in GT3_Var.
 *
 *  $Date: 2006/11/07 00:53:11 $
 */
#include "internal.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gtool3.h"

#define BUFLEN  (IO_BUF_SIZE / 4)
#define BUFLEN8 (IO_BUF_SIZE / 8)

/* a pointer to urc1_packing or urc2_packing */
typedef void (*PACKING_FUNC)(uint32_t *, const float *, int, double,
							 double, double, double);


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


static int
write_ur4_via_double(const void *ptr, size_t len, FILE *fp)
{
	const double *data = ptr;
	float buf[BUFLEN];
	int i, num;
	size_t siz;

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
	size_t siz;

	siz = 4 * len;
	if (IS_LITTLE_ENDIAN)
		reverse_words(&siz, 1);

	if (fwrite(&siz, 4, 1, fp) != 1) {  /* HEADER */
		gt3_error(SYSERR, NULL);
		return -1;
	}
	if (!IS_LITTLE_ENDIAN) {
		if (fwrite(ptr, 4, len, fp) != -1) {
			gt3_error(SYSERR, NULL);
			return -1;
		}
	} else {
		const float *data = ptr;
		float buf[BUFLEN];
		int i, num;

		while (len > 0) {
			num = (len > BUFLEN) ? BUFLEN : len;

			for (i = 0; i < num; i++)
				buf[i] = data[i];

			reverse_words(buf, num);
			if (fwrite(buf, 4, num, fp) != num) {
				gt3_error(SYSERR, NULL);
				return -1;
			}
			data += num;
			len  -= num;
		}
		assert(len == 0);
	}
	if (fwrite(&siz, 4, 1, fp) != 1) {  /* HEADER */
		gt3_error(SYSERR, NULL);
		return -1;
	}
	return 0;
}


static int
write_ur8(const void *ptr, size_t len, FILE *fp)
{
	size_t siz;

	siz = 8 * len;
	if (IS_LITTLE_ENDIAN)
		reverse_words(&siz, 1);

	if (fwrite(&siz, 4, 1, fp) != 1) {  /* HEADER */
		gt3_error(SYSERR, NULL);
		return -1;
	}
	if (!IS_LITTLE_ENDIAN) {
		if (fwrite(ptr, 8, len, fp) != len) {
			gt3_error(SYSERR, NULL);
			return -1;
		}
	} else {
		const double *data = ptr;
		double buf[BUFLEN8];
		int i, num;

		while (len > 0) {
			num = (len > BUFLEN8) ? BUFLEN8 : len;

			for (i = 0; i < num; i++)
				buf[i] = data[i];

			reverse_dwords(buf, num);
			if (fwrite(buf, 8, num, fp) != num) {
				gt3_error(SYSERR, NULL);
				return -1;
			}
			data += num;
			len  -= num;
		}
		assert(len == 0);
	}
	if (fwrite(&siz, 4, 1, fp) != 1) {  /* HEADER */
		gt3_error(SYSERR, NULL);
		return -1;
	}
	return 0;
}


static int
write_urc_zslice(const float *data, int len, double miss,
				 PACKING_FUNC packing, FILE *fp)
{
	char siz4[] = { 0, 0, 0, 4 };
	char siz8[] = { 0, 0, 0, 8 };
	uint32_t packed[1024], siz;
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
write_urc_via_float(const void *ptr, int len, int nz, double miss,
					PACKING_FUNC packing, FILE *fp)
{
	const float *data = ptr;
	int i;

	assert(len % nz == 0);
	len /= nz;

	for (i = 0; i < nz; i++) {
		write_urc_zslice(data, len, miss, packing, fp);
		data += len;
	}
	return 0;
}


static int
write_urc_via_double(const void *ptr, int len, int nz, double miss,
					 PACKING_FUNC packing, FILE *fp)
{
	const double *input = ptr;
	int i, n;
	float *data;

	assert(len % nz == 0);
	len /= nz;

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


static int
write_header(const GT3_HEADER *head, FILE *fp)
{
	unsigned char siz[] = { 0, 0, 4, 0 };

	if (fwrite(siz, 1, 4, fp) != 4
		|| fwrite(head->h, 1, GT3_HEADER_SIZE, fp) != GT3_HEADER_SIZE
		|| fwrite(siz, 1, 4, fp) != 4) {
		gt3_error(SYSERR, NULL);
		return -1;
	}
	return 0;
}


static int
default_format(int type)
{
	int fmt;

	switch (type) {
	case GT3_TYPE_FLOAT:
		fmt = GT3_FMT_UR4;
		break;

	case GT3_TYPE_DOUBLE:
		fmt = GT3_FMT_UR8;
		break;

	default:
		fmt = -1;
		break;
	}
	return fmt;
}


/*
 *  GT3_write() writes data into a stream.
 *
 *  ptr:    a pointer to data.
 *  size:   size of each element (4 for float, 8 for double)
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
	struct { const char *key; int val; } dict[] = {
		{ "UR4",  GT3_FMT_UR4  },
		{ "URC",  GT3_FMT_URC  },
		{ "URC1", GT3_FMT_URC1 },
		{ "UR8",  GT3_FMT_UR8  },
		{ "URC2", GT3_FMT_URC  }
	};
	const char *rdict[] = { "UR4", "URC2", "URC", "UR8" };
	const char *astr[] = { "ASTR1", "ASTR2", "ASTR3" };
	const char *aend[] = { "AEND1", "AEND2", "AEND3" };
	int len = nx * ny * nz;
	int str, end, i, dim[3];
	GT3_HEADER head;
	int fmt, rval;
	int (*write_raw)(const void *, size_t, FILE *);
	int (*write_pack)(const void *, int, int, double, PACKING_FUNC, FILE *);
	PACKING_FUNC packing = NULL;


	/* parameter check */
	if (ptr == NULL) {
		gt3_error(GT3_ERR_CALL, "GT3_write(): null pointer passed");
		return -1;
	}
	if (nx < 1 || ny < 1 || nz < 1) {
		gt3_error(GT3_ERR_CALL, "GT3_write(): %d %d %d", nx, ny, nz);
		return -1;
	}

	fmt = default_format(type);
	if (fmt < 0) {
		gt3_error(GT3_ERR_CALL, "GT3_write(): unknown datatype");
		return -1;
	}

	if (dfmt) {
		int i;

		for (i = 0; i < sizeof dict / sizeof dict[0]; i++)
			if (strcmp(dfmt, dict[i].key) == 0) {
				fmt = dict[i].val;
				break;
			}
	}
	if (fmt == GT3_FMT_UR8 && type == GT3_TYPE_FLOAT)
		fmt = GT3_FMT_UR4;

	GT3_copyHeader(&head, headin);
	GT3_setHeaderString(&head, "DFMT", rdict[fmt]);
	GT3_setHeaderInt(&head, "SIZE", len);

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
	if (write_header(&head, fp) < 0)
		return -1;

	/*
	 *  select a writing function and a packing function.
	 */
	if (type == GT3_TYPE_DOUBLE) {
		write_raw  = write_ur4_via_double;
		write_pack = write_urc_via_double;
	} else {
		write_raw  = write_ur4_via_float;
		write_pack = write_urc_via_float;
	}

	switch (fmt) {
	case GT3_FMT_URC:
		packing = urc2_packing;
		break;

	case GT3_FMT_URC1:
		packing = urc1_packing;
		break;

	case GT3_FMT_UR8:
		write_raw = write_ur8;
		break;

	default:
		break;
	}

	/*
	 *  write data body
	 */
	if (packing) {
		double miss = -999.0;

		GT3_decodeHeaderDouble(&miss, &head, "MISS");
		rval = (*write_pack)(ptr, len, nz, miss, packing, fp);
	} else {
		rval = (*write_raw)(ptr, len, fp);
	}
	return rval;
}


#ifdef TEST
#define NX  5
#define NY  3
#define NZ  4

int
main(int argc, char **argv)
{
	const char *dfmt[] = { "UR8", "UR4", "URC", "URC1" };
	double data[NZ][NX * NY];
	GT3_HEADER head;
	int ij, k, n;
	char item[8];

	for (k = 0; k < NZ; k++)
		for (ij = 0; ij < NX * NY; ij++)
			data[k][ij] = 100. * k + ij;

	GT3_initHeader(&head);

	for (n = 0; n < sizeof dfmt / sizeof dfmt[0]; n++) {
		snprintf(item, sizeof item, "TEST%02d", n);
		GT3_setHeaderString(&head, "ITEM", item);

		if (GT3_write(data, GT3_TYPE_DOUBLE, NX, NY, NZ,
					  &head, dfmt[n], stdout) < 0) {
			GT3_printErrorMessages(stderr);
			return 1;
		}
	}
	return 0;
}
#endif
