lib_LTLIBRARIES = libgtool3.la
libgtool3_la_LDFLAGS = -version-info 1

bin_PROGRAMS = ngtcat ngtls ngtsumm ngtstat ngtick ngthead \
		ngted ngtmkax ngtdiff ngtdump ngtavr ngtconv \
		ngtredist
include_HEADERS = gtool3.h

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
		int_pack.c \
		mask.c \
		read_urc.c \
		read_urx.c \
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

utils = logging.c seq.c fileiter.c split.c get_ints.c

ngtcat_SOURCES = ngtcat.c $(utils)
ngtls_SOURCES = ngtls.c $(utils)
ngtsumm_SOURCES = ngtsumm.c $(utils)
ngtstat_SOURCES = ngtstat.c $(utils)
ngtick_SOURCES = ngtick.c $(utils)
ngthead_SOURCES = ngthead.c
ngted_SOURCES = ngted.c copysubst.c $(utils)
ngtmkax_SOURCES = ngtmkax.c $(utils)
ngtdiff_SOURCES = ngtdiff.c $(utils)
ngtdump_SOURCES = ngtdump.c $(utils)
ngtavr_SOURCES = ngtavr.c dateiter.c $(utils)
ngtconv_SOURCES = ngtconv.c $(utils)
ngtredist_SOURCES = ngtredist.c ghprintf.c $(utils)

LDADD = -lgtool3 -lm


test: $(lib_LTLIBRARIES)
	@-for f in $(libgtool3_la_SOURCES); do \
	    grep "ifdef  *TEST_MAIN" $$f > /dev/null; \
	    if [ $$? -eq 0 ]; then \
	        echo "**** TESTING $$f ****"; \
		$(COMPILE) -L.libs -DTEST_MAIN $$f $(LDADD); \
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
