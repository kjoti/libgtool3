lib_LTLIBRARIES = libgtool3.la
libgtool3_la_LDFLAGS = -version-info 1

noinst_LIBRARIES = libinternal.a

bin_PROGRAMS = ngtcat ngtls ngtsumm ngtstat ngtick ngthead \
		ngted ngtmkax ngtdiff ngtdump ngtavr ngtconv \
		ngtredist ngtmean ngtjoin
include_HEADERS = gtool3.h libgtool3.f90

libgtool3_la_SOURCES = \
		bits_set.c \
		caltime.c \
		error.c \
		file.c \
		find_minmax.c \
		gauss-legendre.c \
		grid.c \
		gtdim.c \
		header.c \
		if_fortran.c \
		int_pack.c \
		mask.c \
		read_urc.c \
		read_ury.c \
		record.c \
		reverse.c \
		scaling.c \
		talloc.c \
		timedim.c \
		urc_pack.c \
		varbuf.c \
		vcat.c \
		version.c \
		write-mask.c \
		write-urx.c \
		write-ury.c \
		write.c \
		xfread.c

libinternal_a_SOURCES = \
		copysubst.c \
		dateiter.c \
		fileiter.c \
		get_ints.c \
		ghprintf.c \
		logging.c \
		range.c \
		seq.c \
		split.c

ngtcat_SOURCES = ngtcat.c $(libinternal_a_SOURCES)
ngtls_SOURCES = ngtls.c $(libinternal_a_SOURCES)
ngtsumm_SOURCES = ngtsumm.c $(libinternal_a_SOURCES)
ngtstat_SOURCES = ngtstat.c $(libinternal_a_SOURCES)
ngtick_SOURCES = ngtick.c $(libinternal_a_SOURCES)
ngthead_SOURCES = ngthead.c
ngted_SOURCES = ngted.c $(libinternal_a_SOURCES)
ngtmkax_SOURCES = ngtmkax.c $(libinternal_a_SOURCES)
ngtdiff_SOURCES = ngtdiff.c $(libinternal_a_SOURCES)
ngtdump_SOURCES = ngtdump.c $(libinternal_a_SOURCES)
ngtavr_SOURCES = ngtavr.c $(libinternal_a_SOURCES)
ngtconv_SOURCES = ngtconv.c $(libinternal_a_SOURCES)
ngtredist_SOURCES = ngtredist.c $(libinternal_a_SOURCES)
ngtmean_SOURCES = ngtmean.c $(libinternal_a_SOURCES)
ngtjoin_SOURCES = ngtjoin.c $(libinternal_a_SOURCES)

LDADD = libinternal.a libgtool3.la -lm

TESTLDADD = libinternal.a .libs/libgtool3.a -lm
TESTSRCS = $(libgtool3_la_SOURCES) $(libinternal_a_SOURCES)

test: $(lib_LTLIBRARIES) $(noinst_LIBRARIES)
	@-for f in $(TESTSRCS); do \
	    grep "ifdef  *TEST_MAIN" $$f > /dev/null; \
	    if [ $$? -eq 0 ]; then \
	        echo "**** TESTING $$f ****"; \
		$(COMPILE) -L.libs -DTEST_MAIN $$f $(TESTLDADD); \
	        if [ $$? -ne 0 ]; then \
		    echo "**** $$f: ERROR IN COMPILE-TIME."; \
		    exit; \
	        fi; \
	        ./a.out; \
	        if [ $$? -ne 0 ]; then \
		    echo "**** $$f: FAILED."; \
		    rm a.out; \
		    exit; \
	        fi; \
	        rm a.out; \
	    fi; \
	done; \
	echo "**** All tests passed."
