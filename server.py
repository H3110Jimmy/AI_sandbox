from http.server import HTTPServer, BaseHTTPRequestHandler
import json
import os
import subprocess
import time
import uuid
import shutil

BASE_DIR = os.path.dirname(os.path.abspath(__file__))

CURRENT_RUN_DIR = os.path.join(BASE_DIR, "rootfs", "tmp", "current_run")
DATABASE_DIR = os.path.join(BASE_DIR, "database")
LOGS_DIR = os.path.join(DATABASE_DIR, "logs")

os.makedirs(CURRENT_RUN_DIR, exist_ok=True)
os.makedirs(DATABASE_DIR, exist_ok=True)
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

    def archive_current_run(self):
        if os.path.exists(CURRENT_RUN_DIR) and os.listdir(CURRENT_RUN_DIR):
            archive_name = time.strftime("run_%Y%m%d_%H%M%S")
            archive_name = archive_name + "_" + str(uuid.uuid4())[:8]
            archive_path = os.path.join(DATABASE_DIR, archive_name)

            shutil.move(CURRENT_RUN_DIR, archive_path)

        os.makedirs(CURRENT_RUN_DIR, exist_ok=True)

    def save_log(self, result):
        log_filename = result["run_id"] + ".json"
        log_path = os.path.join(LOGS_DIR, log_filename)

        with open(log_path, "w") as f:
            json.dump(result, f, indent=4)

    def run_c_code(self, code):

        self.archive_current_run()

        run_id = str(uuid.uuid4())
        run_dir = CURRENT_RUN_DIR

        source_path = os.path.join(run_dir, "main.c")

        with open(source_path, "w") as f:
            f.write(code)

        sandbox_source_path = "/tmp/current_run/main.c"
        sandbox_binary_path = "/tmp/current_run/main"

        start_time = time.time()

        compile_cmd = [
            "./sandbox",
            "/usr/bin/gcc",
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
                timeout=10
            )

            if compile_result.returncode != 0:
                end_time = time.time()

                return {
                    "run_id": run_id,
                    "status": "compile_error",
                    "stdout": compile_result.stdout,
                    "stderr": compile_result.stderr,
                    "exit_code": compile_result.returncode,
                    "execution_time": round(end_time - start_time, 3),
                    "cpu_usage": "N/A",
                    "memory_usage": "N/A",
                    "created_at": time.strftime("%Y-%m-%d %H:%M:%S"),
                    "code": code,
                    "current_run_dir": CURRENT_RUN_DIR,
                    "database_dir": DATABASE_DIR
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
                "cpu_usage": "N/A",
                "memory_usage": "N/A",
                "created_at": time.strftime("%Y-%m-%d %H:%M:%S"),
                "code": code,
                "current_run_dir": CURRENT_RUN_DIR,
                "database_dir": DATABASE_DIR
            }

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

            cpu_usage = "N/A"
            memory_usage = "N/A"

            for line in stderr_msg.splitlines():
                line = line.strip()

                if "User CPU time:" in line:
                    cpu_usage = line.split(":")[-1].strip()

                if "Max RSS:" in line:
                    memory_usage = line.split(":")[-1].strip()

            if exit_code != 0:

                if stderr_msg is None:
                    stderr_msg = ""

                error_alert = ""

                if exit_code == 152:
                    status = "timeout"
                    error_alert = " [Error] Program timeout: 執行時間超過 CPU 軟限制。"

                elif exit_code == 137:
                    if "User CPU time: 3." in stderr_msg:
                        status = "timeout"
                        error_alert = " [Error] Program timeout: 執行時間達到 CPU 硬限制 (3秒強制終止)。"
                    else:
                        status = "memory_limit_exceeded"
                        error_alert = " [Error] Memory Limit Exceeded: 記憶體用量衝破 Cgroup 64MB 限制 (OOM Killed)。"

                elif exit_code == 159:
                    status = "security_violation"
                    error_alert = " [Error] Security Violation: 觸發危險系統呼叫，已被 Seccomp 攔截。"

                elif exit_code == 139:
                    status = "runtime_error"
                    error_alert = " [Error] Segmentation fault: 記憶體區段錯誤 (非法存取指標)。"

                elif exit_code == 136:
                    status = "runtime_error"
                    error_alert = " [Error] Floating point exception: 數學運算錯誤 (例如除以零)。"

                elif exit_code == 255:
                    status = "sandbox_error"
                    error_alert = " [Error] Sandbox internal failure: 沙盒初始化或掛載失敗。"

                else:
                    status = "runtime_error"
                    error_alert = f" [Error] Program exited abnormally with code {exit_code}."

                if error_alert:
                    stderr_msg = (
                        f"{error_alert}\n"
                        "=========================================\n"
                        "[Sandbox Internal Logs]\n"
                        f"{stderr_msg}"
                    )

            return {
                "run_id": run_id,
                "status": status,
                "stdout": exec_result.stdout,
                "stderr": stderr_msg,
                "exit_code": exit_code,
                "execution_time": round(end_time - start_time, 3),
                "cpu_usage": cpu_usage,
                "memory_usage": memory_usage,
                "created_at": time.strftime("%Y-%m-%d %H:%M:%S"),
                "code": code,
                "current_run_dir": CURRENT_RUN_DIR,
                "database_dir": DATABASE_DIR
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
                "cpu_usage": "N/A",
                "memory_usage": "N/A",
                "created_at": time.strftime("%Y-%m-%d %H:%M:%S"),
                "code": code,
                "current_run_dir": CURRENT_RUN_DIR,
                "database_dir": DATABASE_DIR
            }


def main():

    server_address = ("", 5000)

    httpd = HTTPServer(server_address, SandboxHandler)

    print("Backend server running at http://localhost:5000")
    print("API: POST /run")
    print("API: GET /logs")
    print("Current run dir:", CURRENT_RUN_DIR)
    print("Database dir:", DATABASE_DIR)

    httpd.serve_forever()


if __name__ == "__main__":
    main()