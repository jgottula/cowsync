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
#include <time.h>
#include <unistd.h>


#define CHUNK_SIZE (1024 * 1024 * 1024)
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

struct timespec time_before;
struct timespec time_after;

bool time_ok = true;
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
		warn("madvise on src failed");
	}
	if (madvise(mem_dst, len_dst, MADV_SEQUENTIAL) < 0) {
		warn("madvise on dst failed");
	}
	
	if (clock_gettime(CLOCK_MONOTONIC, &time_before) < 0) {
		time_ok = false;
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
		
		char *ptr_src = mem_src + off;
		char *ptr_dst = mem_dst + off;
		
		if ((off % CHUNK_SIZE) == 0) {
			size_t len_chunk = CHUNK_SIZE;
			if ((len_src - off) < CHUNK_SIZE) {
				len_chunk = (len_src - off);
			}
						
			if (off != 0) {
				if (munlock(ptr_src - CHUNK_SIZE, CHUNK_SIZE) < 0) {
					err(1, "munlock on src failed @ %ldK", off / 1024);
				}
				if (munlock(ptr_dst - CHUNK_SIZE, CHUNK_SIZE) < 0) {
					err(1, "munlock on dst failed @ %ldK", off / 1024);
				}
				
				madvise(ptr_src - CHUNK_SIZE, CHUNK_SIZE, MADV_DONTNEED);
				madvise(ptr_dst - CHUNK_SIZE, CHUNK_SIZE, MADV_DONTNEED);
			}
			
			if (mlock(ptr_src, len_chunk) < 0) {
				err(1, "mlock on src failed @ %ldK", off / 1024);
			}
			if (mlock(ptr_dst, len_chunk) < 0) {
				err(1, "mlock on dst failed @ %ldK", off / 1024);
			}
		}
		
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
	
	warnx("syncing to disk");
	if (munlockall() < 0) {
		warn("munlockall failed");
	}
	if (msync(mem_dst, len_dst, MS_SYNC) < 0) {
		warn("msync on dst failed");
	}
	if (fsync(fd_dst) < 0) {
		warn("fsync on dst failed");
	}
	
	if (munmap(mem_src, len_src) < 0) {
		warn("munmap on src failed");
	}
	if (munmap(mem_dst, len_dst) < 0) {
		warn("munmap on dst failed");
	}
	
	if (close(fd_src) < 0) {
		warn("close failed: %s", path_src);
	}
	if (close(fd_dst) < 0) {
		warn("close failed: %s", path_dst);
	}
	
	float pct_written = ((float)b_written / (float)len_src) * 100.f;
	float pct_punched = ((float)b_punched / (float)len_src) * 100.f;
	
	warnx("%luK (%d%%) written", b_written / 1024, (int)pct_written);
	warnx("%luK (%d%%) punched", b_punched / 1024, (int)pct_punched);
	
	if (clock_gettime(CLOCK_MONOTONIC, &time_after) < 0) {
		time_ok = false;
	}
	
	if (time_ok) {
		time_t msec_before = (time_before.tv_nsec / (1000 * 1000));
		time_t msec_after  = (time_after.tv_nsec / (1000 * 1000));
		
		time_t msec_diff = ((time_after.tv_sec - time_before.tv_sec) * 1000) +
			(msec_after - msec_before);
		
		double sec_diff = (double)msec_diff / 1000.;
		double mb_total = (double)len_src / (1024. * 1024.);
		
		warnx("avg rate: %.1fM/s", mb_total / sec_diff);
	}
	
	warnx("done");
	return 0;
}
