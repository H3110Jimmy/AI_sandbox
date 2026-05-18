#define _GNU_SOURCE
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sched.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h> // 給 pivot_root 使用

#define STACK_SIZE (1024*1024) //總共1MB
static char child_stack[STACK_SIZE]; //child process的執行空間區
//--child process function--
int child_function(void *arg){
    printf("\n");
    //Enter the box.
    printf("[Inside] Clone successfully!\n");
    //pid_namespace test
    printf("--pid_namesapce--\n");
    printf("[Inside] child pid inside namespace : %d\n",getpid());
    printf("[Inside] child process see parent pid : %d\n",getppid());
    char *argv[] = {"/bin/bash",NULL};
    execvp("/bin/bash",argv);
    //現在沒有綁mount namespace 所以用上ps aux還是會顯示全部的process
    
    return 1;
}
//--主程式--
int main(void){
    printf("[Outside] Parent id :%d\n",getpid());
    //clone = fork+有更精細的控制權
    /*int clone(int (*fn)(void *), void *stack, int flags, void *arg);
        fn: function pointer, child生出來後執行this.
        stack: 分配給this child的記憶體空間.(因為不分的話會和父process共享同一個memory space)
        flag : bitmask, 去決定隔離
        arg : 如果子process需要外部的參數用這裡傳
    */
    pid_t pid = clone(child_function, child_stack + STACK_SIZE, CLONE_NEWPID | SIGCHLD, NULL);
    printf("[Outside] trying to clone the box...\n");
    if(pid<0){
        perror("[Error] Clone fail.\n");
        exit(EXIT_FAILURE);
    }
    int status;
    waitpid(pid, &status, 0);
    printf("[Outside] Exit the box.\n");
    return 0;
/* 
    Mount 隔離 CLONE_NEWNS
    PID 隔離 CLONE_NEWPID
    Network 隔離 CLONE_NEWNET
    User 隔離 CLONE_NEWUSER
    UTS 隔離 CLONE_NEWUTS
    IPC 隔離 CLONE_NEWIPC
    Cgroup 隔離 CLONE_NEWCGROUP
*/
}
