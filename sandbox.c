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
#include <sys/ioctl.h>
#include <seccomp.h>

#define STACK_SIZE (1024*1024) //總共1MB
static char child_stack[STACK_SIZE]; //child process的執行空間區

void setup_seccomp(void){
    scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_KILL);
    if(!ctx){
        perror("seccomp_init");
        //return 1;
    }
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(read),0);
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(write),0);
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(close),0);
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(lseek),0);
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(pread64),0);
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(pwrite64),0);

    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(pipe),0);
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(pipe2),0);
    // 3. 檔案權限與存取檢查 (execvp 尋找執行檔時非常依賴)
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(faccessat), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(faccessat2), 0);

    // 4. 記憶體管理與優化 (底層 C 函式庫常在背景呼叫)
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(madvise), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getpid), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(poll), 0);
        // 6. 讀取符號連結 (Shell 常需要解析 /proc/self/exe 等路徑)
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(readlink), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(readlinkat), 0);
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(open),0);
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(openat),0);
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(fstat),0);
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(stat),0);
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(lstat),0);
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(access),0);

    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(mmap),0);
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(munmap),0);
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(mprotect),0);
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(brk),0);
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(mremap),0);

    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(rt_sigaction),0);
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(rt_sigprocmask),0);
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(rt_sigreturn),0);

    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(exit_group),0);
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(clone),0);
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(wait4),0);

    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(clock_gettime),0);
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(gettimeofday),0);
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(nanosleep),0);

    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(set_robust_list),0);
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(futex),0);
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(gettid),0);
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(sched_yield),0);
    //add
    // 1. [最關鍵] 允許程式進行靈魂轉移 (execvp 的底層)
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(execve), 0);

    // 2. 允許終端機 I/O 控制 (沒有這個，sh 無法把文字印在你的螢幕上)
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(ioctl), 0);
    
    // 3. 允許檔案描述符操作 (sh 啟動時會整理 stdin/stdout)
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fcntl), 0);

    // 4. x86_64 架構下 C 函式庫初始化的必備呼叫
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(arch_prctl), 0);

    // 5. 許多 Shell 啟動時會檢查當前使用者的權限，以決定要顯示 # 還是 $
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(geteuid), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getuid), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getegid), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getgid), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getppid), 0);
    // 6. [新增] Shell 執行指令 (如 ls, ps) 必備的核心呼叫
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fork), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(socket), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(setsockopt), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sendto), 0);
    
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fsetxattr), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(vfork), 0);       // Alpine sh 依賴 vfork 來產生子進程
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(dup2), 0);        // 處理標準輸入/輸出的重新導向
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(dup), 0);         // 複製 file descriptor
    
    // 7. [新增] 檔案與目錄操作 (讓 ls 和 cd 可以運作)
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getcwd), 0);      // 取得當前工作目錄
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(chdir), 0);       // 允許切換資料夾 (cd)
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getdents64), 0);  // 讀取資料夾內容 (ls 必備)
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getdents), 0);    // 舊版讀取資料夾
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(statx), 0);       // 現代 Linux 用來獲取檔案詳細資訊
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(newfstatat), 0);  // 另一種常用的檔案狀態檢查

    // 8. [新增] 終端機奪權與系統資訊必備
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(setsid), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sysinfo), 0);     // 獲取系統記憶體與運行時間
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(uname), 0);       // 獲取系統核心版本
    // 10. [新增] 允許進階 I/O 操作 (C 函式庫 printf 底層愛用)
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(writev), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(readv), 0);

    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(set_tid_address),0);
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(prlimit64),0);
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(rseq),0);
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(getrandom),0);
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(getpgrp),0);
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(getpgid),0);
    seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(setpgid),0);
    
        // 擴展屬性與存取控制 (ls -l 或著色輸出時常會探測)
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getxattr), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(lgetxattr), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fgetxattr), 0);

    // 檔案系統狀態 (確認掛載點屬性)
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(statfs), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fstatfs), 0);

    // 其他終端機與行程控制補強
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(tgkill), 0);
    //seccomp_rule_add(ctx,SCMP_ACT_ALLOW,SCMP_SYS(kill),0);
    if(seccomp_load(ctx)<0){
        perror("seccomp_load");
        seccomp_release(ctx);
        //return 1;
    }
    seccomp_release(ctx);
    //return 0;
}
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
    printf("\n[Outside] 沙盒已關閉。死因調查報告：\n");
    if (WIFEXITED(status)) {
        printf(" -> 正常結束，退出碼 (Exit Code): %d\n", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        printf(" -> 被訊號強制終止 (Killed by Signal): %d\n", WTERMSIG(status));
    } else if (WIFSTOPPED(status)) {
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
