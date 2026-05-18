編譯
gcc sandbox.c -o sandbox

執行
sudo ./sandbox

測試 inside the box:
ps aux -> 看是否成功掛載 (pid & mount)
ping 8.8.8.8 -> 看是否成功 (network)