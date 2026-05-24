const runBtn = document.getElementById("run-btn");
const outputBox = document.getElementById("output-box");

const cpuUsage = document.getElementById("cpu-usage");
const memoryUsage = document.getElementById("memory-usage");
const executionTime = document.getElementById("execution-time");

const codeEditor = document.getElementById("code-editor");
const historyList = document.getElementById("history-list");

const helloTest=document.getElementById("hello-test");
const compileTest=document.getElementById("compile-test");
const timeoutTest=document.getElementById("timeout-test");
const runtimeTest=document.getElementById("runtime-test");
const memoryTest=document.getElementById("memory-test");

let dbHistoryVisible=false;
let dbHistoryPanel=null;


function appendOutput(text){

    outputBox.textContent+=text+"\n";

    outputBox.scrollTop=
    outputBox.scrollHeight;
}


function clearOutput(){

    outputBox.textContent="";
}


function getStatusClass(status){

    if(status==="success")
        return "status-success";

    if(status==="compile_error")
        return "status-error";

    if(status==="timeout")
        return "status-warning";

    if(status==="runtime_error")
        return "status-runtime";

    if(status==="memory_limit_exceeded")
        return "status-warning";

    if(status==="security_violation")
        return "status-error";

    if(status==="sandbox_error")
        return "status-warning";

    return "status-default";
}



function showResult(result,title){

    clearOutput();

    appendOutput(title);

    appendOutput("");

    appendOutput(
    "STATUS: "+
    result.status
    );

    appendOutput(
    "EXIT CODE: "+
    result.exit_code
    );

    appendOutput(
    "EXECUTION TIME: "+
    result.execution_time+
    " s"
    );

    appendOutput("");

    appendOutput(
    "===== STDOUT ====="
    );

    appendOutput(
    result.stdout||
    "(empty)"
    );

    appendOutput("");

    appendOutput(
    "===== STDERR ====="
    );

    appendOutput(
    result.stderr||
    "(empty)"
    );

}



function loadHistoryToEditor(result){

    codeEditor.value=
    result.code;

    cpuUsage.textContent=
    "CPU Usage: "+
    (result.cpu_usage||"N/A");

    memoryUsage.textContent=
    "Memory Usage: "+
    (result.memory_usage||"N/A");

    executionTime.textContent=
    "Execution Time: "+
    (result.execution_time||"N/A")
    +" s";

    showResult(
    result,
    "[SYSTEM] History loaded"
    );

}



function createHistoryCard(
result,
showCreatedAt
){

const historyItem=
document.createElement(
"div"
);

const statusClass=
getStatusClass(
result.status
);

historyItem.className=
`history-item ${statusClass}`;


const titleLine=

showCreatedAt ?

`<b>Created:</b>
${result.created_at}<br>`

:

"";


historyItem.innerHTML=`

<div class="history-content">

<div class="history-info">

${titleLine}

<b>Status:</b>
${result.status}<br>

<b>Exit Code:</b>
${result.exit_code}<br>

<b>Time:</b>
${result.execution_time}s

</div>

<button
type="button"
class="history-edit-btn">

Edit

</button>

</div>

`;


const editBtn=

historyItem.querySelector(
".history-edit-btn"
);


editBtn.addEventListener(

"click",

function(e){

e.stopPropagation();

loadHistoryToEditor(
result
);

}

);

return historyItem;

}



function addHistoryItem(result){

const historyItem=
createHistoryCard(
result,
false
);

historyList.prepend(
historyItem
);

while(
historyList.children.length>4
){

historyList.removeChild(
historyList.lastChild
);

}

}



async function toggleDatabaseHistory(){

dbHistoryPanel=
document.getElementById(
"db-history-list"
);

dbHistoryVisible=
!dbHistoryVisible;

const panel=
document.getElementById(
"db-history-panel"
);

if(
!dbHistoryVisible
){

panel.style.display=
"none";

return;

}

panel.style.display=
"block";

dbHistoryPanel.innerHTML=
"Loading...";


try{

const response=
await fetch(
"http://localhost:5000/history"
);

const data=
await response.json();

dbHistoryPanel.innerHTML=
"";

for(
const item
of data.history
){

dbHistoryPanel.appendChild(

createHistoryCard(
item,
true
)

);

}

}
catch(error){

dbHistoryPanel.innerHTML=
"Failed";

}

}



document
.getElementById(
"view-db-btn"
)
.addEventListener(

"click",

toggleDatabaseHistory

);



helloTest.onclick=function(){

codeEditor.value=

`#include <stdio.h>

int main(){

printf("Hello Sandbox\\n");

return 0;

}
`;

};



compileTest.onclick=function(){

codeEditor.value=

`#include <stdio.h>

int main(){

printf("Missing")

return 0;

}
`;

};



timeoutTest.onclick=function(){

codeEditor.value=

`int main(){

while(1){

}

return 0;

}
`;

};



runtimeTest.onclick=function(){

codeEditor.value=

`int main(){

int *p=0;

*p=100;

return 0;

}
`;

};



memoryTest.onclick=function(){

codeEditor.value=

`#include <stdlib.h>

int main(){

while(1){

malloc(
1024*1024
);

}

return 0;

}
`;

};



runBtn.onclick=async function(){

clearOutput();

runBtn.disabled=true;

runBtn.textContent=
"Running...";


try{

const response=
await fetch(

"http://localhost:5000/run",

{

method:"POST",

headers:{

"Content-Type":
"application/json"

},

body:JSON.stringify({

code:
codeEditor.value

})

}

);

const result=
await response.json();

cpuUsage.textContent=
"CPU Usage: "+
(result.cpu_usage||"N/A");

memoryUsage.textContent=
"Memory Usage: "+
(result.memory_usage||"N/A");

executionTime.textContent=
"Execution Time: "+
(result.execution_time||"N/A")
+" s";

showResult(
result,
"[SYSTEM] Backend response received"
);

addHistoryItem(
result
);

}
catch(error){

appendOutput(
error.toString()
);

}

finally{

runBtn.disabled=
false;

runBtn.textContent=
"Run in Sandbox";

}

}