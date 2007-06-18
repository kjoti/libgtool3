lib_LIBRARIES = libgtool3.a
bin_PROGRAMS = ngtcat ngtls ngtsumm ngtstat ngtick ngthead
include_HEADERS = gtool3.h

libgtool3_a_SOURCES = \
		bits_set.c \
		error.c \
		file.c \
		gauss-legendre.c \
		grid.c \
		gtdim.c \
		header.c \
		reverse.c \
		talloc.c \
		urc_pack.c \
		varbuf.c \
		version.c \
		write.c

utils = seq.c fileiter.c split.c get_ints.c

ngtcat_SOURCES = ngtcat.c $(utils)
ngtls_SOURCES = ngtls.c $(utils)
ngtsumm_SOURCES = ngtsumm.c $(utils)
ngtstat_SOURCES = ngtstat.c $(utils)
ngtick_SOURCES = ngtick.c caltime.c $(utils)
ngthead_SOURCES = ngthead.c

LDADD = libgtool3.a -lm


test: $(lib_LIBRARIES)
	@for f in $(libgtool3_a_SOURCES); do \
	    grep "ifdef  *TEST_MAIN" $$f > /dev/null; \
	    if [ $$? -eq 0 ]; then \
	        echo "**** TESTING $$f ****"; \
		$(COMPILE) -DTEST_MAIN $$f $(LDADD); \
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
