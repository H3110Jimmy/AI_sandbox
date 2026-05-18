#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
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
    //printf("--pid namesapce test--\n");
    printf("[Inside] child pid inside namespace : %d\n",getpid());
    printf("[Inside] child process see parent pid : %d\n",getppid());
    /*char *argv[] = {"/bin/bash",NULL};
    execvp("/bin/bash",argv);
    ->現在沒有綁mount namespace 所以用上ps aux還是會顯示全部的process*/
    
    //mount
    if(chroot("./rootfs")!=0){
        perror("[Error] chroot fail.");
        return -1;
    }
    if (chdir("/") != 0) {
        perror("[Error] chdir fail");
        return -1;
    }
    printf("[Inside] chroot successfully!\n");

    if(mount("proc", "/proc", "proc", 0, NULL)!=0){
        perror("[Error] mount /proc fail");
        return -1;
    }
    printf("[Inside] Mount /proc successfully!\n");
    
    int max_fd=sysconf(_SC_OPEN_MAX);
    for (int i=3;i<max_fd;i++){
        //close from 3 to max_fd (0,1,2保留)
        close(i);
    }
    printf("[Inside] Closed all unnecessary file descriptors.\n");
    
    //UTS
    char hostname[] = "sandbox-env";
    if(sethostname(hostname, strlen(hostname))!=0){
        perror("[Error] sethostname fail");
        return -1;
    }
    printf("[Inside] Hostname changed to '%s'!\n", hostname);
    
    //這行成功後面的程式碼都不會執行
    //原本是bash 但這個輕量版linux沒有 他是sh代替
    char *argv[] = {"/bin/sh", NULL};
    execvp("/bin/sh", argv);

    perror("[Error] execvp fail");
    return -1;

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
    pid_t pid = clone(child_function, child_stack + STACK_SIZE, CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWNET | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWCGROUP |
                    SIGCHLD, NULL);
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
Namespace : 隔離
    PID 隔離 CLONE_NEWPID [x]
    Mount 隔離 CLONE_NEWNS [x]
    Network 隔離 CLONE_NEWNET [x]
    User 隔離 CLONE_NEWUSER [ ]
    UTS 隔離 CLONE_NEWUTS [x]
    IPC 隔離 CLONE_NEWIPC [x]
    Cgroup 隔離 CLONE_NEWCGROUP [x]

Seccomp : 限制 (Secure Computing Mode)
Cgroup : 
*/
}
