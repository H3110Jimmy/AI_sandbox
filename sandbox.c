#define _GNU_SOURCE //為了能使用Linux部分功能
#include <stdio.h>

#include <sys/types.h>
#include <unistd.h>//getpid()
#include <sys/wait.h>//waitpid()
#include <sched.h> //clone()
#include <signal.h>
#include <stdlib.h>
#include <sys/mount.h>  //mount() umount()
// 配置Memory space (stack) 給每個process
// default 1MB (2的20次方) 
#define STACK_SIZE (1024 * 1024)
static char child_stack[STACK_SIZE];

//inside_sandbox
int child_main(void* arg) {
    printf("\n  ---> [Inside] sandbox is building...\n");
    //========set 1 file system isolation=======
    // 切換跟目錄到我們設好的小linux中，之後的執行都會認為在此地方
    if (chroot("./rootfs") != 0) {
        perror("chroot 失敗");
        return -1;
    }
    // 回到(新的)根目錄
    chdir("/");

    //[Debug] 看底下實際目錄
    printf("\n  ---> [Debug] 沙盒眼中的世界長這樣：\n");
    system("ls -la /");
    printf("  ---> [Debug] 列表結束\n\n");


    // 掛載虛擬的 proc 檔案系統
    // 將kernel內的虛擬進程檔案系統 功能 映射到/proc裡面
    if (mount("proc", "/proc", "proc", 0, NULL) != 0) {
        perror("mount proc 失敗");
        return -1;
    }

    printf("  ---> [Inside] file system isolation done\n");
    //======set 1 : file system isolation done=======
    printf("  ---> [Inside] My pid is : %d\n", getpid());
    
    // [修改] 這裡我們不只印字，我們啟動 Alpine 裡面的 shell
    // 注意：因為 chroot 了，所以這裡的 /bin/sh 是 Alpine 提供的，不是伺服器原本的
    system("/bin/sh"); 

    // 離開前把掛載清除
    umount("/proc");

    printf("  ---> [Inside] 準備銷毀。\n");
    return 0;
}
int main() {
    printf("[Outside] Ready to build sandbox...\n");
    printf("[Outside] PID now is:%d\n",getpid());
    //---build sandbox---
    // 用clone可以操作更底層的東西
    
    // CLONE_VM : kernel不複製出空間，直接讓父子共用同一個Memory space
    // SIGCHLD : 當此process結束或死亡，回傳通知
    
    // clone(執行的function,記憶體空間,附加條件)
    // clone出一個子process去執行我們的測試程式

    // CLONE_NEWNS 加上後process就看不到外界的資料夾了
    int flags = CLONE_NEWPID | CLONE_NEWNS | SIGCHLD;
    // CLONE_NEWNS : 像是一本獨立的筆記本，讓 process 在裡面做掛載動作時，不會改到別人的筆記本，解決「掛載動作會干擾宿主機」的問題。
    // CLONE_NEWPID : 讓process只知道自己的pid =1
    int child_pid = clone(child_main, child_stack + STACK_SIZE, flags, NULL);
    
    if (child_pid == -1) {
        perror("clone failed");
        return 1;
    }

    // 父行程停在這裡等黑盒子執行完畢
    waitpid(child_pid, NULL, 0);

    printf("\n[Outside] sandbox is closed.\n");

    return 0;
}