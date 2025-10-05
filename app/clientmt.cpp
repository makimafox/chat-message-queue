#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

struct msg_buffer {
    long msg_type;
    int client_pid;
    char msg_text[256];
};

int msgid;
int current_pid;
volatile int running = 1;

// =========================
// Thread รับข้อความ
// =========================
void* receive_messages(void* arg) {
    struct msg_buffer msg;
    while (running) {
        if (msgrcv(msgid, &msg, sizeof(msg) - sizeof(long), current_pid, 0) >= 0) {
            printf("\n[Message Received]: %s\n", msg.msg_text);
            printf("เขียนข้อความ: ");
            fflush(stdout);
        }
    }
    return NULL;
}

// =========================
// ส่งข้อความจากไฟล์
// command: "say" หรือ "dm"
// target: room name หรือ client PID
// =========================
void send_messages_from_file(const char* command, const char* target, const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) { perror("fopen"); return; }

    char line[256];
    struct msg_buffer message;
    message.msg_type = 1; // ส่งไป server
    message.client_pid = current_pid;

    while (fgets(line, sizeof(line), file)) {
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';

        if (strcmp(command, "say") == 0) {
            snprintf(message.msg_text, sizeof(message.msg_text), "say %s %s", target, line);
        } else if (strcmp(command, "dm") == 0) {
            snprintf(message.msg_text, sizeof(message.msg_text), "dm %s %s", target, line);
        }

        if (msgsnd(msgid, &message, sizeof(message) - sizeof(long), 0) == -1) {
            perror("msgsnd failed");
        } else {
            printf("[Sent]: %s\n", message.msg_text);
        }

        usleep(100000); // delay 0.1s
    }

    fclose(file);
}

// =========================
// MAIN
// =========================
int main() {
    key_t key;
    struct msg_buffer message;

    current_pid = getpid();

    key = ftok("progfile", 65);
    msgid = msgget(key, 0666 | IPC_CREAT);
    if (msgid == -1) { perror("msgget"); exit(1); }

    pthread_t recv_tid;
    pthread_create(&recv_tid, NULL, receive_messages, NULL);

    printf("Client started. พิมพ์ 'quit' เพื่อออก\n");
    printf("client id: %d\n", current_pid);

    while (1) {
        printf("เขียนข้อความ: ");
        if (fgets(message.msg_text, sizeof(message.msg_text), stdin) == NULL) break;

        size_t len = strlen(message.msg_text);
        if (len > 0 && message.msg_text[len-1] == '\n') message.msg_text[len-1] = '\0';
        if (strcmp(message.msg_text, "quit") == 0) break;

        // ถ้า user พิมพ์ "file command target filename"
        if (strncmp(message.msg_text, "file ", 5) == 0) {
            char cmd[10], target[50], filename[100];
            int n = sscanf(message.msg_text + 5, "%s %s %s", cmd, target, filename);
            if (n == 3) {
                send_messages_from_file(cmd, target, filename);
            } else {
                printf("ใช้: file <say|dm> <room|pid> <filename>\n");
            }
            continue;
        }

        // ส่งข้อความปกติแบบ interactive
        message.msg_type = 1;
        message.client_pid = current_pid;
        if (msgsnd(msgid, &message, sizeof(message) - sizeof(long), 0) == -1) {
            perror("msgsnd failed");
            exit(1);
        }
    }

    running = 0;
    pthread_cancel(recv_tid);
    pthread_join(recv_tid, NULL);
    msgctl(msgid, IPC_RMID, NULL);

    return 0;
}