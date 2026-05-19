可手動更改參數
1. CPU Time Limit
->setup_limit(RLIMIT_CPU, 2, 3); 以及 Python 端 wrapper 的 timeout
2. Memory Limit
->Cgroup 的 memory.max 與 RLIMIT_AS 的 64 * 1024 * 1024。
3. Max Processes Limit
->Cgroup 的 pids.max 設定的 20。

目前只支援C
在沙盒裡面掛上一個輕便型的linux
在這linux內裝上編譯系統
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

JSON (Request)
{
  "submission_id": "sub_123456789",
  "language": "python3", 
  "code": "import sys\nfor line in sys.stdin:\n    print('Hello, ' + line.strip())",
  "input": "World\nJimmy",
  "limits": {
    "time_limit_ms": 2000,
    "memory_limit_kb": 65536
  }
}
JSON (Response)
{
  "submission_id": "sub_123456789",
  "status": "Accepted", 
  "exit_code": 0,
  "stdout": "Hello, World\nHello, Jimmy\n",
  "stderr": "",
  "execution_time_ms": 45,
  "memory_used_kb": 12048,
  "error_message": null
}