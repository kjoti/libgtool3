#ifndef SEQ__H
#define SEQ__H

struct sequence {
	int curr;

	char *spec;
	char *spec_tail;
	int first, last;

	char *it;
	int head, tail, step;
};

struct sequence *initSeq(const char *spec, int first, int last);
void freeSeq(struct sequence *seq);
int nextSeq(struct sequence *seq);

#endif
