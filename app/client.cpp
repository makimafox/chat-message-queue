#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>
#include <unistd.h>
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

// =========================
// Thread รับข้อความ (ลบ [Message Received]: ทั้งหมด)
// =========================
// ... (ส่วนโครงสร้างและตัวแปร global เดิม)

// =========================
// Thread รับข้อความ (แก้ไขการแสดงผล)
// =========================
void* receive_messages(void* arg) {
    struct msg_buffer msg;
    while (running) {
        // ใช้ MSG_NOERROR เพื่อป้องกัน buffer overflow หรือ error เมื่อข้อความยาวเกิน
        if (msgrcv(msgid, &msg, sizeof(msg) - sizeof(long), current_pid, MSG_NOERROR) >= 0) {
            struct timeval now;
            gettimeofday(&now, NULL);
            long long recv_time = (long long)now.tv_sec * 1000000LL + now.tv_usec;
            long long latency_us = recv_time - msg.send_timestamp;

            // 1. ใช้ \r เพื่อกลับไปที่ต้นบรรทัด
            printf("\r"); 
            
            // 2. **ลบบรรทัดเคลียร์ช่องว่างออก** เพื่อไม่ให้เกิดช่องว่างเยอะเกินไป
            // printf("%-200s", ""); 

            // 3. **ไม่ต้องกลับไปที่ต้นบรรทัดอีก** เพราะ \r แรกเพียงพอแล้ว
            // printf("\r");
            
            // 4. แสดงผลข้อความที่ได้รับ (msg.msg_text) ตามด้วย \n
            printf("%s\n", msg.msg_text);
            
            // 5. แสดง Latency
            printf("[Latency]: %.3f ms\n", latency_us / 1000.0);
            
            // 6. แสดง prompt "เขียนข้อความ: " ขึ้นมาใหม่ทันที
            printf("เขียนข้อความ: ");
            fflush(stdout); // บังคับให้แสดงผลทันที
        }
    }
    return NULL;
}

// ... (ส่วนที่เหลือของโค้ด main และ send_messages_from_file ยังคงเดิม)

// =========================
// ส่งข้อความจากไฟล์
// =========================
void send_messages_from_file(const char* command, const char* target, const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) { perror("fopen"); return; }

    char line[256];
    struct msg_buffer message;
    message.msg_type = 1; // ส่งไปยัง Server/Router (msg_type 1)
    message.client_pid = current_pid;

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

        usleep(100000); // หน่วงเวลา 100ms ระหว่างข้อความ
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
    // ต้องใช้ key เดียวกับฝั่ง Server/Router
    key = ftok("progfile", 65); 
    msgid = msgget(key, 0666 | IPC_CREAT);
    if (msgid == -1) { perror("msgget"); exit(1); }

    pthread_t recv_tid;
    pthread_create(&recv_tid, NULL, receive_messages, NULL);

    printf("Client started. พิมพ์ 'quit' เพื่อออก\n");
    printf("client id: %d\n", current_pid);

    while (1) {
        printf("เขียนข้อความ: ");
        fflush(stdout); // บังคับให้แสดง Prompt ทันที

        if (fgets(message.msg_text, sizeof(message.msg_text), stdin) == NULL) break;

        size_t len = strlen(message.msg_text);
        if (len > 0 && message.msg_text[len-1] == '\n') message.msg_text[len-1] = '\0';
        if (strcmp(message.msg_text, "quit") == 0) break;

        if (strncmp(message.msg_text, "file ", 5) == 0) {
            char cmd[10], target[50], filename[100];
            int n = sscanf(message.msg_text + 5, "%s %s %s", cmd, target, filename);
            if (n == 3) send_messages_from_file(cmd, target, filename);
            else printf("ใช้: file <say|dm> <room|pid> <filename>\n");
            continue;
        }

        message.msg_type = 1; // ส่งไปยัง Server/Router (msg_type 1)
        message.client_pid = current_pid;
        struct timeval tv;
        gettimeofday(&tv, NULL);
        // เก็บ timestamp เป็น microseconds
        message.send_timestamp = (long long)tv.tv_sec * 1000000LL + tv.tv_usec;

        if (msgsnd(msgid, &message, sizeof(message) - sizeof(long), 0) == -1)
            perror("msgsnd failed");
    }

    running = 0;
    // ใช้ pthread_cancel เพื่อปลดบล็อก msgrcv ที่รออยู่
    pthread_cancel(recv_tid); 
    pthread_join(recv_tid, NULL);
    // ลบ msgctl(msgid, IPC_RMID, NULL); ออก เพราะ client ไม่ควรเป็นคนลบ queue
    
    return 0;
}