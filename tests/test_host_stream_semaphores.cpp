#include "PrivateSemaphore.h"
#include <semaphore.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <thread>
#include <cstdio>
int main(){
 auto a=CreatePrivateSemaphore(),b=CreatePrivateSemaphore();if(a==SEM_FAILED||b==SEM_FAILED)return 1;
 bool zero=sem_trywait(a)==-1&&errno==EAGAIN;sem_post(a);bool isolated=sem_trywait(b)==-1&&errno==EAGAIN;
 pid_t child=fork();if(child==0){auto own=CreatePrivateSemaphore();bool okay=own!=SEM_FAILED&&sem_trywait(own)==-1&&errno==EAGAIN;if(own!=SEM_FAILED)sem_close(own);_exit(okay?0:1);}int status=0;waitpid(child,&status,0);
 bool parent=sem_trywait(a)==0;std::thread waiter([&]{while(sem_wait(b)!=0&&errno==EINTR){}});sem_post(b);waiter.join();sem_close(a);sem_close(b);
 bool okay=zero&&isolated&&WIFEXITED(status)&&WEXITSTATUS(status)==0&&parent;std::printf("private stream semaphores: zero=%d isolated=%d child=%d retained_wake=%d\n",zero,isolated,WIFEXITED(status)?WEXITSTATUS(status):-1,parent);return okay?0:1;
}
