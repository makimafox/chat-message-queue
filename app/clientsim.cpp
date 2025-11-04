#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>
#include <sys/time.h>

struct msg_buffer {
    long msg_type;
    int client_pid;
    char msg_text[256];
    long long send_timestamp;
};

int msgid;
int current_pid;
volatile int running = 1;


// Thread รับข้อความ
void* receive_messages(void* arg) {
    struct msg_buffer msg;
    while (running) {
        if (msgrcv(msgid, &msg, sizeof(msg) - sizeof(long), current_pid, 0) >= 0) {
            struct timeval now;
            gettimeofday(&now, NULL);
            long long recv_time = (long long)now.tv_sec * 1000000LL + now.tv_usec;
            long long latency_us = recv_time - msg.send_timestamp;

            printf("\n%s\n", msg.msg_text);
            printf("[Latency]: %.3f ms\n", latency_us / 1000.0);
            fflush(stdout);
        }
    }
    return NULL;
}


// ส่งข้อความจากไฟล์
void send_messages_from_file(const char* command, const char* target, const char* filename) {
    // เปิดไฟล์
    FILE* file = fopen(filename, "r");
    if (!file) { perror("fopen"); return; }

    char line[256];
    struct msg_buffer message;
    message.msg_type = 1;
    message.client_pid = current_pid;


    // อ่านแต่ละบรรทัดและส่งข้อความ
    while (fgets(line, sizeof(line), file)) {
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';

        struct timeval tv;
        gettimeofday(&tv, NULL);
        message.send_timestamp = (long long)tv.tv_sec * 1000000LL + tv.tv_usec;

        if (strcmp(command, "say") == 0)
            snprintf(message.msg_text, sizeof(message.msg_text), "say %s %s", target, line);
        else if (strcmp(command, "dm") == 0)
            snprintf(message.msg_text, sizeof(message.msg_text), "dm %s %s", target, line);

        if (msgsnd(msgid, &message, sizeof(message) - sizeof(long), 0) == -1)
            perror("msgsnd failed");
        else
            printf("[Sent]: %s\n", message.msg_text);
        // รอเล็กน้อยระหว่างการส่งข้อความ
        usleep(100000);
    }

    fclose(file);
}


// โปรแกรมหลัก
int main() {
    int num_clients;
    char group_name[64];

    printf("Enter number of clients: ");
    scanf("%d", &num_clients);
    getchar(); // consume newline
    printf("Enter group name: ");
    fgets(group_name, sizeof(group_name), stdin);
    size_t len = strlen(group_name);
    if (len > 0 && group_name[len-1] == '\n') group_name[len-1] = '\0';

    key_t key = ftok("progfile", 65);
    msgid = msgget(key, 0666 | IPC_CREAT);
    if (msgid == -1) { perror("msgget"); exit(1); }

    for (int i = 0; i < num_clients; ++i) {
        pid_t pid = fork();
        if (pid == 0) { // child process = client
            current_pid = getpid();
            pthread_t recv_tid;
            pthread_create(&recv_tid, NULL, receive_messages, NULL);

            printf("Client %d started. PID: %d\n", i+1, current_pid);

            // Join group (room)
            struct msg_buffer join_msg;
            join_msg.msg_type = 1;
            join_msg.client_pid = current_pid;
            struct timeval tv;
            gettimeofday(&tv, NULL);
            join_msg.send_timestamp = (long long)tv.tv_sec * 1000000LL + tv.tv_usec;
            snprintf(join_msg.msg_text, sizeof(join_msg.msg_text), "join %s", group_name);
            if (msgsnd(msgid, &join_msg, sizeof(join_msg) - sizeof(long), 0) == -1)
                perror("msgsnd failed (join)");
            else
                printf("[Client %d] Joined group: %s\n", i+1, group_name);

            // Say with file
            send_messages_from_file("say", group_name, "test/fixed.txt");

            sleep(1); // allow time for messages to be received
            running = 0;
            pthread_cancel(recv_tid);
            pthread_join(recv_tid, NULL);
            exit(0);
        }
    }

    // wait child processes
    for (int i = 0; i < num_clients; ++i) {
        wait(NULL);
    }

    return 0;
}