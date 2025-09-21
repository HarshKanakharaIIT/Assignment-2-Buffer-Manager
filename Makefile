
CC=gcc
CFLAGS=-std=c11 -Wall -Wextra -O2 -pthread

# You must supply storage_mgr.c from Assignment 1 in this directory.
SRCS_COMMON = buffer_mgr.c buffer_mgr_stat.c dberror.c storage_mgr.c
HDRS = buffer_mgr.h buffer_mgr_stat.h dberror.h dt.h storage_mgr.h test_helper.h

all: test_assign2_1 test_assign2_2

test_assign2_1: test_assign2_1.c $(SRCS_COMMON) $(HDRS)
	$(CC) $(CFLAGS) -o $@ test_assign2_1.c $(SRCS_COMMON)

test_assign2_2: test_assign2_2.c $(SRCS_COMMON) $(HDRS)
	$(CC) $(CFLAGS) -o $@ test_assign2_2.c $(SRCS_COMMON)

clean:
	rm -f test_assign2_1 test_assign2_2 *.o *.bin
