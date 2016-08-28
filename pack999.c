#include <sys/mman.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <ftw.h>
#include <setjmp.h>

#include "header.h"

extern char* get_current_dir_name();

#define FILL_HEADER(head, sign, sz, compr_size, mt, at, m) \
						{ head.signature = sign; \
						  head.size = sz; \
						  head.compressed_size = compr_size; \
						  head.mtime = mt; \
						  head.atime = at; \
						  head.mode = m; }

static int dest_fd;
static int dest_total_size = 0;
static ino_t dest_inode;
static unsigned char *dest;
char *dest_name;

void fail(const char *why, const char *path)
{
	char *s = "error";
	char *buf;

	if(path == NULL) path = s;
	buf = (char *)malloc(PATH_MAX + 20);
	sprintf(buf, "%s: %s", path, why);
	if(strcmp("MAX_DEST_SIZE overdraft", why)) perror(buf);
	else {
		unlink(dest_name);
		fprintf(stderr, buf);
		fprintf(stderr,"\n");
	}

	free(buf);;
	exit(EXIT_FAILURE);
}

void fail_d(const char *why, const char *path, int fd)
{
	close(fd);
	fail(why, path);
}

void fail_dm(const char *why, const char *path, int fd, void *map, size_t sz)
{
	munmap(map, sz);
	fail_d(why, path, fd);
}

void fail_dmm(const char *why, const char *path, int fd, void *map, size_t sz,
	      void *buf)
{
	free(buf);
	fail_dm(why, path, fd, map, sz);
}

int conditions(const char *path, const struct stat *st, __attribute__((__unused__)) int flag)
{
	if (dest_inode == st->st_ino) {
		printf("omitting %s\n", path);
		return 0;
	}

	if (!S_ISREG(st->st_mode) && !S_ISDIR(st->st_mode))
		return 0;

	if (st->st_size > 100 * 1024 * 1024) {
		printf("size of %s is %lu > 100Mb. (skipped)\n", path,
		       st->st_size);
		return 0;
	}

	return 1;
}

unsigned long memcpy2(void *dst, const void *src, unsigned long len, unsigned long wpos, int fd)
{
	if(dest_total_size + len + wpos > MAX_DEST_SIZE)
		fail("MAX_DEST_SIZE overdraft", NULL);
	lseek(fd, len + wpos - 1, SEEK_SET);
	write(fd, "", 1);
	memcpy(dst + wpos, src, len);

	return wpos + len;
}

static int do_pack(const char *path, const struct stat *st, int flag)
{
	static unsigned long wpos = 0;
	struct header_t header;
	struct record_t record;
	off_t header_offset;
	unsigned char *raw;
	lzo_bytep wrk = NULL;
	lzo_uint dest_len;
	int fd, path_len;

	if (!conditions(path, st, flag))
		return 0;

	if (!S_ISDIR(st->st_mode)) {
		printf("%s %lu ", path, st->st_size);
		fflush(stdout);
	}// else printf("%s\n", path);

	fd = open(path, O_RDONLY);
	if (fd == -1)
		fail("error opening", path);

	FILL_HEADER(header, 0xC001A7C1, st->st_size, 0, st->st_mtime,
		    st->st_atime, st->st_mode);

	path_len = strlen(path);

	if (!S_ISDIR(st->st_mode)) {
		raw =  (unsigned char *)mmap(NULL, st->st_size, PROT_READ, MAP_SHARED, fd, 0);
		if (raw == MAP_FAILED)
		fail_d("error mapping", path, fd);
		wrk = (lzo_bytep) malloc(LZO1X_999_MEM_COMPRESS);
		if (!wrk)
			fail_dm("cannot allocate enought memory for LZO to work", NULL, fd, raw, st->st_size);
	}

	header_offset = wpos;
	wpos = memcpy2(dest, &header, sizeof(struct header_t), wpos, dest_fd);

	record.npad_1 = ((path_len + 1) % 4 == 0) ? 0 : (4 - (path_len + 1) % 4);
	char* path_aligned = (char *)malloc(path_len + 2 + record.npad_1);
	memcpy(path_aligned, path, path_len + 1);
	if (record.npad_1 > 0)
		memset(path_aligned + path_len + 1, '>', record.npad_1);

	wpos = memcpy2(dest, path_aligned, path_len + 1 + record.npad_1, wpos, dest_fd);
	free(path_aligned);

	if (!S_ISDIR(st->st_mode)) {	// only save directories names and its `header's`
		lseek(dest_fd, ((st->st_size + st->st_size / 64 + 16 + 3) + 3) + wpos, SEEK_SET);
		write(dest_fd, "", 1);
		// we cannot be definitely shure about compressed size (dest_len) at this moment
		if (lzo1x_999_compress(raw, st->st_size, dest + wpos , &dest_len, wrk) != LZO_E_OK)
			fail_dmm("LZO error - compression failed!", NULL, fd, raw, st->st_size, wrk);
		free(wrk);
		if (dest_len < (lzo_uint)st->st_size) {
			wpos += dest_len;
			if(wpos > MAX_DEST_SIZE)
				fail_dm("MAX_DEST_SIZE overdraft", NULL, fd, raw, st->st_size);
			header.compressed_size = dest_len;
			record.npad_2 = (header.compressed_size % 4 == 0) ? 0 : (4 - header.compressed_size % 4);
		} else {			// compressing lose it's ratio
			header.compressed_size = 0;
			dest_len = header.size;
			wpos = memcpy2(dest, raw, st->st_size, wpos, dest_fd);
			record.npad_2 = (header.size % 4 == 0) ? 0 : (4 - header.size % 4);
		}

		if (record.npad_2 > 0) 	{
			memset(dest + wpos, '>', record.npad_2);
			dest_len += record.npad_2;
		}
		wpos += record.npad_2;
		memcpy(dest + header_offset + offsetof(struct header_t, compressed_size), &header.compressed_size, sizeof(header.compressed_size));

		printf("=> %lu\n", header.compressed_size == 0 ? header.size : header.compressed_size);
		dest_total_size = wpos;
	}

	close(fd);
	munmap(raw, st->st_size);
	return 0;
}

void finish()
{
	munmap(dest, 1024*1024*1024);
	if (close(dest_fd) == -1)
		perror("close");
}

int main(int argc, char *argv[])
{
	struct stat st;
	char cwd[PATH_MAX];

	if (argc < 3) {
		fprintf(stderr, "usage: %s <target archive> <directory>\n",
			argv[0]);
		exit(EXIT_FAILURE);
	}

	if (lzo_init() != LZO_E_OK) {
		perror("lzo_init() failed\n");
		exit(EXIT_FAILURE);
	}

	getcwd(cwd, PATH_MAX);

	dest_fd = open(argv[1], O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
	if (dest_fd == -1)
		fail("cannot create", argv[1]);
	if (fchmod(dest_fd, 0664) == -1)
		fail("cannot chown", argv[1]);
	stat(argv[1], &st);
	dest_inode = st.st_ino;
	dest_name = (char *)malloc(strlen(cwd) + strlen(argv[1]));
	sprintf(dest_name, "%s/%s", cwd, argv[1]);

	stat(argv[2], &st);
	if ((st.st_mode & S_IFMT) == S_IFDIR) {
		chdir(argv[2]);
	} else {
		fprintf(stderr, "error: %s is NOT DIR\n", argv[1]);
		exit(EXIT_FAILURE);
	}

	printf("cwd: %s\nchanged to %s\n", cwd, get_current_dir_name());

	dest = (unsigned char *)mmap(NULL, 1024*1024*1024, PROT_READ | PROT_WRITE, MAP_SHARED, dest_fd, 0);
	if (dest == MAP_FAILED)
		fail_d("error mapping", NULL, dest_fd);

	atexit(finish);
	if (ftw(".", do_pack, 20) == -1) {
		perror("ftw");
		exit(EXIT_FAILURE);
	}

	if (ftruncate(dest_fd, dest_total_size) == -1)
		perror("tuncate failed");

	if (msync(dest, dest_total_size, MS_SYNC) < 0)
		perror("msync");
	printf("EXIT_SUCCESS\n");
	exit(EXIT_SUCCESS);
}

