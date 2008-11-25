/*  -*- tab-width: 4; -*-
 *  vim: ts=4
 *
 *  vcat.c -- virtually concatenated file.
 */
#include "internal.h"

#include <sys/types.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gtool3.h"
#include "debug.h"


static int
alloc_file(GT3_VCatFile *vf, size_t num)
{
	int newsize;
	char **path = NULL;
	int *idx = NULL;


	assert(vf);
	newsize = vf->reserved + num;

	if ((path = realloc(vf->path, newsize * sizeof(char *))) == NULL
		|| (idx = realloc(vf->index, (newsize + 1) * sizeof(int))) == NULL) {
		free(path);
		return -1;
	}

	vf->path = path;
	vf->index = idx;
	vf->reserved = newsize;
	return 0;
}


/*
 *  select_file() pickup an appropriate file from files in GT3_VCatFile,
 *  and seek to an appropriate position(chunk) in the file.
 */
static GT3_File *
select_file(GT3_VCatFile *vf, int tpos)
{
	GT3_File *fp = NULL;
	int i;

	for (i = 0; i < vf->num_files; i++)
		if (tpos >= vf->index[i] && tpos < vf->index[i+1]) {
			if (i == vf->opened_)
				fp = vf->ofile_;
			else {
				fp = GT3_open(vf->path[i]);
				if (fp && vf->opened_ >= 0) {
					GT3_close(vf->ofile_);
					vf->ofile_ = NULL;
				}
			}
			break;
		}

	if (fp) {
		if (GT3_seek(fp, tpos - vf->index[i], SEEK_SET) < 0) {
			assert("seek error in select_file");
		}

		vf->opened_ = i;
		vf->ofile_ = fp;
		debug2("select_file: %d %d", i, fp->curr);
	}
	return fp;
}


/*
 *  create new GT3_VCatFile type.
 */
GT3_VCatFile *
GT3_newVCatFile(void)
{
	GT3_VCatFile *vf;
	const int INITIAL_SIZE = 8;


	vf = malloc(sizeof(GT3_VCatFile));
	if (vf) {
		vf->num_files = 0;
		vf->path = NULL;
		vf->index = NULL;
		vf->reserved = 0;
		vf->opened_ = -1;
		vf->ofile_ = NULL;

		if (alloc_file(vf, INITIAL_SIZE) < 0) {
			free(vf);
			return NULL;
		}
		vf->index[0] = 0;
	}
	return vf;
}


int
GT3_vcatFile(GT3_VCatFile *vf, const char *path)
{
	int curr, nc;

	nc = GT3_countChunk(path);
	if (nc < 0)
		return -1;

	if (vf->num_files >= vf->reserved
		&& alloc_file(vf, vf->reserved) < 0)
		return -1;

	curr = vf->num_files;
	if ((vf->path[curr] = strdup(path)) == NULL)
		return -1;

	vf->index[curr+1] = vf->index[curr] + nc;
	vf->num_files++;
	return 0;
}


void
GT3_destroyVCatFile(GT3_VCatFile *vf)
{
	int i;

	if (vf->opened_ >= 0)
		GT3_close(vf->ofile_);

	free(vf->index);
	for (i = 0; i < vf->num_files; i++)
		free(vf->path[i]);
}


GT3_Varbuf *
GT3_setVarbuf_VF(GT3_Varbuf *var, GT3_VCatFile *vf, int tpos)
{
	if (select_file(vf, tpos) == NULL) {
		gt3_error(GT3_ERR_INDEX, "select_file() failed: t=%d", tpos);
		return NULL;
	}

	return GT3_getVarbuf2(var, vf->ofile_);
}


int
GT3_numChunk_VF(const GT3_VCatFile *vf)
{
	return vf->index[vf->num_files];
}


#ifdef HAVE_GLOB
#include <glob.h>
int
GT3_glob_VF(GT3_VCatFile *vf, const char *pattern)
{
	glob_t g;
	int i;

	if (glob(pattern, 0, NULL, &g) < 0) {
		gt3_error(SYSERR, "in glob pattern(%s)", pattern);
		return -1;
	}

	for (i = 0; i < g.gl_pathc; i++)
		if (GT3_vcatFile(vf, g.gl_pathv[i]) < 0)
			break;

	globfree(&g);
	return 0;
}
#else
int
GT3_globVCatFile(GT3_VCatFile *vf, const char *pattern)
{
	return GT3_vcatFile(vf, pattern);
}
#endif /* HAVE_GLOB */


#ifdef TEST_MAIN
void
print_filelist(const GT3_VCatFile *vf)
{
	int i;

	printf("# of files: %d\n", vf->num_files);
	printf("# of chunks: %d\n", vf->index[vf->num_files]);

	for (i = 0; i < vf->num_files; i++)
		printf("(%s) %d %d\n", vf->path[i], vf->index[i], vf->index[i+1]);
}


void
print_status(const GT3_VCatFile *vf)
{
	if (vf->opened_ >= 0) {
		printf("opened file: (%s)\n", vf->path[vf->opened_]);
	}
}


int
main(int argc, char **argv)
{
	GT3_VCatFile *vf;
	GT3_Varbuf *var = NULL;
	int i, rval, nc;

	vf = GT3_newVCatFile();
	GT3_glob_VF(vf, "/tmp/kjo/T2.?");

	print_filelist(vf);

	nc = GT3_numChunk_VF(vf);
	for (i = 0; i < nc; i++) {
		var = GT3_setVarbuf_VF(var, vf, i);
		assert(var);
		rval = GT3_readVarZ(var, 0);
		assert(rval == 0);
		print_status(vf);
	}


	return 0;
}
#endif
