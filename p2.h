#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "tiger.h"

typedef struct Queue {
	// New Management
	unsigned int level; // up level -> +1
	unsigned int number;
	// Management
	int last; // 0 or 1
	// Data
	int outFd;
	off_t mapOffset;
	off_t len;
	word64 hashVal[3];
	int hashed;
	// Next
	struct Queue *next;
} Queue;

void* workloop(void *args);

int nextQ();

Queue* popQ();

void pushQ(Queue *temp);