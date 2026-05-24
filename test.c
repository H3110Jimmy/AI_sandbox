#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ipc.h>
#include <sys/shm.h>

// ==========================================
// 1. 資源消耗攻擊測試區 (Cgroup & Rlimit 資源枷鎖)
// ==========================================

void test_cpu() {
    printf("[Test CPU] 無限迴圈. CPU Time Limit (Exit 152/137)...\n");
    while(1);
}

void test_memory() {
    printf("[Test Memory] Memory leak. OOM Killer (Exit 137)...\n");
    while(1) {
        void *p = malloc(1024 * 1024); // 每次要 1MB
        if (p) memset(p, 0, 1024 * 1024);
    }
}

void test_forkbomb() {
    printf("[Test Fork Bomb] Fork bomb...\n");
    while(1) fork();
}

void test_fdbomb() {
    printf("[Test FD Bomb]...\n");
    int count = 0;
    while(1) {
        int fd = dup(1); 
        if (fd < 0) {
            printf("\n[Success] 成功觸發 FD 限制！共新增了 %d 個檔案描述符。\n", count);
            perror("系統攔截原因");
            break; 
        }
        count++;
    }
}

// ==========================================
// 2. 系統呼叫過濾 (Seccomp-BPF)
// ==========================================

void test_seccomp() {
    printf("[Test Seccomp] 嘗試呼叫被禁用的 unshare()...\n");
    // 如果 seccomp 沒擋下，這個 syscall 會回傳，否則直接被 SIGSYS 殺死 (Exit 159)
    unshare(CLONE_NEWNS);
    printf("[Danger] 測試失敗\n");
}

// ==========================================
// 3. 七大命名空間 (Namespaces) 隔離測試區
// ==========================================

// 1. PID Namespace (行程隔離)
void test_pid() {
    if (getpid() == 1) {
        printf("[Safe] 我目前是沙盒內的 PID 1\n");
    }

    // 2. 嘗試發送訊號給一個外部一定存在的 PID (例如宿主機的 PID 2 或是隨便一個大數字)
    // 正常的宿主機環境中，隨便盲狙一個 PID 就算權限不足也會回傳 EPERM，但如果隔離了會回傳 ESRCH (找不到該行程)
    printf("[Test PID] 嘗試對外部行程 (例如 PID 12345) 發送訊號...\n");
    if (kill(12345, 0) == -1) {
        perror("[Safe] 外部行程探測結果"); 
        // 預期這裡會印出 "No such process"，代表沙盒裡是一座孤島，完全看不到外面的行程
    } else {
        printf("[Danger] 糟糕，我居然能看到外部的行程\n");
    }
}

// 2. Mount Namespace (掛載隔離)
void test_mount() {
    FILE *f_sudo = fopen("../../../../../../../../../etc/sudoers", "r");

    FILE *f_alpine = fopen("/etc/alpine-release", "r");

    if (f_sudo) {
        printf("[Danger] 能讀到宿主機的 /etc/sudoers, 隔離失敗。\n");
        fclose(f_sudo);
    } else if (f_alpine) {
        printf("[Safe] 找不到宿主機機密檔案。\n");
        printf("[Safe] 但我找到了 /etc/alpine-release, 被隔離在微型fs裡。\n");
        fclose(f_alpine);
    } else {
        printf("[Warning] 什麼都沒找到，檔案系統可能壞了。\n");
    }
}

// 3. Network Namespace (網路隔離)
void test_network() {
    printf("[Test Network] 嘗試連線至外部 DNS (8.8.8.8:80)...\n");
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    inet_pton(AF_INET, "8.8.8.8", &addr.sin_addr);
    
    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        printf("[Danger] 連線成功！Network 隔離失敗。\n");
    } else {
        perror("[Safe] 連線失敗 (預期結果)");
    }
}

// 4. UTS Namespace (主機名隔離)
void test_uts() {
    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    printf("[Test UTS] 當前主機名稱為: %s\n", hostname);
    if(strcmp(hostname, "sandbox-env") == 0) {
        printf("[Safe] UTS 隔離成功！看不到宿主機真實名稱。\n");
    } else {
        printf("[Danger] UTS 隔離失敗！\n");
    }
}

// 5. User Namespace (使用者權限隔離)
void test_user() {
    uid_t uid = getuid();
    gid_t gid = getgid();
    printf("[Test User] 當前視角 UID: %d, GID: %d\n", uid, gid);
    if (uid == 0) {
        printf("[Safe] 雖然顯示為 root (0)，但在外部已被映射為普通使用者 (1000)！User 隔離成功。\n");
    } else {
        printf("[Danger] User Namespace 映射可能未生效。\n");
    }
}

// 6. IPC Namespace (進程間通訊隔離)
void test_ipc() {
    printf("[Test IPC] 嘗試獲取外部系統的共享記憶體...\n");
    // 嘗試使用常見的測試 Key 獲取共享記憶體
    int shmid = shmget((key_t)1234, 1024, 0666);
    if (shmid < 0) {
        perror("[Safe] 無法獲取外部共享記憶體");
        printf("[Safe] IPC 隔離成功，沙盒擁有乾淨的獨立通訊空間！\n");
    } else {
        printf("[Danger] 居然拿到了外部的共享記憶體！\n");
    }
}

// 7. Cgroup Namespace (資源群組視角隔離)
void test_cgroup() {
    printf("[Test Cgroup] 檢查 Cgroup 掛載視角...\n");
    FILE *f = fopen("/proc/self/cgroup", "r");
    if (f) {
        char buffer[256];
        int is_isolated = 0;
        while(fgets(buffer, sizeof(buffer), f)) {
            printf(" -> %s", buffer);
            // 如果路徑只有 "/"，代表 Cgroup Namespace 將當前節點視為根目錄
            if (strstr(buffer, "0::/\n") != NULL) {
                is_isolated = 1;
            }
        }
        fclose(f);
        
        if (is_isolated) {
            printf("[Safe] Cgroup Namespace 生效！沙盒無法窺探宿主機完整的 Cgroup 樹狀結構。\n");
        } else {
            printf("[Warning] 路徑看起來包含宿主機的結構，Cgroup Namespace 可能未完全隱藏。\n");
        }
    } else {
        printf("[Error] 無法讀取 /proc/self/cgroup\n");
    }
}

// ==========================================
// 主程式入口
// ==========================================
int main() {
    printf("===== 啟動 AI 沙盒防禦測試 =====\n\n");

    // -- 資源消耗測試 --
    // test_cpu();
    // test_memory();
    // test_forkbomb();
    // test_fdbomb();
    
    // -- 系統權限越界 --
    // test_seccomp();
    
    // -- 7 大 Namespaces 測試 --
    // test_pid();
    // test_mount();
    // test_network();
    // test_uts();
    // test_user();
    // test_ipc();
    // test_cgroup();

    return 0;
}