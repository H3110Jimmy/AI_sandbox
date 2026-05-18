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

let timer = 0;
let monitorInterval;

function appendOutput(text) {
    outputBox.textContent += text + "\n";
    outputBox.scrollTop = outputBox.scrollHeight;
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

    outputBox.textContent = "";

    timer = 0;

    clearInterval(monitorInterval);

    appendOutput("[SYSTEM] Sending code to backend...");

    monitorInterval = setInterval(function () {

        let cpu = Math.floor(Math.random() * 60) + 10;
        let memory = Math.floor(Math.random() * 200) + 20;

        timer += 1;

        cpuUsage.textContent =
            "CPU Usage: " + cpu + "%";

        memoryUsage.textContent =
            "Memory Usage: " + memory + " MB";

        executionTime.textContent =
            "Execution Time: " + timer + " s";

    }, 1000);

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

        clearInterval(monitorInterval);

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
        const historyItem = document.createElement("div");

        historyItem.className = "history-item";

        historyItem.innerHTML =
        `
        <b>Status:</b> ${result.status}<br>
        <b>Exit Code:</b> ${result.exit_code}<br>
        <b>Time:</b> ${result.execution_time} s<br>
        <b>Run ID:</b> ${result.run_id}
        `;

        historyList.prepend(historyItem);
        } catch (error) {

          clearInterval(monitorInterval);

          appendOutput("[ERROR] Failed to connect backend.");
          appendOutput(error.toString());
          }

});
