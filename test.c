//CPU time
int main() {
    while(1);
    return 0;
}

//memory
#include <stdlib.h>
#include <string.h>
int main() {
    while(1) {
        void *p = malloc(1024 * 1024);
        if (p) memset(p, 0, 1024 * 1024);
    }
    return 0;
}

//forkbomb
#include <unistd.h>
int main() {
    while(1) fork();
    return 0;
}

//fdbomb
#include <fcntl.h>
int main() {
    while(1) open("/dev/null", O_RDONLY);
    return 0;
}

//seccomp
#define _GNU_SOURCE
#include <sched.h>
int main() {
    unshare(CLONE_NEWNS);
    return 0;
}

//pid
#include <signal.h>
int main() {
    kill(1, SIGKILL);
    return 0;
}

//mount pivot
#include <stdio.h>
int main() {
    FILE *f = fopen("../../../../../../../../../etc/shadow", "r");
    if (f) {
        char ch;
        while((ch = fgetc(f)) != EOF) putchar(ch);
        fclose(f);
    }
    return 0;
}

//network
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main() {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    inet_pton(AF_INET, "8.8.8.8", &addr.sin_addr);
    connect(s, (struct sockaddr *)&addr, sizeof(addr));
    return 0;
}