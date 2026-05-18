from http.server import HTTPServer, BaseHTTPRequestHandler
import json
import os
import subprocess
import time
import uuid

BASE_DIR = os.path.dirname(os.path.abspath(__file__))

RUNS_DIR = os.path.join(BASE_DIR, "runs")
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

        source_path = os.path.join(run_dir, "main.c")
        binary_path = os.path.join(run_dir, "main")

        with open(source_path, "w") as f:
            f.write(code)

        start_time = time.time()

        compile_cmd = [
            "gcc",
            source_path,
            "-o",
            binary_path
        ]

        compile_result = subprocess.run(
            compile_cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
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
                "created_at": time.strftime("%Y-%m-%d %H:%M:%S"),
                "code": code
            }

        try:

            exec_result = subprocess.run(
                [binary_path],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=3
            )

            end_time = time.time()

            return {
                "run_id": run_id,
                "status": (
                    "success"
                    if exec_result.returncode == 0
                    else "runtime_error"
                ),
                "stdout": exec_result.stdout,
                "stderr": exec_result.stderr,
                "exit_code": exec_result.returncode,
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
                "stderr": (
                    "Program timeout: "
                    "execution exceeded 3 seconds"
                ),
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
