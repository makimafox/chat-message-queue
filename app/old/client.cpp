#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

// Updated message buffer to include client's PID
struct msg_buffer {
    long msg_type;
    int client_pid;
    char msg_text[256];
};

int msgid;
int current_pid;

void* receive_messages(void* arg)
{
    struct msg_buffer msg;
    while (1)
    {
        // Receive messages with msg_type equal to the client's PID
        if (msgrcv(msgid, &msg, sizeof(msg) - sizeof(long), current_pid, 0) >= 0)
        {
            printf("\n[Message Received]: %s\n", msg.msg_text);
            printf("เขียนข้อความ: ");
            fflush(stdout);
        }
    }
    return NULL;
}

int main()
{
    key_t key;
    struct msg_buffer message;

    // Use getpid() to get a unique PID for the client
    current_pid = getpid();
    
    // Create or get the message queue key
    key = ftok("progfile", 65);
    msgid = msgget(key, 0666 | IPC_CREAT);
    if (msgid == -1) { perror("msgget"); exit(1); }

    pthread_t tid;
    pthread_create(&tid, NULL, receive_messages, NULL);

    printf("Client started. พิมพ์ 'quit' เพื่อออก\n");
    printf("client id: %d\n", current_pid);

    while (1)
    {
        printf("เขียนข้อความ: ");
        if (fgets(message.msg_text, sizeof(message.msg_text), stdin) == NULL) break;

        size_t len = strlen(message.msg_text);
        if (len > 0 && message.msg_text[len-1] == '\n') message.msg_text[len-1] = '\0';
        if (strcmp(message.msg_text, "quit") == 0) break;
        
        // Send to server with msg_type = 1 and include the client's PID
        message.msg_type = 1;
        message.client_pid = current_pid;
        if (msgsnd(msgid, &message, sizeof(message) - sizeof(long), 0) == -1)
        {
            perror("msgsnd failed");
            exit(1);
        }
    }
    
    // Clean up
    pthread_cancel(tid);
    pthread_join(tid, NULL);
    msgctl(msgid, IPC_RMID, NULL);

    return 0;
}