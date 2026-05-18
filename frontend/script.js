const runBtn = document.getElementById("run-btn");
const outputBox = document.getElementById("output-box");

runBtn.addEventListener("click", function () {
    outputBox.textContent = "Running program in sandbox...\n";

    setTimeout(function () {
        outputBox.textContent += "Hello Sandbox\n";
        outputBox.textContent += "Program finished successfully.\n";
        outputBox.textContent += "Exit code: 0\n";
    }, 1000);
});
