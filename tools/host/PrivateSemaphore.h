#pragma once
// Process-private POSIX semaphore. macOS does not implement sem_init; named
// handles are unlinked immediately so other game instances cannot share wakes.
#include <semaphore.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <atomic>
inline sem_t *CreatePrivateSemaphore(){
 static std::atomic<unsigned> serial{0};char name[31];
 std::snprintf(name,sizeof name,"/vc_%x_%x",unsigned(getpid()),serial.fetch_add(1));
 sem_t *result=sem_open(name,O_CREAT|O_EXCL,0600,0);
 if(result!=SEM_FAILED)sem_unlink(name);
 return result;
}
