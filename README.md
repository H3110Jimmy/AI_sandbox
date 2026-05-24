# 自製AI沙盒環境

## 核心功能
使用者可以直接在主網頁(`index.html`) 貼上AI生成或來歷不明的code，此沙盒環境會自動檢測該程式碼是否有危險疑慮。

## 整體邏輯運行架構
`AI生出code -> 丟入沙盒執行 -> 網頁上回報測試結果`

## 系統架構
在專案中建置一個新的微型file system，在此之中加上gcc編譯器與各種限制達到沙盒之目的
```text
AI_sandbox/
├── index.html           # 網頁前端介面 (使用者操作面板)
├── server.py            # Python 後端伺服器 (API 路由與沙盒排程)
├── sandbox.c            # C 語言核心沙盒 (核心代碼)
├── .gitignore           # Git 忽略清單 (避免系統垃圾上傳)
├── test.c               # 內部放一些測試用的code
├── rootfs/              # Alpine Linux 隔離環境 (內部微型fs)
│   └── tmp/
│       └── current_run/ # 最新一次執行資料
├── database/
│   ├── sandbox.db       # SQLite歷史資料庫
│   ├── archive/         # 舊執行紀錄
│   └── logs/            # 程式碼執行歷史紀錄與驗屍報告 (會自動生成)
│                        # JSON執行紀錄
```

## 環境要求
1. **作業系統:** Linux (推薦 Ubuntu 20.04/22.04 或 WSL2 環境)
2. **Python 版本:** Python 3.6 或以上 (用於後端伺服器)
3. **C 編譯器:** `gcc` (用於編譯沙盒主程式)
4. **系統函式庫:** `libseccomp` (用於實作系統呼叫過濾)

## 安裝與建置
請依照以下步驟建置沙盒環境：
1. 安裝系統相依套件
在宿主機 (Host) 終端機執行以下指令，安裝 C 語言編譯器與 Seccomp 開發套件：
```bash
sudo apt update
sudo apt install gcc libseccomp-dev wget tar -y
```
2. 安裝原始碼
```bash
git clone <你的_GitHub_Repo_網址>
cd AI_sandbox
```
3. 編譯系統的核心-沙盒
```bash
gcc sandbox.c -o sandbox -lseccomp
```

4. 新建微型file system至專案中
```bash
# 移動到該專案底下
cd AI_sandbox

# 建立並清空 rootfs 資料夾
sudo rm -rf rootfs/*
mkdir -p rootfs

# 下載並解壓縮 Alpine mini rootfs
wget https://dl-cdn.alpinelinux.org/alpine/v3.19/releases/x86_64/alpine-minirootfs-3.19.1-x86_64.tar.gz
sudo tar -xzf alpine-minirootfs-3.19.1-x86_64.tar.gz -C rootfs/

# 解壓縮完畢後，刪除下載的壓縮檔以維持目錄整潔
rm alpine-minirootfs-3.19.1-x86_64.tar.gz

# 建立執行期間需要的掛載與暫存目錄
sudo mkdir -p rootfs/tmp/runs
sudo mkdir -p rootfs/oldroot
```

5. 在沙盒中裝入gcc編譯器
```bash
# 暫時借用宿主機網路設定以便下載套件
sudo cp /etc/resolv.conf rootfs/etc/

# 切換進入 rootfs 內部
sudo chroot rootfs /bin/sh

# === 以下指令在內部file system環境內執行 ===
apk update
apk add gcc musl-dev
exit
# ==================================
```
6. 最後回到專案啟動後端伺服器
```bash
sudo python3 server.py
# 啟動後即可進入index.html使用沙盒環境
```
## 全端運作流程
本系統將前端介面，後端 API 與底層 C 語言沙盒串接

```text
[瀏覽器 (index.html)]          [Python 伺服器 (server.py)]           [C 核心沙盒 (sandbox.c)]
       │                                │                                │
 1. 提交 C 語言 Code ────(POST/run)───▶│                                 │
       │                                │ 2. 生成UUID + 寫入 main.c       │
       │                                │                                │
       │                                │ 3. 啟動編譯程序 ─────────────────▶ 隔離環境內執行 gcc
       │                        　      │◀──────(回傳編譯結果)────────────│
       │                                │                           　　　│
       │                                │ 4. 啟動執行程序 ─────────────────▶ 佈置 Namespaces/Cgroups
       │                                │                                │   載入 Seccomp 黑名單
       │                                │                                │   執行 main 二進位檔
       │                                │◀────(回傳 Exit Code & 資源)────│
       │                                │                                │
       │                                │ 5. 錯誤分析                     │
 6. 更新網頁 Dashboard ◀──(回傳 JSON)───│ 儲存至 logs/                    │
```

## 網頁執行狀態回報說明
當程式碼執行完畢或被系統強制攔截時，API 將回傳以下幾種 `status` 狀態碼，並附帶詳細的 `stderr` 診斷說明：

| 狀態碼 (Status) | 觸發條件與底層 Exit Code | 說明與防禦機制對應 |
| :--- | :--- | :--- |
| `success` | `Exit Code: 0` | **執行成功**：程式碼安全且正常地執行完畢。 |
| `compile_error` | `gcc` 回傳非 0 值 | **編譯失敗**：程式碼有語法錯誤，或因標頭檔缺失導致編譯器報錯。 |
| `security_violation` | `Exit Code: 159` (SIGSYS) | **安全違規**：偵測到惡意系統呼叫（如 `unshare`, `ptrace` 等），被 Seccomp 防禦機制當場攔截並擊殺。 |
| `timeout` | `Exit Code: 152` (SIGXCPU) <br>或被 `SIGKILL` 且時間達 3 秒 | **執行超時**：程式陷入無窮迴圈，或執行時間過長，觸發 CPU Time Limit 限制而被系統強制終止。 |
| `memory_limit_exceeded`| `Exit Code: 137` (SIGKILL) | **記憶體超標**：程式宣告過大陣列或發生 Memory Leak，衝破 Cgroup 設定的 64MB 上限，被 OOM Killer 擊殺。 |
| `runtime_error` | `Exit Code: 139` (SIGSEGV) <br> `Exit Code: 136` (SIGFPE) 等 | **執行期錯誤**：發生記憶體區段錯誤（如指標存取越界）、除以零等常見的 C 語言執行期崩潰。 |
| `sandbox_error` | `Exit Code: 255` 或其他 | **沙盒內部錯誤**：沙盒初始化失敗（例如 Rootfs 掛載失敗或權限不足），屬系統環境設定問題。 |
## API 回傳格式

當前端呼叫 `POST /run` API 後，後端會回傳以下 JSON 格式：

```json
{
    "status": "success",
    "stdout": "...",
    "stderr": "...",
    "exit_code": 0,
    "execution_time": 0.021,
    "cpu_usage": "0.0012 s",
    "memory_usage": "1408 KB",
    "run_id": "UUID"
}
```

### 欄位說明

| 欄位名稱 | 說明 |
| :--- | :--- |
| `status` | 程式執行狀態 |
| `stdout` | 程式標準輸出 |
| `stderr` | 錯誤訊息與 Sandbox Log |
| `exit_code` | Linux Exit Code |
| `execution_time` | 程式執行時間 |
| `cpu_usage` | CPU 使用時間 |
| `memory_usage` | 記憶體使用量 |
| `run_id` | 本次執行唯一識別碼 |
## 核心防禦機制
### 1. 七大 Namespace 隔離
系統透過 `clone` 系統呼叫建立 7 種獨立的 Namespace，確保沙盒與宿主機 (Host) 處於完全平行的時空：
* **檔案系統隔離 (Mount Namespace + pivot_root):** 獨立的 Alpine 運行環境，徹底阻斷惡意程式用 `../` 穿越目錄，防止讀取或破壞宿主機機密檔案（如 `/etc/shadow`）。
* **網路隔離 (Network Namespace):** 建立無對外路由的虛空網路，防止惡意程式發起 DDoS 攻擊、建立後門或外洩資料。
* **行程與權限隔離 (PID & User Namespace):** 沙盒內 PID 重新映射（內部視為 PID 1），且強制將 root 權限降級映射至外部普通使用者，防止惡意程式發送訊號暗殺宿主機行程。
* **主機名與通訊隔離 (UTS & IPC Namespace):** 隱藏真實主機名稱，並阻斷沙盒內外行程間的共享記憶體通訊。

### 2. Seccomp-BPF 系統呼叫攔截
採用 `SCMP_ACT_KILL` 懲罰機制，一旦偵測到越權行為，Linux 核心將立即以 `SIGSYS` 擊殺該行程。
目前攔截的危險 System Call 黑名單包含：
* **權限越界與提權:** `ptrace`, `unshare`, `setns`, `bpf`
* **檔案系統破壞:** `mount`, `umount2`, `chroot`, `pivot_root`
* **系統主機操作:** `reboot`, `syslog`, `swapon`, `swapoff`

### 3. Cgroup v2 + Rlimit 核心資源限制
雙重資源枷鎖，精準阻擋資源耗盡攻擊 (Resource Exhaustion)：
* **CPU 時間限制 (RLIMIT_CPU):** Soft limit 2 秒，Hard limit 3 秒
* **記憶體限制 (Cgroup memory.max + RLIMIT_AS):** 硬上限 64MB
* **行程數量限制 (Cgroup pids.max):** 最多允許 20 個行程
* **開檔數量限制 (RLIMIT_NOFILE):** Soft limit 256 個，Hard limit 512 個
