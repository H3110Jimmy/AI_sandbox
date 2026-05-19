from http.server import HTTPServer, BaseHTTPRequestHandler
import json
import os
import subprocess
import time
import uuid

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
RUNS_DIR = os.path.join(BASE_DIR, "rootfs", "tmp", "runs")
LOGS_DIR = os.path.join(BASE_DIR, "logs")

os.makedirs(RUNS_DIR, exist_ok=True)
os.makedirs(LOGS_DIR, exist_ok=True)


class SandboxHandler(BaseHTTPRequestHandler):

    def _set_headers(self, status_code=200):
        self.send_response(status_code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def do_OPTIONS(self):
        self._set_headers(200)

    def do_GET(self):

        if self.path == "/health":

            self._set_headers(200)

            self.wfile.write(json.dumps({
                "status": "ok",
                "message": "Backend server is running"
            }).encode())

            return

        if self.path == "/logs":

            logs = []

            for filename in os.listdir(LOGS_DIR):

                if filename.endswith(".json"):
                    logs.append(filename)

            self._set_headers(200)

            self.wfile.write(json.dumps({
                "log_count": len(logs),
                "logs": logs
            }).encode())

            return

        if self.path.startswith("/logs/"):

            log_name = self.path.replace("/logs/", "")

            log_path = os.path.join(LOGS_DIR, log_name)

            if not os.path.exists(log_path):

                self._set_headers(404)

                self.wfile.write(json.dumps({
                    "error": "Log not found"
                }).encode())

                return

            with open(log_path, "r") as f:
                log_data = json.load(f)

            self._set_headers(200)

            self.wfile.write(json.dumps(log_data).encode())

            return

        self._set_headers(404)

        self.wfile.write(json.dumps({
            "error": "Not found"
        }).encode())

    def do_POST(self):

        if self.path != "/run":

            self._set_headers(404)

            self.wfile.write(json.dumps({
                "error": "Not found"
            }).encode())

            return

        content_length = int(self.headers.get("Content-Length", 0))

        body = self.rfile.read(content_length)

        try:

            data = json.loads(body.decode())

            code = data.get("code", "")

            if not code.strip():

                self._set_headers(400)

                self.wfile.write(json.dumps({
                    "status": "error",
                    "message": "No code provided"
                }).encode())

                return

            result = self.run_c_code(code)

            self.save_log(result)

            self._set_headers(200)

            self.wfile.write(json.dumps(result).encode())

        except Exception as e:

            self._set_headers(500)

            self.wfile.write(json.dumps({
                "status": "server_error",
                "message": str(e)
            }).encode())

    def save_log(self, result):

        log_filename = result["run_id"] + ".json"

        log_path = os.path.join(LOGS_DIR, log_filename)

        with open(log_path, "w") as f:
            json.dump(result, f, indent=4)

    def run_c_code(self, code):

        run_id = str(uuid.uuid4())
        run_dir = os.path.join(RUNS_DIR, run_id)
        os.makedirs(run_dir, exist_ok=True)

        # 宿主機視角的實體路徑 (用來寫入使用者的 code)
        source_path = os.path.join(run_dir, "main.c")
        with open(source_path, "w") as f:
            f.write(code)

        # 從沙盒內部 (pivot_root 後) 看出去的相對路徑
        sandbox_source_path = f"/tmp/runs/{run_id}/main.c"
        sandbox_binary_path = f"/tmp/runs/{run_id}/main"

        start_time = time.time()

        # ==========================================
        # 升級 1：連編譯 (gcc) 都關進沙盒裡面執行！
        # ==========================================
        compile_cmd = [
            "./sandbox",
            "/usr/bin/gcc",        # 呼叫 Alpine 內建的 gcc
            sandbox_source_path,
            "-o",
            sandbox_binary_path,
            "-static"
        ]

        try:
            compile_result = subprocess.run(
                compile_cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=10 # 外部保險絲：防止惡意代碼引發「編譯炸彈」卡死伺服器
            )

            if compile_result.returncode != 0:
                end_time = time.time()
                return {
                    "run_id": run_id,
                    "status": "compile_error",
                    "stdout": compile_result.stdout,
                    "stderr": compile_result.stderr, # 這裡會包含 gcc 的語法報錯，或沙盒被阻擋的日誌
                    "exit_code": compile_result.returncode,
                    "execution_time": round(end_time - start_time, 3),
                    "created_at": time.strftime("%Y-%m-%d %H:%M:%S"),
                    "code": code
                }
                
        except subprocess.TimeoutExpired:
            end_time = time.time()
            return {
                "run_id": run_id,
                "status": "compile_error",
                "stdout": "",
                "stderr": "[Error] 編譯超時 (Timeout)！可能是觸發了編譯炸彈。",
                "exit_code": -1,
                "execution_time": round(end_time - start_time, 3),
                "created_at": time.strftime("%Y-%m-%d %H:%M:%S"),
                "code": code
            }

        # ==========================================
        # 升級 2：執行編譯好的程式碼 (維持原樣，但路徑更明確)
        # ==========================================
        try:
            exec_result = subprocess.run(
                ["./sandbox", sandbox_binary_path],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=5
            )

            end_time = time.time()
            exit_code = exec_result.returncode
            status = "success"
            stderr_msg = exec_result.stderr

            if exit_code != 0:
                if stderr_msg is None:
                    stderr_msg = ""
                    
                if exit_code == 152: # SIGXCPU
                    status = "timeout"
                    stderr_msg += "\n[Error] Program timeout: 執行時間超過 CPU 限制。"
                elif exit_code == 137: # SIGKILL
                    status = "memory_limit_exceeded"
                    stderr_msg += "\n[Error] Memory Limit Exceeded: 記憶體用量超標 (Killed)。"
                elif exit_code == 159: # SIGSYS
                    status = "security_violation"
                    stderr_msg += "\n[Error] Security Violation: 觸發危險系統呼叫，已被 Seccomp 攔截。"
                elif exit_code == 139: # SIGSEGV
                    status = "runtime_error"
                    stderr_msg += "\n[Error] Segmentation fault: 記憶體區段錯誤。"
                elif exit_code == 136: # SIGFPE
                    status = "runtime_error"
                    stderr_msg += "\n[Error] Floating point exception: 數學運算錯誤 (例如除以零)。"
                elif exit_code == 255: # Sandbox internal
                    status = "sandbox_error"
                    stderr_msg += "\n[Error] Sandbox internal failure: 沙盒初始化失敗。"
                else:
                    status = "runtime_error"
                    stderr_msg += f"\n[Error] Program exited abnormally with code {exit_code}."

            return {
                "run_id": run_id,
                "status": status,
                "stdout": exec_result.stdout,
                "stderr": stderr_msg,
                "exit_code": exit_code,
                "execution_time": round(end_time - start_time, 3),
                "created_at": time.strftime("%Y-%m-%d %H:%M:%S"),
                "code": code
            }

        except subprocess.TimeoutExpired:
            end_time = time.time()
            return {
                "run_id": run_id,
                "status": "timeout",
                "stdout": "",
                "stderr": "Program timeout: execution exceeded host wrapper limit",
                "exit_code": -1,
                "execution_time": round(end_time - start_time, 3),
                "created_at": time.strftime("%Y-%m-%d %H:%M:%S"),
                "code": code
            }

def main():

    server_address = ("", 5000)

    httpd = HTTPServer(server_address, SandboxHandler)

    print("Backend server running at http://localhost:5000")
    print("API: POST /run")
    print("API: GET /logs")

    httpd.serve_forever()


if __name__ == "__main__":
    main()
