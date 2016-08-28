#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <utime.h>

#include "header.h"

extern char* get_current_dir_name();
//extern int lzo1x_decompress_safe(const unsigned char *src, unsigned int  src_len,
//											unsigned char *dst, unsigned int *dst_len, void *wrk);

static int do_unpack(const char *path)
{
	struct header_t header;
	struct stat st;
	off_t offset = 0, header_offset;
	unsigned char *raw, *arc;
	lzo_bytep wrk;
	int pad, arc_fd, fd, len, arc_len;
	char *name;
	struct utimbuf utim;

	arc_fd = open(path, O_RDWR, S_IRUSR | S_IWUSR);
	if (arc_fd == -1) {
		perror(path);
		exit(EXIT_FAILURE);
	}

	if (fstat(arc_fd, &st) == -1) {
		perror(path);
		close(arc_fd);
		exit(EXIT_FAILURE);
	}

	arc = (unsigned char*)mmap(0, st.st_size, PROT_READ, MAP_PRIVATE, arc_fd, 0);
	if (arc == MAP_FAILED) {
		perror(path);
		close(arc_fd);
		exit(EXIT_FAILURE);
	}
	wrk = (lzo_bytep) malloc(LZO1X_999_MEM_COMPRESS);

	arc_len = st.st_size;
	while (offset < arc_len) {
		header_offset = offset;
		memcpy(&header, arc + offset, sizeof(struct header_t));
		if (header.signature != PACK_SIGNATURE){
			printf("signature conflict: 0x%x at 0x%x\n", header.signature, (unsigned int)header_offset);
			close(arc_fd);
			munmap(arc, st.st_size);
			exit(EXIT_FAILURE);
		}
		offset += sizeof(struct header_t);
		len = 0;
		while (*(arc + offset + len) != 0) len++;
		name = (char *)malloc(len + 1);
		memcpy(name, arc + offset, len + 1);
		offset += len + 1;
		pad = ((len + 1) % 4 == 0) ? 0 : (4 - (len + 1) % 4);
		offset += pad;
		if (S_ISREG(header.mode)) {
			fd = open(name, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
			if (fd == -1) {
				printf("open: ");
				perror(name);
				close(fd);
				close(arc_fd);
				munmap(arc, st.st_size);
				exit(EXIT_FAILURE);
			}
			if (fchmod(fd, header.mode) == -1) {
				printf("fchmod: ");
				perror(path);
				close(fd);
				close(arc_fd);
				munmap(arc, st.st_size);
				exit(EXIT_FAILURE);
			}
			lseek(fd, header.size - 1, SEEK_SET);
			write(fd, "", 1);

			raw = (unsigned char*)mmap(0, header.size, PROT_WRITE, MAP_SHARED, fd, 0);
			if (raw == MAP_FAILED) {
				perror("mmap");
				close(fd);
				close(arc_fd);
				munmap(arc, st.st_size);
				exit(EXIT_FAILURE);
			}
			if (header.compressed_size == 0) {		 // uncompressed
				printf("extracting %s\n", name);
				len = header.size;
				memcpy(raw, arc + offset, len);
			} else {
				printf("unpacking %s\n", name);
				len = header.compressed_size;
				// lzo1x_decompress_asm_safe_fast seems to be a little bit buggy
				// 'diff -r <orig dir> <packed-unpacked dir>' returns 1
				//
				if (lzo1x_decompress_safe(arc + offset, len, raw, &header.size, wrk) != LZO_E_OK) {
					perror("ERROR UNPACKING FILE");
				}
			}
			offset += len;
			pad = (len % 4 == 0) ? 0 : (4 - len % 4);
			offset += pad;
			close(fd);
		} else if (S_ISDIR(header.mode)) {
			if(mkdir(name, header.mode) == -1 && errno != EEXIST)
				perror("mkdir");
		}


		utim.actime = header.atime;
		utim.modtime = header.mtime;
		if (utime(name, &utim) == -1) {
			perror("utime");
			free(name);
			close(arc_fd);
			munmap(raw, header.size);
			munmap(arc, st.st_size);
			exit(EXIT_FAILURE);
		}

		free(name);
	}

	close(arc_fd);
	munmap(raw, header.size);
	munmap(arc, st.st_size);

	return 0;
}

int main(int argc, char *argv[])
{
	struct stat st;
	char cwd[PATH_MAX];
	char fn[PATH_MAX];

	setbuf(stdout, NULL);
	if(argc < 3) {
		fprintf(stderr, "usage: %s <target archive> <directory>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	if(lzo_init() != LZO_E_OK)
	{
		perror("lzo_init() failed\n");
		exit(EXIT_FAILURE);
	}

	getcwd(cwd, PATH_MAX);

	stat(argv[2], &st);			// FIXME do we need to create directory, if it doesn't exist?
	if ((st.st_mode & S_IFMT) == S_IFDIR) {
		chdir(argv[2]);
	} else {
		fprintf(stderr, "error: %s is NOT DIR\n", argv[1]);
		exit(EXIT_FAILURE);
	}

	printf("cwd: %s\nchanged to %s\n", cwd, get_current_dir_name());
	sprintf(fn, "%s/%s", cwd, argv[1]);
	do_unpack(fn);

	printf("EXIT_SUCCESS\n");
	exit(EXIT_SUCCESS);
}

