#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <xutils/file_map.h>

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

#ifndef MIN
#define MIN(x,y) ((x) <= (y) ? (x) : (y))
#endif

#define fmlog(quiet, fmt...)		\
	do {				\
		if (!quiet)		\
			printf(fmt);	\
	} while (0)

typedef struct file_info {
	size_t size;
	mode_t mode;
} file_info_t;

static int
stat_real(const char *file, struct stat *st, int quiet)
{
	int r = stat(file, st);
	if (r) {
		fmlog(quiet, "Error: *** stat %s error %d\n", file, errno);
		return (r);
	}

	if ((st->st_mode & S_IFMT) != S_IFLNK)
		return (0);

	char path[1024];
	r = readlink(file, path, sizeof(path));
	if (r <= 0 || r + 1 >= sizeof(path)) {
		fmlog(quiet, "Error: *** readlink %s error %d\n", file, errno);
		return (-1);
	}

	r = stat(path, st);
	if (r)
		fmlog(quiet, "Error: *** stat %s error %d\n", path, errno);
	return (r);
}

static int
get_bdev_sz(const char *path, size_t *size, int quiet)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		fmlog(quiet, "Error: *** open %s error %d\n", path, errno);
		return (-1);
	}

	uint64_t sz64;
	int r = ioctl(fd, BLKGETSIZE64, &sz64);
	close(fd);

	if (!r)
		*size = (size_t)sz64;
	else
		fmlog(quiet, "Error: *** get size of %s, ret %d, errno %d\n",
		    path, r, errno);
	return (r);
}

static int
get_fi_(const char *file, file_info_t *fi, int quiet)
{
	struct stat st;
	if (stat_real(file, &st, quiet)) {
		fmlog(quiet, "Error: *** stat %s error %d\n", file, errno);
		return (-1);
	}

	mode_t mode = st.st_mode & S_IFMT;
	if (mode != S_IFREG && mode != S_IFBLK) {
		fmlog(quiet, "Error: *** %s is not a regular file "
		    "or block device\n", file);
		return (-1);
	}

	fi->mode = mode;
	if (mode == S_IFREG) {
		fi->size = st.st_size;
		return (0);
	} else // S_IFBLK
		return (get_bdev_sz(file, &fi->size, quiet));
}

static inline int
get_fi(const char *file, file_info_t *fi)
{
	return (get_fi_(file, fi, 0));
}

static inline int
get_fi_quiet(const char *file, file_info_t *fi)
{
	return (get_fi_(file, fi, 1));
}

static inline size_t
calc_map_size(size_t size)
{
	return ((size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));
}

static inline int
fm_mapped(file_map_t *fm)
{
	return (fm->buf != NULL);
}

static int
do_map(file_map_t *fm)
{
	file_info_t fi;
	if (get_fi(fm->path, &fi))
		return (-1);

	size_t size = fi.size;
	if (!size || size > (1ULL << 63) - 1) {
		printf("Error: *** error size of %s %ld\n", fm->path, size);
		return (-1);
	}

	int fd = open(fm->path, fm->readonly ? O_RDONLY : O_RDWR);
	if (fd < 0) {
		printf("Error: *** open %s error %d\n", fm->path, errno);
		return (-1);
	}

	fm->buf = mmap(NULL, calc_map_size(size),
	    PROT_READ | (fm->readonly ? 0 : PROT_WRITE), MAP_SHARED, fd, 0);
	close(fd);

	if (fm->buf == MAP_FAILED) {
		printf("Error: *** map %s with size %lx error %d\n",
		    fm->path, size, errno);
		fm->buf = NULL;
		return (-1);
	}

	fm->size = size;
	return (0);
}

static inline void
do_unmap(file_map_t *fm)
{
	munmap(fm->buf, calc_map_size(fm->size));
	fm->buf = NULL;
}

static int
size_changable(const char *path)
{
	file_info_t fi;
	if (!get_fi_quiet(path, &fi) && fi.mode == S_IFBLK) {
		printf("Error: *** can not change size of block device %s\n",
		    path);
		return (0);
	}
	return (1);
}

static int
expand(const char *path, size_t size)
{
	FILE *fp = fopen(path, "a+");
	if (!fp) {
		printf("Error: *** open %s error %d\n", path, errno);
		return (-1);
	}

	size_t max_size = 1024 * 1024;
	void *buf = malloc(max_size);
	memset(buf, 0, max_size);

	int rc = 0;
	while (size > 0) {
		size_t wr_size = MIN(size, max_size);
		if (fwrite(buf, wr_size, 1, fp) != 1) {
			printf("Error: *** write %s size %lx error %d\n",
			    path, wr_size, errno);
			rc = -1;
			break;
		}
		size -= wr_size;
	}

	free(buf);
	fclose(fp);
	return (rc);
}

static int
set_size(const char *path, size_t size, int *changed)
{
	int rc;

	if (!access(path, F_OK)) {
		file_info_t fi;
		if (get_fi(path, &fi))
			return (-1);

		if (fi.size < size)
			rc = expand(path, size - fi.size);
		else if (fi.size > size)
			rc = truncate(path, size);
		else {
			*changed = 0;
			return (0);
		}
	} else
		rc = expand(path, size);

	*changed = !rc;
	return (rc);
}

void
file_map_init(file_map_t *fm, const char *path)
{
	memset(fm, 0, sizeof(*fm));
	fm->path = strdup(path);
	assert(fm->path);
}

int
file_map_set_size(file_map_t *fm, size_t size)
{
	if (!size_changable(fm->path))
		return (-1);

	int changed = 0;
	int rc = set_size(fm->path, size, &changed);
	if (!rc && changed && fm_mapped(fm)) {
		do_unmap(fm);
		rc = do_map(fm);
	}
	return (rc);
}

int
file_map_expand(file_map_t *fm, size_t size)
{
	if (!size_changable(fm->path))
		return (-1);

	int rc = expand(fm->path, size);
	if (!rc && fm_mapped(fm)) {
		do_unmap(fm);
		rc = do_map(fm);
	}
	return (rc);
}

int
file_map_open_(file_map_t *fm, int readonly, ...)
{
	if (fm_mapped(fm)) {
		if (fm->readonly == !!readonly)
			return (0);

		do_unmap(fm);
		fm->readonly = !!readonly;
	}

	if (access(fm->path, F_OK))
		if (expand(fm->path, PAGE_SIZE))
			return (-1);
	return (do_map(fm));
}

void
file_map_fini(file_map_t *fm)
{
	if (fm_mapped(fm))
		do_unmap(fm);
	free((void*)fm->path);
}
