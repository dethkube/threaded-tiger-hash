#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <pthread.h>
#include <math.h>

#include "tiger.h"
#include "p2.h"
#include "smartalloc.h"

// Smallest block size is 32K per problem statement
#define SMALLEST_BLK_SIZE 32*1024
#define MAX_THREADS 1024
#define WRITE_LEN 51

pthread_t threads[MAX_THREADS];
pthread_mutex_t masterlock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t managerlock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t queuelock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t mastersig = PTHREAD_COND_INITIALIZER;
pthread_cond_t resumesig = PTHREAD_COND_INITIALIZER;

Queue *head = NULL;
Queue **tail = NULL;

int alldone = 0;
int numthreads = 0;
int tblocks = 0;
void *mfile = NULL;
int fsize = 0;
int wtotal = 0;
int wait = 0;


int main(int argc, char **argv)
{
   long currArg, i;
   void *status;
   char *file;
   Queue *temp, *prev;

   int inpFd, outFd, nameLen;
   struct stat statBuff;
   char *nameBuff;
   off_t blockSize;
   off_t blockOffset;
   off_t blockLen;

   // Argument Parsing
   if (argc < 2) {
      return 0;
   }
   else {
      numthreads = atoi(argv[1]);
   }
   if (numthreads > MAX_THREADS || numthreads < 1) {
      printf("Incorrect thread number %d\n", numthreads);
      return 0;
   }

   // Start each file
   for (currArg = 2; currArg < argc; currArg++) {
      file = argv[currArg];
      // Open the input file
      inpFd = open(file, O_RDONLY);
      if (inpFd < 0) {
         perror("Error opening input file");
         return -1;
      }
      // Open the output file
      nameLen = strlen(file) + 5;
      nameBuff = malloc(nameLen);
      if (!nameBuff) {
         perror("Malloc error");
         close(inpFd);
         return -1;
      }

      sprintf(nameBuff, "%s.tth", file);
      nameBuff[nameLen - 1] = 0;

      outFd = open(nameBuff, O_WRONLY | O_CREAT | O_TRUNC, 0600);
      if (outFd < 0) {
         perror("Error opening output file");
         free(nameBuff);
         close(inpFd);
         return -1;
      }
      free(nameBuff);

      // Use stat to determine the file size
      if (-1 == fstat(inpFd, &statBuff)) {
         perror("Error stat'ing input file");
         close(outFd);
         close(inpFd);
         return -1;
      }
      // Copy to Global
      fsize = statBuff.st_size;

      mfile = mmap(NULL, fsize, PROT_READ, MAP_FILE | MAP_SHARED, inpFd, 0);
      if (MAP_FAILED == mfile) {
         perror("Error mmap'ing");
         return -1;
      }
      // Mapped whole file, done with fd
      close(inpFd);
      
      // Set Common Vars
      i = 1;
      blockSize = SMALLEST_BLK_SIZE;

      // Init first block
      prev = calloc(sizeof(Queue), 1);
      prev->number = i++;
      prev->level = 1;
      prev->outFd = outFd;
      prev->mapOffset = 0;
      prev->len = blockSize;
      if (prev->mapOffset + prev->len > fsize) {
         prev->len = fsize - prev->mapOffset;
         temp = prev;
      }

      head = prev;

      // Compute all the file ranges and pass those into the hash function
      for (blockOffset = blockSize; blockOffset < fsize;
            blockOffset += blockSize) {
         // Compute block length accounting for possible truncated blocks
         //  at the end of the file
         blockLen = blockSize;
         if (blockOffset + blockLen > fsize)
            blockLen = fsize - blockOffset;

         // Init other blocks
         temp = calloc(sizeof(Queue), 1);
         // Finish last block
         prev->next = temp;
         // Rest of current
         temp->number = i++;
         temp->level = 1;
         temp->outFd = outFd;
         temp->mapOffset = blockOffset;
         temp->len = blockLen;
         // Switch it up
         prev = temp;
      } // Finished work queue
      temp->last = 1;
      tail = &(temp->next);
      wtotal = temp->number;

      // Unblocks threads if still blocked
      if (currArg == 2) {
         // Create all threads
         wait = 1;
         for (i = 0; i < numthreads; i++) {
            pthread_create(&threads[i], NULL, workloop, (void*)(i + 1));
         }
      }
      else { // Wait for processes to resume, then continue with relocking
         wait = 1; // <<< Explained below @ line ~262
         pthread_cond_wait(&resumesig, &masterlock);
         pthread_mutex_unlock(&masterlock);
      }

      // Wait to start new job
      // one mutex for processes to wait on
      pthread_mutex_lock(&masterlock);
      // one mutex for main to wait on
      pthread_mutex_lock(&managerlock);
      wait = 0;
      pthread_cond_wait(&mastersig, &managerlock);
      pthread_mutex_unlock(&managerlock);

      // Clean up after ourselves
      close(outFd);
      if (munmap(mfile, fsize)) {
         perror("Error munmap'ing");
         return -1;
      }

   } // End of per-file loop reminder

   alldone = 1;
   // Final unlock
   pthread_mutex_unlock(&masterlock);

   // Wait for all threads
   for (i = 0; i < numthreads; i++) {
      pthread_join(threads[i], &status);
   }

   return 0;
}

// Thread equivalent of "main"
void* workloop(void *args) {
   Queue *work;
   int done = 0, freed = 0, foffset, i, works;
   char strBuff[128];
   long tid = (long)args;
   tid = tid; // Stupid Compiler

   while (1) {
      pthread_mutex_lock(&queuelock);
      if (nextQ()) {  // checks if queue is empty
         work = popQ();
         pthread_mutex_unlock(&queuelock);

         if (!work->hashed) { // hashes if not Hashed
            tiger(mfile + work->mapOffset, work->len, work->hashVal);
            work->hashed = 1;
            pthread_mutex_lock(&queuelock);
            pushQ(work);
            pthread_mutex_unlock(&queuelock);
         }
         else { // writes WITHOUT locking, thanks Dr. Bellardo
            sprintf(strBuff, "%08X%08X %08X%08X %08X%08X\n",
               (word32)(work->hashVal[0]>>32), (word32)(work->hashVal[0]),
               (word32)(work->hashVal[1]>>32), (word32)(work->hashVal[1]),
               (word32)(work->hashVal[2]>>32), (word32)(work->hashVal[2]) );

            // changed write functionality
            foffset = 0;
            works = wtotal;
            for (i = 1; i < work->level; i++) {
               foffset += works * WRITE_LEN;
               works = works / 2 + works % 2;
            }
            foffset += (work->number - 1) * WRITE_LEN;

            if ( strlen(strBuff) != pwrite(work->outFd, strBuff, strlen(strBuff), foffset) )
               perror("write");

            if (work->last && work->number == 1) {
               free(work);
               freed = 1;
            }

            // Make new work here
            if (!freed && work->number % 2) {
               work->number = work->number / 2 + 1;
               work->level = work->level + 1;
               work->hashed = 0;
                  
               // special length check
               work->len = work->len * 2;
               if (work->mapOffset + work->len > fsize) {
                  work->len = fsize - work->mapOffset;
                  work->last = 1;
               }

               pthread_mutex_lock(&queuelock);
               pushQ(work);
               pthread_mutex_unlock(&queuelock);
            }
            else if (!freed) {
               free(work);
            }
         }
      }
      // Only applies to multiple files
      // Basically a spinlock for syncronization of processes
      else if (wait) {
         pthread_mutex_unlock(&queuelock);
         while (wait);
      } // so that the first thread doesn't finish
      // before the last thread starts, causing errors
      else {
         tblocks++;
         if (tblocks == numthreads) {
            done = 1;
         }
         pthread_mutex_unlock(&queuelock);
         if (done) {
            pthread_cond_signal(&mastersig);
         }
         pthread_mutex_lock(&masterlock);
         tblocks--;
         done = 0;
         if (!tblocks) {
            pthread_cond_signal(&resumesig);
         }
         pthread_mutex_unlock(&masterlock);
         if (alldone) {
            pthread_exit((void*) 0);
         }
      }
   }
   return 0;
}

int nextQ() {
   if (head) {
      return 1;
   }
   return 0;
}

Queue* popQ() {
   Queue *temp = head;
   head = head->next;
   if (!head) {
      tail = &head;
   }
   return temp;
}

void pushQ(Queue *temp) {
   temp->next = NULL;
   *tail = temp;
   tail = &temp->next;
}
