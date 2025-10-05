#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

#define MAX_MSG 256

struct msg_buffer {
    long msg_type;
    int client_pid;
    char msg_text[MAX_MSG];
};

int msgid;
volatile int running = 1;

typedef struct {
    int client_id;
    int num_messages;
    const char* target; // room or PID for say/dm
} ClientThreadArgs;

// Thread รับข้อความ
void* receive_messages(void* arg) {
    int client_pid = *(int*)arg;
    struct msg_buffer msg;

    while (running) {
        if (msgrcv(msgid, &msg, sizeof(msg) - sizeof(long), client_pid, IPC_NOWAIT) >= 0) {
            printf("[Client %d Received]: %s\n", client_pid, msg.msg_text);
        }
        usleep(1000); // avoid busy wait
    }
    return NULL;
}

// Thread client จำลอง
void* client_thread(void* arg) {
    ClientThreadArgs* args = (ClientThreadArgs*)arg;
    int pid = args->client_id;

    pthread_t recv_tid;
    pthread_create(&recv_tid, NULL, receive_messages, &args->client_id);

    struct msg_buffer msg;
    msg.msg_type = 1;
    msg.client_pid = pid;

    for (int i = 0; i < args->num_messages; i++) {
        snprintf(msg.msg_text, sizeof(msg.msg_text), "say %s Hello from client %d msg %d", args->target, pid, i+1);
        if (msgsnd(msgid, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            perror("msgsnd failed");
        }
        usleep(10000); // 10ms delay
    }

    pthread_cancel(recv_tid);
    pthread_join(recv_tid, NULL);
    return NULL;
}

int main() {
    // ====== กำหนดค่าตรงนี้ ======
    int n_clients = 10;          // จำนวน client จำลอง
    int messages_per_client = 50; // จำนวนข้อความต่อ client
    const char* room_name = "room1"; // room ที่จะส่งข้อความ
    // ==============================

    key_t key = ftok("progfile", 65);
    msgid = msgget(key, 0666 | IPC_CREAT);
    if (msgid == -1) { perror("msgget"); exit(1); }

    pthread_t* threads = (pthread_t*)malloc(sizeof(pthread_t) * n_clients);
    ClientThreadArgs* args = (ClientThreadArgs*)malloc(sizeof(ClientThreadArgs) * n_clients);

    srand(time(NULL));

    for (int i = 0; i < n_clients; i++) {
        args[i].client_id = 1000 + i; // fake PID for each thread
        args[i].num_messages = messages_per_client;
        args[i].target = room_name;
        pthread_create(&threads[i], NULL, client_thread, &args[i]);
    }

    for (int i = 0; i < n_clients; i++) {
        pthread_join(threads[i], NULL);
    }

    msgctl(msgid, IPC_RMID, NULL);
    free(threads);
    free(args);

    return 0;
}