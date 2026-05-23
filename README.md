### 自製AI沙盒環境

## 核心功能
使用者可以直接在主網頁(`index.html`) 貼上AI生成或來歷不明的code，此沙盒環境會自動檢測該程式碼是否有危險疑慮。

## 整體邏輯運行架構
`AI生出code -> 丟入沙盒執行 -> 網頁上回報測試結果`

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
