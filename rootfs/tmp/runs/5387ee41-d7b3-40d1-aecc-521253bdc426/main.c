#include <stdio.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <errno.h>

int main() {
    printf("=== Sandbox Namespace Isolation Report ===\n\n");

    // 1. 測試 UTS Namespace (主機名稱隔離)
    struct utsname uts;
    if (uname(&uts) == 0) {
        printf("[1. UTS Namespace]\n");
        printf("系統看到的主機名稱: %s\n", uts.nodename);
        printf("防禦狀態: %s\n\n", (uts.nodename[0] == 's') ? "✅ 成功隔離 (與宿主機不同)" : "❌ 失敗");
    }

    // 2. 測試 PID Namespace (行程 ID 隔離)
    // 在隔離的 PID Namespace 中，你的程式碼應該會是前幾個被執行的行程
    pid_t pid = getpid();
    printf("[2. PID Namespace]\n");
    printf("當前程式的 PID: %d\n", pid);
    printf("防禦狀態: %s\n\n", (pid < 10) ? "✅ 成功隔離 (PID 被重新映射成極小值)" : "❌ 失敗 (可能看到宿主機 PID)");

    // 3. 測試 User Namespace (權限映射隔離)
    // 雖然外面是用普通使用者啟動，但裡面應該要是 root
    uid_t uid = getuid();
    printf("[3. User Namespace]\n");
    printf("當前程式的 UID: %d\n", uid);
    printf("防禦狀態: %s\n\n", (uid == 0) ? "✅ 成功降權映射 (內部認為是 Root，外部是普通人)" : "❌ 失敗");

    // 4. 測試 Mount Namespace + Pivot Root (檔案系統隔離)
    // 試圖讀取 Linux 宿主機絕對會有的密碼檔
    printf("[4. Mount Namespace (Pivot Root)]\n");
    FILE *f = fopen("/etc/shadow", "r");
    if (f) {
        printf("防禦狀態: ❌ 失敗！成功讀取到宿主機的高機密檔案！\n\n");
        fclose(f);
    } else {
        printf("防禦狀態: ✅ 成功隔離 (fopen 失敗，找不到宿主機的 /etc/shadow)\n\n");
    }

    // 5. 測試 Network Namespace (網路隔離)
    // 試圖建立對外的連線 (Google DNS)
    printf("[5. Network Namespace]\n");
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(80);
    inet_pton(AF_INET, "8.8.8.8", &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("防禦狀態: ✅ 成功隔離 (連線被作業系統拒絕，無外部網路)\n\n");
    } else {
        printf("防禦狀態: ❌ 失敗！成功建立對外網路連線！\n\n");
    }

    printf("==========================================\n");
    return 0;
}