lib_LIBRARIES = libgtool3.a
bin_PROGRAMS = ngtls
include_HEADERS = gtool3.h

libgtool3_a_SOURCES = \
		bits_set.c \
		error.c \
		file.c \
		grid.c \
		gtdim.c \
		header.c \
		nr-gauleg.c \
		reverse.c \
		urc_pack.c \
		varbuf.c \
		write.c

ngtls_SOURCES = ngtls.c

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
