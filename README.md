# Chat-message-queue

This project implements a basic chat application in C++ using Posix Message Queue (IPC) for communication between Server and Client.


## Features

- Join – A user can join the chat room with a chosen username
- Say – Send a broadcast message to all users in the chat room
- DM (Direct Message) – Send a private message to a specific user
- Leave – Leave the chat room (without quitting the program)
- Quit – Exit the client program


## Project Structure

```

├── app/
│   ├── server.cpp        # Server program
│   ├── client.cpp        # Client program
│   ├── compile.sh        # Compile code script
│   └── clearipc.sh       # Kill all mq
│
├── Dockerfile     # Dockerfile
├── Build.sh       # Build script
├── exec.sh        # Exec docker container
└── README.md      # Project description
```
    
## Build & Run

Clone the project

```bash
  git clone https://link-to-project
```

Go to the project directory

```bash
  cd my-project
```

Install dependencies

```bash
  ./build.sh
```

or

```bash
  docker build -t chat .

  docker run -it --name chat-mq --hostname chinatsu chat bash
```

When run in container


```bash
  cd app
```


Compile

```bash
  ./compile.sh
```

or

```bash
  gcc client.cpp -o client

  gcc server.cpp -o server
```

Run

```bash
  ./server

  ./client
```


## Execute docker

Run exec

```bash
  ./exec.sh
```

and then cd app and compile


# Client Application - Message Queue System

## Overview
โปรแกรม Client นี้เป็นส่วนหนึ่งของระบบ Chat แบบ Distributed Chat System ที่ใช้ System V Message Queue เป็นกลไกในการสื่อสารระหว่าง Client และ Server/Router โดย Client สามารถส่งข้อความไปยัง Room หรือส่งข้อความ Direct Message (DM) ไปยัง Client อื่นได้

## Features
* **การส่งข้อความแบบ Real-time**
    * ส่งข้อความผ่าน Message Queue ไปยัง Server
    * รองรับคำสั่ง `say` (ส่งข้อความในห้อง) และ `dm` (ส่งข้อความส่วนตัว)
* **การรับข้อความแบบ Multi-threading**
    * ใช้ `pthread` สร้าง Thread แยกสำหรับรับข้อความ
    * รับข้อความได้ตลอดเวลาโดยไม่รบกวนการพิมพ์ของผู้ใช้
* **การวัด Latency**
    * คำนวณเวลาแฝง (Latency) ของข้อความแต่ละข้อความ
    * แสดงผลเป็นมิลลิวินาที (ms) พร้อมความละเอียด 3 ตำแหน่งทศนิยม
* **การส่งข้อความจากไฟล์ (Batch Messaging)**
    * รองรับการอ่านข้อความจากไฟล์และส่งทีละบรรทัด
    * มีการหน่วงเวลา 100ms ระหว่างการส่งแต่ละข้อความ

## Data Structure
```cpp
struct msg_buffer {
    long msg_type;              // ประเภทข้อความ (1 = ส่งไป Server, PID = รับจาก Server)
    int client_pid;             // Process ID ของ Client ผู้ส่ง
    char msg_text[256];         // เนื้อหาข้อความ (สูงสุด 256 ตัวอักษร)
    long long send_timestamp;   // เวลาส่งข้อความ (microseconds)
};
````

## การทำงานของโปรแกรม


### 1\. Initialization (การเตรียมการ)

Client จะทำการเชื่อมต่อกับ Message Queue ที่มีอยู่ (ซึ่ง Server สร้างไว้) โดยใช้ `ftok` และ `msgget`

  * ใช้ `ftok()` สร้าง unique key จากไฟล์ `progfile` และ project ID `65`
  * ต้องใช้ key เดียวกับ Server เพื่อเชื่อมต่อกับ Queue เดียวกัน
  * เก็บ `current_pid` (Process ID) ของตัวเองไว้ เพื่อใช้เป็น `msg_type` ในการรับข้อความ

<!-- end list -->

```cpp
current_pid = getpid();          // เก็บ Process ID ของตัวเอง
key = ftok("progfile", 65);      // สร้าง Key สำหรับ Message Queue
msgid = msgget(key, 0666 | IPC_CREAT);  // เชื่อมต่อกับ Message Queue
```


### 2\. Receiving Thread (การรับข้อความ)

โปรแกรมจะสร้าง Thread แยก (`receive_messages`) เพื่อรอรับข้อความจาก Server ตลอดเวลา

```cpp
void* receive_messages(void* arg) {
    while (running) {
        // กรองเฉพาะข้อความที่มี msg_type = current_pid (ข้อความที่ส่งมาหาตัวเอง)
        if (msgrcv(msgid, &msg, sizeof(msg) - sizeof(long), 
                   current_pid, MSG_NOERROR) >= 0) {
            
            // คำนวณ Latency
            long long recv_time = now.tv_sec * 1000000LL + now.tv_usec;
            long long latency_us = recv_time - msg.send_timestamp;
            
            // แสดงข้อความและ Latency
            printf("%s\n", msg.msg_text);
            printf("[Latency]: %.3f ms\n", latency_us / 1000.0);
        }
    }
}
```


**จุดเด่น:**

  * ใช้ `MSG_NOERROR` flag เพื่อป้องกัน error เมื่อข้อความยาวเกิน buffer (ระบบจะตัดข้อความส่วนเกินทิ้ง)
  * คำนวณ latency โดยเปรียบเทียบ timestamp ตอนส่งกับตอนรับ

### 3\. Sending Messages (การส่งข้อความ)

**3.1 ส่งข้อความทั่วไป**
ผู้ใช้พิมพ์คำสั่ง (เช่น `say` หรือ `dm`) ข้อความจะถูกบรรจุใน `struct msg_buffer` และส่งไปยัง Server

```cpp
message.msg_type = 1;              // ส่งไปยัง Server (Server จะอ่าน type 1)
message.client_pid = current_pid;  // ระบุตัวตน
message.send_timestamp = ...;      // บันทึกเวลาส่ง
msgsnd(msgid, &message, ...);      // ส่งข้อความ
```

**3.2 ส่งข้อความจากไฟล์**
รองรับคำสั่ง `file` เพื่ออ่านข้อความจากไฟล์และส่งทีละบรรทัด

```bash
# ส่งข้อความในไฟล์ messages.txt ไปยังห้อง room1
file say room1 messages.txt

# ส่งข้อความส่วนตัวในไฟล์ private.txt ไปยัง PID 12345
file dm 12345 private.txt
```

## Commands

| คำสั่ง | รูปแบบ | คำอธิบาย |
| :--- | :--- | :--- |
| `say` | `say <room_name> <message>` | ส่งข้อความในห้องสาธารณะ |
| `dm` | `dm <target_pid> <message>` | ส่งข้อความส่วนตัว |
| `file` | `file <say\|dm> <target> <filename>` | ส่งข้อความจากไฟล์ |
| `quit` | `quit` | ออกจากโปรแกรม |

## ตัวอย่างการใช้งาน

### เริ่มต้นโปรแกรม

```bash
$ gcc client.cpp -o client -lpthread
$ ./client
Client started. พิมพ์ 'quit' เพื่อออก
client id: 12345
เขียนข้อความ: 
```

### ส่งข้อความในห้อง

```
เขียนข้อความ: say lobby Hello everyone!
```

### ส่งข้อความจากไฟล์

```
เขียนข้อความ: file say lobby announcements.txt
[Sent]: say lobby First announcement
[Sent]: say lobby Second announcement
...
```

### แสดงผลเมื่อได้รับข้อความ

(ข้อความนี้จะแสดงแทรกขึ้นมาได้ทุกเมื่อ โดยไม่ขัดจังหวะการพิมพ์)

```
[Server → You]: Welcome to the chat!
[Latency]: 2.345 ms
เขียนข้อความ: 
```

## การวัด Latency

โปรแกรมคำนวณ latency โดย

1.  บันทึก timestamp ตอนส่ง (microsecond precision) ด้วย `gettimeofday()`
    ```cpp
    struct timeval tv;
    gettimeofday(&tv, NULL);
    message.send_timestamp = (long long)tv.tv_sec * 1000000LL + tv.tv_usec;
    ```
2.  คำนวณความต่างเมื่อรับข้อความกลับมาใน Receiving Thread
    ```cpp
    long long latency_us = recv_time - msg.send_timestamp;
    printf("[Latency]: %.3f ms\n", latency_us / 1000.0);
    ```

## Thread Safety และการจัดการ Thread

### การสร้าง Thread

```cpp
pthread_t recv_tid;
pthread_create(&recv_tid, NULL, receive_messages, NULL);
```

### การปิดโปรแกรมอย่างปลอดภัย

เมื่อผู้ใช้พิมพ์ `quit` โปรแกรมจะ

  ตั้งค่า `running = 0` เพื่อให้ loop ใน `receive_messages` หยุดทำงาน
  ใช้ `pthread_cancel(recv_tid)` เพื่อบังคับให้ `msgrcv()` ที่กำลัง "บล็อก" อยู่ ถูกขัดจังหวะและคืนค่า (return)
  ใช้ `pthread_join(recv_tid, NULL)` เพื่อรอให้ receiving thread จบการทำงานอย่างสมบูรณ์ก่อนปิดโปรแกรม

<!-- end list -->

```cpp
running = 0;                    // หยุด loop ใน receive thread
pthread_cancel(recv_tid);       // ปลดบล็อก msgrcv()
pthread_join(recv_tid, NULL);   // รอให้ thread จบการทำงาน
```

> **หมายเหตุ:** Client ไม่ควร ลบ Message Queue (`msgctl` with `IPC_RMID`) เพราะ Server และ Client อื่นๆ อาจยังใช้งานอยู่ ให้ Server เป็นผู้จัดการลบ Queue เมื่อปิดระบบ

##  Requirements

  * **OS:** Linux/Unix (เนื่องจากใช้ System V IPC)
  * **Compiler:** GCC / G++ (รองรับ C++11)
  * **Libraries:**
      * `pthread` (POSIX threads)
      * `sys/msg.h` (Message Queue)
      * `sys/time.h` (High-resolution timing)

## การ Compile

ใช้ `gcc`:

```bash
gcc client.cpp -o client -lpthread
```

หรือใช้ `g++`:

```bash
g++ client.cpp -o client -lpthread
```

## จุดเด่นของการออกแบบ

  * **Non-blocking UI:** ใช้ multi-threading ทำให้รับ-ส่งข้อความไม่รบกวนกัน
  * **High-precision latency:** ใช้ `gettimeofday()` วัดเวลาแม่นยำถึง microsecond
  * **Batch messaging:** รองรับการส่งข้อความจากไฟล์
  * **Safe cleanup:** ใช้ `pthread_cancel()` ปลดบล็อก blocking call ได้อย่างปลอดภัย
  * **Buffer overflow protection:** ใช้ `MSG_NOERROR` flag

## ข้อจำกัด (Limitations)

  * ข้อความต้องไม่เกิน 256 ตัวอักษร
  * ต้องมี Server/Router ทำงานอยู่ก่อน
  * ใช้งานได้เฉพาะบน Linux/Unix systems
  * ไม่รองรับการเข้ารหัสข้อความ (plaintext only)

## การพัฒนาต่อ (Future Improvements)

  - [ ] เพิ่มการเข้ารหัสข้อความ (Encryption)
  - [ ] รองรับการส่งไฟล์ (File transfer)
  - [ ] เพิ่ม GUI interface
  - [ ] รองรับ Unicode และ Emoji
  - [ ] เพิ่มระบบ authentication

