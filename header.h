#ifndef _PACK_H_
#define _PACK_H_

#define PACK_SIGNATURE 0xC001A7C1
#define MAX_DEST_SIZE 0x40000000

#include <lzo/lzo1x.h>

struct header_t {
	unsigned int signature;			/* 0xC001A7C1 */
	lzo_uint size;					/* исходный размер файла */
	unsigned int compressed_size;	/* размер сжатых данных */
	unsigned int mtime;				/* stat->st_mtime */
	unsigned int atime;				/* stat->st_atime */
	unsigned int mode;				/* stat->st_mode */
};

struct record_t {
	struct header_t header;
	char *fname;
	char npad_1;
	char *cdata;
	char npad_2;
};

#endif

