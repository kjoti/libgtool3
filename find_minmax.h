/*
 *  find_minmax.h
 */
#ifndef FIND_MINMAX_H
#define FIND_MINMAX_H


#define proto_find(M,T) \
	int find_##M##_##T(const T *values, int nelems, const T * miss)

proto_find(max, int);
proto_find(min, int);
proto_find(max, unsigned);
proto_find(min, unsigned);
proto_find(max, float);
proto_find(min, float);
proto_find(max, double);
proto_find(min, double);

#undef proto_find

#endif /* !FIND_MINMAX_H */
