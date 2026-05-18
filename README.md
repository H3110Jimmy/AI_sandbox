編譯
gcc sandbox.c -o sandbox

執行
sudo ./sandbox

測試 inside the box: (可以開兩個Ubuntu展示)
ps aux -> 看是否成功掛載 (pid & mount)
ping 8.8.8.8 -> 看是否成功 (network)
hostname -> 看是否成功更改hostname
ipcs -> 看是否把IPC隔離開來