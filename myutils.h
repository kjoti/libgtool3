#ifndef MYUTILS__H
#define MYUTILS__H

int split(char *buf, int maxlen, int maxnum,
		  const char *head, const char *tail, char **endptr);
int get_ints(int vals[], int maxnum, const char *str, char delim);
int copysubst(char *dest, size_t len,
			  const char *orig, const char *old, const char *new);

/* scaling.c */
int idx_min_double(const double *data, size_t nelem, const double *maskval);
int idx_max_double(const double *data, size_t nelem, const double *maskval);
int idx_min_float(const float *data, size_t nelem, const float *maskval);
int idx_max_float(const float *data, size_t nelem, const float *maskval);
void scaling(unsigned *dest,
			 const double *src,
			 size_t nelem,
			 double offset, double scale,
			 unsigned imax, double miss);

void scalingf(unsigned *dest,
			  const float *src,
			  size_t nelem,
			  double offset, double scale,
			  unsigned imax, double miss);

size_t masked_scaling(unsigned *dest,
					  const double *src,
					  size_t nelem,
					  double offset, double scale,
					  unsigned imiss, double miss);

size_t masked_scalingf(unsigned *dest,
					   const float *src,
					   size_t nelem,
					   double offset, double scale,
					   unsigned imiss, double miss);


#endif /* !MYUTILS__H */
