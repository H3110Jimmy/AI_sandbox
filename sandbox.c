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
#include <sys/resource.h>
#define CGROUP_PATH "/sys/fs/cgroup/ai_sandbox"

#define STACK_SIZE (1024*1024) //總共1MB
static char child_stack[STACK_SIZE]; //child process的執行空間區
//用來傳遞給 clone 子行程的參數結構體
struct sandbox_args {
    int pipe_fd;
    int argc;
    char **argv;
};
void setup_cgroup(pid_t child_pid) {
    char path[256];
    FILE *fp;

    // 1. 建立專屬的 Cgroup 資料夾
    // 在 v2 中，只要建立資料夾，核心就會自動在裡面產生各種控制檔
    mkdir(CGROUP_PATH, 0755);

    // 2. 限制記憶體 (Memory Limit) - 例如 64MB
    snprintf(path, sizeof(path), "%s/memory.max", CGROUP_PATH);
    fp = fopen(path, "w");
    if (fp) {
        fprintf(fp, "%d\n", 64 * 1024 * 1024);
        fclose(fp);
        fprintf(stderr,"[Outside] 記憶體限制已設定為 64MB\n");
    }

    // 3. 限制行程數量 (PID Limit) - 完美防禦 Fork Bomb
    snprintf(path, sizeof(path), "%s/pids.max", CGROUP_PATH);
    fp = fopen(path, "w");
    if (fp) {
        fprintf(fp, "20\n"); // 這個 Cgroup 內最多只能有 20 個行程
        fclose(fp);
        fprintf(stderr,"[Outside] 最大行程數量已設定為 20\n");
    }

    // 4. [關鍵] 將子行程 (沙盒) 抓進這個 Cgroup 裡面
    snprintf(path, sizeof(path), "%s/cgroup.procs", CGROUP_PATH);
    fp = fopen(path, "w");
    if (fp) {
        fprintf(fp, "%d\n", child_pid);
        fclose(fp);
        fprintf(stderr,"[Outside] 已將沙盒 (PID: %d) 關入資源限制區\n", child_pid);
    } else {
        perror("[Error] 無法寫入 cgroup.procs");
    }
}
void print_resource_usage(void){
    struct rusage ru;
    getrusage(RUSAGE_CHILDREN, &ru);
    fprintf(stderr,"User CPU time: %ld.%06ld s\n",ru.ru_utime.tv_sec, ru.ru_utime.tv_usec);
    fprintf(stderr,"Sys CPU time: %ld.%06ld s\n",ru.ru_stime.tv_sec, ru.ru_stime.tv_usec);
    fprintf(stderr,"Max RSS: %ld KB\n", ru.ru_maxrss);
}
// 設定 User Namespace UID/GID 映射 (由父行程在外部執行)
void setup_uid_gid_map(pid_t child_pid) {
    char path[256];
    FILE *fp;
    
    // 強制指定映射到宿主機的普通使用者 UID=1000, GID=1000 (請確認 jimmy 的 UID 是 1000)
    uid_t outer_uid = 1000; 
    gid_t outer_gid = 1000;

    // 先拒絕 setgroups (UserNS 寫入 gid_map 的必要條件)
    snprintf(path, sizeof(path), "/proc/%d/setgroups", child_pid);
    fp = fopen(path, "w");
    if (fp) {
        fprintf(fp, "deny\n");
        fclose(fp);
    } else {
        perror("[Error] 無法寫入 setgroups");
    }

    // 映射 UID: 沙盒內 root(0) -> 外部使用者(1000)
    snprintf(path, sizeof(path), "/proc/%d/uid_map", child_pid);
    fp = fopen(path, "w");
    if (fp) {
        fprintf(fp, "0 %d 1\n", outer_uid);
        fclose(fp);
    } else {
        perror("[Error] 無法寫入 uid_map");
    }

    // 映射 GID
    snprintf(path, sizeof(path), "/proc/%d/gid_map", child_pid);
    fp = fopen(path, "w");
    if (fp) {
        fprintf(fp, "0 %d 1\n", outer_gid);
        fclose(fp);
    } else {
        perror("[Error] 無法寫入 gid_map");
    }
    fprintf(stderr,"[Outside] 權限映射完成：沙盒內 root(0) -> 外部使用者(%d)\n", outer_uid);
}
void setup_limit(int resource, rlim_t soft, rlim_t hard){
    struct rlimit rl = {soft, hard};
    if(setrlimit(resource, &rl)!=0){
        perror("set limit error");
        exit(EXIT_FAILURE);
    }
}

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
    // 解包參數
    struct sandbox_args *sargs = (struct sandbox_args *)arg;
    int pipe_fd = sargs->pipe_fd; 
    char ch;

    // [for uid namesapce]讀取管線，卡住自己，等待父行程在外面佈置好sandbox
    read(pipe_fd, &ch, 1);
    close(pipe_fd);
    if (setgid(0) != 0) {
        perror("[Error] setgid fail");
        return -1;
    }
    if (setuid(0) != 0) {
        perror("[Error] setuid fail");
        return -1;
    }
    //Enter the box.
    fprintf(stderr,"[Inside] Clone successfully!\n");
    fprintf(stderr,"[Inside] child pid inside namespace : %d\n",getpid());
    fprintf(stderr,"[Inside] child process see parent pid : %d\n",getppid());

    setup_limit(RLIMIT_CPU,2,3); // soft:2s hard:3s
    setup_limit(RLIMIT_AS,64*1024*1024,128*1024*1024); //64MB
    setup_limit(RLIMIT_NOFILE,256,512);

    // Bind Mount 新根目錄
    if(mount("./rootfs", "./rootfs", "bind", MS_BIND | MS_REC, "") != 0){
        perror("[Error] bind mount rootfs fail");
        return -1;
    }


    mkdir("./rootfs/oldroot", 0777);
    if(syscall(SYS_pivot_root, "./rootfs", "./rootfs/oldroot") != 0){
        perror("[Error] pivot_root fail");
        return -1;
    }

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
    fprintf(stderr,"[Inside] pivot_root successfully!\n");

    if (mount("proc", "/proc", "proc", 0, NULL) != 0) {
        perror("[Error] mount /proc fail");
        return -1;
    }
    fprintf(stderr,"[Inside] Mount /proc successfully!\n");

    int max_fd=sysconf(_SC_OPEN_MAX);
    for (int i=3;i<max_fd;i++){
        //close from 3 to max_fd (0,1,2保留)
        close(i);
    }
    fprintf(stderr,"[Inside] Closed all unnecessary fds.\n");
    
    //UTS
    char hostname[] = "sandbox-env";
    if(sethostname(hostname, strlen(hostname))!=0){
        perror("[Error] sethostname fail");
        return -1;
    }
    fprintf(stderr,"[Inside] Hostname changed to '%s'!\n", hostname);
    
    //set filter
    setup_seccomp();
    
    if (sargs->argc < 2) {
        // 如果沒有傳入參數，預設開啟 sh
        char *argv_sh[] = {"/bin/sh", NULL};
        execvp("/bin/sh", argv_sh);
    }else {
        // 例如：傳入的是 /tmp/runs/xxx/main
        // sargs->argv[1] = "/tmp/runs/xxx/main"
        // &sargs->argv[1] 會把後續所有的參數一起打包帶走
        execvp(sargs->argv[1], &sargs->argv[1]);
    }
    perror("[Error] execvp fail");
    return -1;

}
//--主程式--
int main(int argc, char *argv[]){
    fprintf(stderr,"[Outside] Parent id :%d\n",getpid());
    // [超重要安全防線] 由外面的真 root 先將全域掛載設為私有，防止 umount 傳染回宿主機
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0){
        perror("[Error] 全域掛載點設為私有失敗");
        exit(EXIT_FAILURE);
    }

    // 建立父子同步管線
    int sync_pipe[2];
    if (pipe(sync_pipe) < 0) {
        perror("[Error] Pipe 建立失敗");
        exit(EXIT_FAILURE);
    }
    // 打包參數丟給子行程
    struct sandbox_args sargs;
    sargs.pipe_fd = sync_pipe[0];
    sargs.argc = argc;
    sargs.argv = argv;

    //clone = fork+有更精細的控制權
    /*int clone(int (*fn)(void *), void *stack, int flags, void *arg);
        fn: function pointer, child生出來後執行this.
        stack: 分配給this child的記憶體空間.(因為不分的話會和父process共享同一個memory space)
        flag : bitmask, 去決定隔離
        arg : 如果子process需要外部的參數用這裡傳
    */
    pid_t pid = clone(child_function, child_stack + STACK_SIZE, 
                      CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWNET | CLONE_NEWUTS | 
                      CLONE_NEWIPC | CLONE_NEWCGROUP | CLONE_NEWUSER | SIGCHLD, 
                      (void *)&sargs);
    fprintf(stderr,"[Outside] trying to clone the box...\n");
    if(pid<0){
        perror("[Error] Clone fail.\n");
        exit(EXIT_FAILURE);
    }
    close(sync_pipe[0]); // 父行程不需要讀取端
    setup_cgroup(pid);
    setup_uid_gid_map(pid); // 寫入 User Namespace 映射機制

    fprintf(stderr,"[Outside] sandbox佈置完畢，釋放沙盒行程！\n");
    write(sync_pipe[1], "OK", 1);
    close(sync_pipe[1]);

    int status;
    waitpid(pid, &status, 0);
    fprintf(stderr,"\n[Outside] Sandbox is closed：\n");

    rmdir(CGROUP_PATH);
    fprintf(stderr,"[Outside] 資源限制區已回收\n");
    
    print_resource_usage();
    // 將沙盒內程式的退出狀態（包括被訊號殺死）無縫傳遞回 Python
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status); // 標準 Linux 訊號退出碼規則
    return 0;
/* 
Namespace : 隔離
    PID 隔離 CLONE_NEWPID [x]
    Mount 隔離 CLONE_NEWNS [x]
    Network 隔離 CLONE_NEWNET [x]
    User 隔離 CLONE_NEWUSER [x]
    UTS 隔離 CLONE_NEWUTS [x]
    IPC 隔離 CLONE_NEWIPC [x]
    Cgroup 隔離 CLONE_NEWCGROUP [x]

Seccomp : 限制 (Secure Computing Mode) [x]
    白名單
Setrlimit & Cgroup : [x]
    CPU bomb -> RLIMIT_CPU
    memory leak -> RLIMIT_AS
    fork bomb -> RLIMIT_OC
    fd exhaustion -> RLIMIT_NOFILE
*/
}