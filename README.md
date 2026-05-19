編譯
gcc sandbox.c -o sandbox
gcc sandbox.c -o sandbox -lseccomp

執行
sudo ./sandbox

測試 inside the box: (可以開兩個Ubuntu展示)
1. ls -> 看是否被實際隔離
2. hostname -> 看是否成功更改hostname
3. ps aux -> 看是否成功掛載 (pid & mount)
4. ping -c 2 8.8.8.8 -> 看是否成功 (network)

5. 先在外層Ububtu打
ipcmk -M 1024
然後用下面這個指令在沙盒和外層個打一次
ipcs -> 看是否把IPC隔離開來
最後用這個清掉
ipcrm -m 0

6. 