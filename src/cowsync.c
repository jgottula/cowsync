#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64


#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/falloc.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>


#define CHUNK_SIZE (1024 * 1024)
#define BLOCK_SIZE 4096


const char *path_src = NULL;
const char *path_dst = NULL;
int fd_src = -1;
int fd_dst = -1;
off_t len_src = 0;
off_t len_dst = 0;
char *mem_zero = NULL;
char *mem_src = NULL;
char *mem_dst = NULL;

bool falloc_ok = true;


int main(int argc, char **argv) {
	if (argc != 3) {
		errx(1, "expected 2 arguments");
	}
	
	if ((mem_zero = mmap(NULL, BLOCK_SIZE, PROT_READ,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED) {
		err(1, "anonymous mmap failed");
	}
	
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
		
		len_dst = lseek(fd_dst, 0, SEEK_END);
		lseek(fd_dst, 0, SEEK_SET);
		if (len_src != len_dst) {
			warnx("src and dst lens differ");
			abort();
		}
	}
	
	if ((mem_src = mmap(NULL, len_src, PROT_READ, MAP_SHARED,
		fd_src, 0)) == MAP_FAILED) {
		err(1, "mmap on src failed");
	}
	if ((mem_dst = mmap(NULL, len_dst, PROT_READ | PROT_WRITE, MAP_SHARED,
		fd_dst, 0)) == MAP_FAILED) {
		err(1, "mmap on dst failed");
	}
	
	if (madvise(mem_src, len_src, MADV_SEQUENTIAL) < 0) {
		err(1, "madvise on src failed");
	}
	if (madvise(mem_dst, len_dst, MADV_SEQUENTIAL) < 0) {
		err(1, "madvise on dst failed");
	}
	
	warnx("copying now");
	off_t off = 0;
	size_t b_written = 0;
	size_t b_punched = 0;
	while (off < len_src) {
		ssize_t count = BLOCK_SIZE;
		if ((len_src - off) < BLOCK_SIZE) {
			count = (len_src - off);
		}
		
		const char *ptr_src = mem_src + off;
		char *ptr_dst       = mem_dst + off;
		
		if (memcmp(ptr_src, ptr_dst, count) != 0) {
			if (falloc_ok && memcmp(ptr_src, mem_zero, count) == 0) {
				//warnx("modified block [zero] @ %ldK", off / 1024);
				
				if (fallocate(fd_dst, FALLOC_FL_KEEP_SIZE |
					FALLOC_FL_PUNCH_HOLE, off, count) == 0) {
					b_punched += count;
					continue;
				} else {
					if (errno == EOPNOTSUPP) {
						falloc_ok = false;
					} else {
						err(1, "fallocate failed: dst @ %ldK", off / 1024);
					}
				}
			}
			
			//warnx("modified block @ %ldK", off / 1024);
			memcpy(ptr_dst, ptr_src, count);
			b_written += count;
		}
		
		off += count;
		
		if ((off % (1 << 30)) == 0) {
			warnx("progress: %ldG", off / (1 << 30));
		}
	}
	
	float pct_written = ((float)b_written / (float)len_src) * 100.f;
	float pct_punched = ((float)b_punched / (float)len_src) * 100.f;
	
	warnx("%luK (%d%%) written", b_written / 1024, (int)pct_written);
	warnx("%luK (%d%%) punched", b_punched / 1024, (int)pct_punched);
	
	// TODO: unmap
	
	if (close(fd_src) < 0) {
		warn("close failed: %s", path_src);
	}
	if (close(fd_dst) < 0) {
		warn("close failed: %s", path_dst);
	}
	
	warnx("done");
	return 0;
}
