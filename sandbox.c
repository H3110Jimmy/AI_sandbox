/*
    加上cgroup & rlimit
    有些沒有加進黑名單的是用其他手段去過濾
*/
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
#include <sys/syscall.h> //For pivot_root
#include <sys/ioctl.h>
#include <seccomp.h>
#include <errno.h>

#define STACK_SIZE (1024*1024) //總共1MB
static char child_stack[STACK_SIZE]; //child process的執行空間區

void setup_seccomp(void){
    scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_ALLOW);
    if(!ctx){
        perror("seccomp_init");
        return ;
    }
    
    seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(ptrace), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(unshare), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(setns), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(bpf), 0);   
    seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(mount), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(umount2), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(chroot), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(pivot_root), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(reboot), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(syslog), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(swapon), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(swapoff), 0);

    if(seccomp_load(ctx)<0){
        perror("seccomp_load");
        seccomp_release(ctx);
    }
    seccomp_release(ctx);
    //return 0;
}
//--child process function--
int child_function(void *arg){
    printf("\n");
    //Enter the box.
    printf("[Inside] Clone successfully!\n");
    printf("[Inside] child pid inside namespace : %d\n",getpid());
    printf("[Inside] child process see parent pid : %d\n",getppid());
    /*char *argv[] = {"/bin/bash",NULL};
    execvp("/bin/bash",argv);
    ->現在沒有綁mount namespace 所以用上ps aux還是會顯示全部的process*/
    
    //mount
    /*
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
    */
    // 將掛載點設為私有(防止umount影響宿主機)
    //幹你娘這段不能漏 把我主機端幹爛了
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL)!=0){
        perror("[Error] make root private fail");
        return -1;
    }
    if(mount("./rootfs", "./rootfs", "bind", MS_BIND | MS_REC, "") != 0){
        perror("[Error] bind mount rootfs fail");
        return -1;
    }

    // 執行 pivot_root，切換視角的中心
    // 因為 glibc 沒有包裝 pivot_root，所以必須直接呼叫底層 syscall
    mkdir("./rootfs/oldroot", 0777);
    if(syscall(SYS_pivot_root, "./rootfs", "./rootfs/oldroot") != 0){
        perror("[Error] pivot_root fail");
        return -1;
    }

    //將當前工作目錄切換到新的根目錄
    if(chdir("/") != 0){
        perror("[Error] chdir fail");
        return -1;
    }

    //把舊的宿主機根目錄徹底卸載並刪除完成真正的隔離
    if(umount2("/oldroot", MNT_DETACH)!=0){
        perror("[Error] umount2 oldroot fail");
        return -1;
    }
    rmdir("/oldroot");
    printf("[Inside] pivot_root successfully!\n");

    int max_fd=sysconf(_SC_OPEN_MAX);
    for (int i=3;i<max_fd;i++){
        //close from 3 to max_fd (0,1,2保留)
        close(i);
    }
    printf("[Inside] Closed all unnecessary fds.\n");
    
    //UTS
    char hostname[] = "sandbox-env";
    if(sethostname(hostname, strlen(hostname))!=0){
        perror("[Error] sethostname fail");
        return -1;
    }
    printf("[Inside] Hostname changed to '%s'!\n", hostname);
    
    //set filter
    setup_seccomp();
    //這行成功後面的程式碼都不會執行
    //原本是bash 但這個輕量版linux沒有 他是sh代替
    char *argv[] = {"/bin/sh",NULL};
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
    //debug
    printf("\n[Outside] Sandbox is closed：\n");
    if(WIFEXITED(status)){
        printf(" -> 正常結束，退出碼 (Exit Code): %d\n", WEXITSTATUS(status));
    }else if(WIFSIGNALED(status)){
        printf(" -> 被訊號強制終止 (Killed by Signal): %d\n", WTERMSIG(status));
    } else if(WIFSTOPPED(status)){
        printf(" -> 被訊號暫停 (Stopped by Signal): %d\n", WSTOPSIG(status));
    }
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

Seccomp : 限制 (Secure Computing Mode) [x]
    白名單
Setrlimit & Cgroup : 
    CPU bomb -> RLIMIT_CPU
    memory leak -> RLIMIT_AS
    fork bomb -> RLIMIT_OC
    fd exhaustion -> RLIMIT_NOFILE
*/
}
