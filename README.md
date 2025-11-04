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

---

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


---


# Chat Server with Message Queue

โปรเจกต์นี้เป็น **Multi-threaded Chat Server** ที่ใช้ System V Message Queue สำหรับการสื่อสารระหว่าง Server และ Client โดยรองรับการส่งข้อความแบบห้องแชท (Room) และข้อความส่วนตัว (Direct Message)

##  ภาพรวมของระบบ

Server นี้ทำหนาที่เป็นตัวกลาง (Router) ในการจัดการการสื่อสารระหว่าง Client หลายตัว โดยใช้:
- **Message Queue** สำหรับการส่ง-รับข้อความระหว่าง Process
- **Thread Pool** สำหรับจัดการ Task แบบ Concurrent
- **Room-based Chat** สำหรับการแชทกลุ่ม
- **Direct Messaging** สำหรับการส่งข้อความส่วนตัว

##  สถาปัตยกรรมของระบบ

### 1. **Message Buffer Structure**
```cpp
struct msg_buffer {
    long msg_type;           // ID ของผู้รับข้อความ
    int client_pid;          // ID ของผู้ส่ง
    char msg_text[256];      // เนื้อหาข้อความ
    long long send_timestamp; // เวลาที่ส่ง (microseconds)
}
```

### 2. **คลาสหลักในระบบ**

#### **ThreadPool**
- จัดการ Worker Threads สำหรับประมวลผล Task แบบ Concurrent
- ใช้ Queue และ Condition Variable สำหรับการกระจาย Task
- รองรับการตั้งค่าจำนวน Thread ตามต้องการ

#### **Client**
- เก็บข้อมูล Client แต่ละคน (ID, Name)
- มีฟังก์ชัน `boardcast()` สำหรับส่งข้อความส่วนตัว (DM)
- รูปแบบข้อความ DM: `[Recieved Message from <SenderID> to <TargetID>]: <Text>`

#### **Room**
- จัดการห้องแชทและสมาชิกในห้อง
- ฟังก์ชัน `join()` สำหรับเข้าร่วมห้อง
- ฟังก์ชัน `leave()` สำหรับออกจากห้อง
- ฟังก์ชัน `BoardCast()` สำหรับส่งข้อความไปยังสมาชิกทุกคนในห้อง
- รูปแบบข้อความ Broadcast: `[Recieved Message from <SenderID> in room <RoomName>]: <Text>`

#### **Router**
- เป็นหัวใจหลักของ Server
- จัดการ Client และ Room ทั้งหมด
- ประมวลผลคำสั่งต่างๆ ที่ได้รับจาก Client
- ส่งข้อความ Error/Info กลับไปยัง Client

## Protocol การสื่อสาร

### Message Type Convention
- **Type 1**: ข้อความที่ส่งมายัง Server (Router)
- **Type = Client ID**: ข้อความที่ Server ส่งกลับไปยัง Client นั้นๆ

### รูปแบบคำสั่ง

| คำสั่ง | รูปแบบ | คำอธิบาย |
|--------|--------|----------|
| `join` | `join <room_name>` | เข้าร่วมห้องแชท |
| `leave` | `leave <room_name>` | ออกจากห้องแชท |
| `say` | `say <room_name> <message>` | ส่งข้อความในห้อง |
| `dm` | `dm <target_client_id> <message>` | ส่งข้อความส่วนตัว |
| `online` | `online` | ดูรายชื่อ Client ที่ออนไลน์ |
| `help` | `help` | แสดงคำสั่งทั้งหมด |

## การติดตั้งและการใช้งาน

### Requirements
- Linux/Unix OS (ต้องรองรับ System V IPC)
- g++ compiler with C++11 support
- pthread library

### Compilation
```bash
g++ -std=c++11 -pthread server.cpp -o server
```

### Running the Server
```bash
./server
```

เมื่อเริ่มโปรแกรม Server จะถามจำนวน Thread ที่ต้องการใช้ใน Thread Pool
```
Enter number of threads in pool: 4
```

## การทำงานของ Server

### 1. **Initialization**
- สร้าง Message Queue ด้วย `ftok()` และ `msgget()`
- Key ที่ใช้: `ftok("progfile", 65)`
- Permission: `0666`

### 2. **Message Processing Flow**
```
Client → Message Queue (Type 1) → Router.handleMessage()
                                        ↓
                                   Parse Command
                                        ↓
                                   Execute Action
                                        ↓
                                   Send Response
                                        ↓
                                Message Queue (Type = Client ID) → Client
```

### 3. **Error Handling**
- ตรวจสอบความถูกต้องของคำสั่งและพารามิเตอร์
- ส่งข้อความ Error กลับไปยัง Client พร้อม Timestamp
- Log ข้อผิดพลาดใน Server Console

### 4. **Server Console Output**

Server จะแสดงข้อมูลการทำงานแบบ Real-time:

**Join/Leave Events:**
```
[Join][123][To][general]
[Left][456][From][lobby]
```

**Broadcast Messages:**
```
[BROADCAST][From:123][To:general]: Hello everyone!
```

**Direct Messages:**
```
[Route DM][From:123][To:456]: Hi there!
[DM][456]: [Recieved Message from 123 to 456]: Hi there!
```

**Info/Error Messages:**
```
[SendInfo][123]: Joined room general successfully
[SendError][456]: Room not found: unknown_room
```

## Features

### Implemented
- [x] Multi-threaded message processing
- [x] Room-based group chat
- [x] Direct messaging between clients
- [x] Dynamic room creation
- [x] Client online status checking
- [x] Comprehensive error handling
- [x] Timestamp tracking (microseconds precision)
- [x] Thread-safe operations
- [x] Automatic client/room management

### Key Features
1. **Thread Pool**: ปรับแต่งจำนวน Thread ได้ตามความต้องการ
2. **Non-blocking**: ใช้ Thread Pool ทำให้ Server ไม่ติดขัดเมื่อมี Task หนัก
3. **Scalable**: รองรับ Client และ Room จำนวนมาก
4. **Reliable**: มี Error handling ครอบคลุมทุกจุด
5. **Real-time Logging**: แสดงสถานะการทำงานแบบ Real-time

## Debugging

### Server Console จะแสดง:
- การเข้า-ออกห้อง
- ข้อความที่ส่งในห้องและ DM
- ข้อผิดพลาดต่างๆ
- สถานะการทำงานของ ThreadPool

### Common Issues:
1. **ftok failed**: ตรวจสอบว่ามีไฟล์ "progfile" หรือไม่
2. **msgget failed**: ตรวจสอบ Permission และ IPC limits
3. **Invalid thread count**: ระบุจำนวน Thread ที่เป็นบวก

## Implementation Notes

### Thread Safety
- ใช้ `mutex` ป้องกันการเข้าถึง shared data
- `lock_guard` สำหรับ automatic unlocking
- Thread-safe message queue operations

### Memory Management
- ใช้ `new`/`delete` สำหรับจัดการ Client และ Room
- Cleanup ใน Router destructor
- Error handling สำหรับ memory allocation failures

### Performance Optimization
- Thread Pool ลดค่าใช้จ่ายในการสร้าง Thread
- Enqueue tasks แทนการประมวลผลแบบ Sequential
- Non-blocking message sending

## Message Queue Cleanup

Server จะทำการลบ Message Queue เมื่อปิดโปรแกรม:
```cpp
msgctl(msgid, IPC_RMID, nullptr)
```

หากต้องการลบ Queue ด้วยตนเอง:
```bash
ipcs -q              # ดู message queues
ipcrm -q <msgid>     # ลบ queue
```

## Future Enhancements

- [ ] รองรับ Authentication
- [ ] เพิ่ม Room password protection
- [ ] Private room creation
- [ ] Message history/logging
- [ ] File sharing capability
- [ ] Encryption สำหรับข้อความ
- [ ] Web interface

## 📄 License

This project is open source and available for educational purposes.

---

**Note**: โปรเจกต์นี้ใช้สำหรับการเรียนรู้เกี่ยวกับ Inter-Process Communication และ Multi-threading ใน C++
