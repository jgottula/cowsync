#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64


#include <err.h>
#include <fcntl.h>
#include <linux/falloc.h>
#include <string.h>
#include <unistd.h>


#define BLK_SIZE 4096


/* goals:
- truncate dest so it's the same size
- block by block copy using mmap
- write only when different
- when it is different and src is zero, punch holes in dest
*/


const char *path_src = NULL;
const char *path_dst = NULL;
int fd_src = -1;
int fd_dst = -1;
off_t len_src = 0;
off_t len_dst = 0;
char buf_zero[BLK_SIZE];
char buf_src[BLK_SIZE];
char buf_dst[BLK_SIZE];


int main(int argc, char **argv) {
	if (argc != 3) {
		errx(1, "expected 2 arguments");
	}
	
	memset(buf_zero, 0, sizeof(buf_zero));
	
	path_src = argv[1];
	path_dst = argv[2];
	warnx("src: %s", path_src);
	warnx("dst: %s", path_dst);
	
	if ((fd_src = open(path_src, O_RDONLY)) < 0) {
		err(1, "open failed: %s", path_src);
	}
	if ((fd_dst = open(path_dst, O_RDWR)) < 0) {
		err(1, "open failed: %s", path_dst);
	}
	
	len_src = lseek(fd_src, 0, SEEK_END);
	lseek(fd_src, 0, SEEK_SET);
	len_dst = lseek(fd_dst, 0, SEEK_END);
	lseek(fd_dst, 0, SEEK_SET);
	warnx("src len: %ldK", len_src / 1024);
	warnx("dst len: %ldK", len_dst / 1024);
	
	if (len_src != len_dst) {
		warnx("truncating dst");
		
		if (ftruncate(fd_dst, len_src) < 0) {
			err(1, "ftruncate failed");
		}
	}
	
	warnx("copying now");
	off_t off = 0;
	while (off < len_src) {
		ssize_t count = BLK_SIZE;
		if ((len_src - off) < BLK_SIZE) {
			count = (len_src - off);
		}
		
		ssize_t b_read_src = read(fd_src, buf_src, count);
		if (b_read_src < 0) {
			err(1, "read failed: src @ %ldK", off / 1024);
		} else if (b_read_src != count) {
			errx(1, "parial read (%ld/%ld): src @ %ldK",
				b_read_src, count, off / 1024);
		}
		
		ssize_t b_read_dst = read(fd_dst, buf_dst, count);
		if (b_read_dst < 0) {
			err(1, "read failed: dst @ %ldK", off / 1024);
		} else if (b_read_dst != count) {
			errx(1, "parial read (%ld/%ld): dst @ %ldK",
				b_read_dst, count, off / 1024);
		}
		
		if (memcmp(buf_src, buf_dst, count) != 0) {
			if (memcmp(buf_src, buf_zero, count) == 0) {
				warnx("modified ZERO block @ %ldK", off / 1024);
				
				if (fallocate(fd_dst, FALLOC_FL_KEEP_SIZE |
					FALLOC_FL_PUNCH_HOLE, off, count) < 0) {
					err(1, "fallocate failed: dst @ %ldK", off / 1024);
				}
			} else {
				warnx("modified block @ %ldK", off / 1024);
				
				if (lseek(fd_dst, -count, SEEK_CUR) != off) {
					warnx("unexpected offset in dst");
					abort();
				}
				
				ssize_t b_written = write(fd_dst, buf_src, count);
				if (b_written < 0) {
					err(1, "write failed: dst @ %ldK", off / 1024);
				} else if (b_written != count) {
					errx(1, "parial write (%ld/%ld): dst @ %ldK",
						b_written, count, off / 1024);
				}
			}
		}
		
		off += count;
		
		if (lseek(fd_src, 0, SEEK_CUR) != off) {
			warnx("unexpected offset in src");
			abort();
		}
		if (lseek(fd_dst, 0, SEEK_CUR) != off) {
			warnx("unexpected offset in dst");
			abort();
		}
		
		if ((off % (1 << 30)) == 0) {
			warnx("progress: %ldG", off / (1 << 30));
		}
	}
	
	if (close(fd_src) < 0) {
		warn("close failed: %s", path_src);
	}
	if (close(fd_dst) < 0) {
		warn("close failed: %s", path_dst);
	}
	
	warnx("done");
	return 0;
}
