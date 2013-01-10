/*
 * vcat.c -- virtually concatenated file.
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
extend_slots(GT3_VCatFile *vf, size_t num)
{
    int newsize;
    char **path = NULL;
    int *idx = NULL;

    assert(vf);
    newsize = vf->reserved + num;

    if ((path = realloc(vf->path, newsize * sizeof(char *))) == NULL
        || (idx = realloc(vf->index, (newsize + 1) * sizeof(int))) == NULL) {
        free(path);
        gt3_error(SYSERR, NULL);
        return -1;
    }

    vf->path = path;
    vf->index = idx;
    vf->reserved = newsize;
    return 0;
}


static int
find_range(int value, const int *bnd, size_t nrange)
{
    int low, high, mid;

    if (value < bnd[0] || value >= bnd[nrange])
        return -1;

    low = 0;
    high = nrange;
    while (high - low > 1) {
        mid = (low + high) / 2;

        if (value >= bnd[mid] && value < bnd[mid+1])
            return mid;

        if (value < bnd[mid])
            high = mid;
        else
            low = mid + 1;
    }
    return low;
}


/*
 * select_file() pickup an appropriate file from files in GT3_VCatFile,
 * and seek to an appropriate position(chunk) in the file.
 */
static GT3_File *
select_file(GT3_VCatFile *vf, int tpos)
{
    GT3_File *fp = NULL;
    int i;

    i = find_range(tpos, vf->index, vf->num_files);
    if (i < 0) {
        gt3_error(GT3_ERR_INDEX, "t=%d", tpos);
        return NULL;
    }

    if (i == vf->opened_) {
        fp = vf->ofile_;
        assert(fp != NULL);
    } else {
        if ((fp = GT3_open(vf->path[i])) == NULL)
            return NULL;

        if (fp && vf->opened_ >= 0) {
            GT3_close(vf->ofile_);
            vf->ofile_ = NULL;
        }
    }

    if (GT3_seek(fp, tpos - vf->index[i], SEEK_SET) < 0) {
        /* assert("Unbelievable"); */
        return NULL;
    }

    vf->opened_ = i;
    vf->ofile_ = fp;
    return fp;
}


/*
 * create new GT3_VCatFile type.
 */
GT3_VCatFile *
GT3_newVCatFile(void)
{
    const int INITIAL_SIZE = 8;
    GT3_VCatFile *vf;

    if ((vf = malloc(sizeof(GT3_VCatFile))) == NULL) {
        gt3_error(SYSERR, NULL);
        return NULL;
    }

    vf->num_files = 0;
    vf->path = NULL;
    vf->index = NULL;
    vf->reserved = 0;
    vf->opened_ = -1;
    vf->ofile_ = NULL;
    if (extend_slots(vf, INITIAL_SIZE) < 0) {
        free(vf);
        return NULL;
    }
    vf->index[0] = 0;
    return vf;
}


int
GT3_vcatFile(GT3_VCatFile *vf, const char *path)
{
    GT3_File *fp;
    int curr, nc;

    if ((fp = GT3_openHistFile(path)) == NULL)
        return -1;
    nc = GT3_getNumChunk(fp);
    GT3_close(fp);

    if (vf->num_files >= vf->reserved
        && extend_slots(vf, vf->reserved) < 0)
        return -1;

    curr = vf->num_files;
    if ((vf->path[curr] = strdup(path)) == NULL) {
        gt3_error(SYSERR, NULL);
        return -1;
    }

    vf->num_files++;
    vf->index[curr + 1] = vf->index[curr] + nc;
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
    free(vf->path);
}


GT3_Varbuf *
GT3_setVarbuf_VF(GT3_Varbuf *var, GT3_VCatFile *vf, int tpos)
{
    if (select_file(vf, tpos) == NULL)
        return NULL;

    return GT3_getVarbuf2(var, vf->ofile_);
}


int
GT3_readHeader_VF(GT3_HEADER *header, GT3_VCatFile *vf, int tpos)
{
    if (select_file(vf, tpos) == NULL)
        return -1;

    return GT3_readHeader(header, vf->ofile_);
}


int
GT3_numChunk_VF(const GT3_VCatFile *vf)
{
    return vf->index[vf->num_files];
}


#ifdef HAVE_GLOB
#ifdef HAVE_GLOB_H
#  include <glob.h>
#endif

int
GT3_glob_VF(GT3_VCatFile *vf, const char *pattern)
{
    glob_t g;
    int i, rval = 0;

    if (glob(pattern, 0, NULL, &g) < 0) {
        gt3_error(SYSERR, "in glob pattern(%s)", pattern);
        return -1;
    }

    for (i = 0; i < g.gl_pathc; i++)
        if (GT3_vcatFile(vf, g.gl_pathv[i]) < 0) {
            rval = -1;
            break;
        }

    globfree(&g);
    return rval;
}
#else
int
GT3_glob_VF(GT3_VCatFile *vf, const char *pattern)
{
    return GT3_vcatFile(vf, pattern);
}
#endif /* HAVE_GLOB */
