const runBtn = document.getElementById("run-btn");
const outputBox = document.getElementById("output-box");

const cpuUsage = document.getElementById("cpu-usage");
const memoryUsage = document.getElementById("memory-usage");
const executionTime = document.getElementById("execution-time");

const codeEditor = document.getElementById("code-editor");
const historyList = document.getElementById("history-list");

const helloTest = document.getElementById("hello-test");
const compileTest = document.getElementById("compile-test");
const timeoutTest = document.getElementById("timeout-test");
const runtimeTest = document.getElementById("runtime-test");
const memoryTest = document.getElementById("memory-test");

function appendOutput(text) {
    outputBox.textContent += text + "\n";
    outputBox.scrollTop = outputBox.scrollHeight;
}

function getStatusClass(status) {
    if (status === "success") return "status-success";
    if (status === "compile_error") return "status-error";
    if (status === "timeout") return "status-warning";
    if (status === "runtime_error") return "status-runtime";
    if (status === "memory_limit_exceeded") return "status-warning";
    if (status === "security_violation") return "status-error";
    if (status === "sandbox_error") return "status-warning";
    return "status-default";
}

function addHistoryItem(result) {
    const historyItem = document.createElement("div");
    const statusClass = getStatusClass(result.status);

    historyItem.className = `history-item ${statusClass}`;

    historyItem.innerHTML = `
        <b>Status:</b> ${result.status}<br>
        <b>Exit Code:</b> ${result.exit_code}<br>
        <b>Time:</b> ${result.execution_time} s<br>
        <b>Run ID:</b> ${result.run_id}
    `;

    historyList.prepend(historyItem);

    while (historyList.children.length > 5) {
        historyList.removeChild(historyList.lastChild);
    }
}

helloTest.addEventListener("click", function () {
    codeEditor.value =
`#include <stdio.h>

int main() {
    printf("Hello Sandbox\\n");
    return 0;
}
`;
});

compileTest.addEventListener("click", function () {
    codeEditor.value =
`#include <stdio.h>

int main() {
    printf("Missing semicolon")
    return 0;
}
`;
});

timeoutTest.addEventListener("click", function () {
    codeEditor.value =
`int main() {
    while (1) {
    }
    return 0;
}
`;
});

runtimeTest.addEventListener("click", function () {
    codeEditor.value =
`int main() {
    int *p = 0;
    *p = 100;
    return 0;
}
`;
});

memoryTest.addEventListener("click", function () {
    codeEditor.value =
`#include <stdlib.h>

int main() {
    while (1) {
        malloc(1024 * 1024);
    }
    return 0;
}
`;
});

runBtn.addEventListener("click", async function () {
    const code = codeEditor.value.trim();

    if (code.length === 0) {
        outputBox.textContent = "";
        appendOutput("[ERROR] Code editor is empty.");
        return;
    }

    if (code.length > 5000) {
        outputBox.textContent = "";
        appendOutput("[ERROR] Code is too long. Please keep it under 5000 characters.");
        return;
    }

    runBtn.disabled = true;
    runBtn.textContent = "Running...";

    outputBox.textContent = "";

    cpuUsage.textContent = "CPU Usage: Waiting...";
    memoryUsage.textContent = "Memory Usage: Waiting...";
    executionTime.textContent = "Execution Time: Running...";

    appendOutput("[SYSTEM] Sending code to backend...");

    try {
        const response = await fetch("http://localhost:5000/run", {
            method: "POST",
            headers: {
                "Content-Type": "application/json"
            },
            body: JSON.stringify({
                code: codeEditor.value
            })
        });

        const result = await response.json();
        console.log(result);
        cpuUsage.textContent =
            "CPU Time: " + (result.cpu_usage || "N/A");
        memoryUsage.textContent =
            "Memory Usage: " + (result.memory_usage || "N/A");
        executionTime.textContent =
            "Execution Time: " + result.execution_time + " s";

        appendOutput("[SYSTEM] Backend response received.");
        appendOutput("");

        appendOutput("STATUS: " + result.status);
        appendOutput("EXIT CODE: " + result.exit_code);
        appendOutput("EXECUTION TIME: " + result.execution_time + " s");

        appendOutput("");
        appendOutput("===== STDOUT =====");
        appendOutput(result.stdout || "(empty)");

        appendOutput("");
        appendOutput("===== STDERR =====");
        appendOutput(result.stderr || "(empty)");

        addHistoryItem(result);

    } catch (error) {
        cpuUsage.textContent = "CPU Usage: N/A";
        memoryUsage.textContent = "Memory Usage: N/A";
        executionTime.textContent = "Execution Time: Failed";

        appendOutput("[ERROR] Failed to connect backend.");
        appendOutput(error.toString());

    } finally {
        runBtn.disabled = false;
        runBtn.textContent = "Run in Sandbox";
    }
});
