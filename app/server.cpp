#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <unistd.h>
#include <cstring>
#include <map>
#include <functional>
#include <stdexcept>
#include <ctime>
#include <chrono>
#include <algorithm>

using namespace std;
using namespace std::chrono;


int CONFIG_BC_THREAD;


struct msg_buffer {
    long msg_type;
    int client_pid;
    char msg_text[256];
    long long send_timestamp;
};

int msgid;

// ThreadPool สำหรับจัดการ concurrent tasks
class ThreadPool {
    vector<thread> workers;
    queue<function<void()>> tasks;
    mutex queue_mutex;
    condition_variable cv;
    bool stop = false;

public:
    ThreadPool(size_t threads = thread::hardware_concurrency()) {
        if (threads == 0)
            threads = 2;
        try {
            for (size_t i = 0; i < threads; ++i) {
                workers.emplace_back([this] {
                    while (true) {
                        function<void()> task;
                        {
                            unique_lock<mutex> lock(queue_mutex);
                            cv.wait(lock, [this] { return stop || !tasks.empty(); });
                            if (stop && tasks.empty()) return;
                            task = std::move(tasks.front());
                            tasks.pop();
                        }
                        try {
                            task();
                        } catch (const exception &e) {
                            cerr << "[ThreadPool] Task error: " << e.what() << endl;
                        } catch (...) {
                            cerr << "[ThreadPool] Unknown error in task.\n";
                        }
                    }
                });
            }
        } catch (const exception &e) {
            cerr << "[ThreadPool] Failed to create threads: " << e.what() << endl;
            stop = true;
        }
    }

    template <class F>
    void enqueue(F &&f) {
        if (stop) return;
        {
            unique_lock<mutex> lock(queue_mutex);
            tasks.emplace(std::forward<F>(f));
        }
        cv.notify_one();
    }

    ~ThreadPool() {
        {
            unique_lock<mutex> lock(queue_mutex);
            stop = true;
        }
        cv.notify_all();
        for (auto &w : workers) {
            if (w.joinable()) w.join();
        }
    }
};

// คลาส Client
class Client {
public:
    string name;
    int id;

    Client(string n, int i) : name(std::move(n)), id(i) {}

    // รับ senderID เข้ามา
    void boardcast(const string &text, long long timestamp, int senderID) {
        if (text.empty()) {
            cerr << "[Client][" << id << "] Empty message ignored.\n";
            return;
        }

        // แสดงใน Server Console ให้ชัดเจนว่าข้อความ DM ไปหาใคร
        cout << "[DM][" << id << "]: " << text << endl;

        msg_buffer msg{};
        msg.msg_type = id;
        msg.client_pid = id;
        
        // การสร้างข้อความ: [Recieved Message from <SenderID> to <TargetID>]: <Text>
        const string dm_text = "[Recieved Message from " + to_string(senderID) + " to " + to_string(this->id) + "]: " + text;
        strncpy(msg.msg_text, dm_text.c_str(), sizeof(msg.msg_text) - 1);
        
        msg.send_timestamp = timestamp;

        if (msgsnd(msgid, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            perror("[Client] msgsnd failed");
        }
    }
};

// คลาส Room
class Room {
public:
    string room_name;
    vector<Client *> members;
    mutex members_mtx;

    explicit Room(string n) : room_name(std::move(n)) {}

    void join(Client *client) {
        if (!client) {
            cerr << "[Room][" << room_name << "] Null client ignored.\n";
            return;
        }
        lock_guard<mutex> lock(members_mtx);
        // ป้องกันการ join ซ้ำ
        if (std::find(members.begin(), members.end(), client) != members.end()) {
            cerr << "[Join][" << client->name << "][To][" << room_name << "] already joined.\n";
            return;
        }
        members.push_back(client);
        cout << "[Join][" << client->name << "][To][" << room_name << "]\n";
    }

    bool leave(Client *client) {
        if (!client) return false;
        lock_guard<mutex> lock(members_mtx);
        for (auto it = members.begin(); it != members.end(); ++it) {
            if ((*it)->id == client->id) {
                members.erase(it);
                cout << "[Left][" << client->name << "][From][" << room_name << "]\n";
                return true;
            }
        }
        cerr << "[Leave][" << client->name << "] not found in [" << room_name << "]\n";
        return false;
    }

    // รับ senderID เข้ามา และปรับ Console Output
    void BoardCast(const string &text, ThreadPool &pool, long long timestamp, int senderID) {
        if (text.empty()) {
            cerr << "[Room][" << room_name << "] Empty broadcast ignored.\n";
            return;
        }

        // NEW Server Console Output: แสดง SenderID และ Room Name
        cout << "[BROADCAST][From:" << senderID << "][To:" << room_name << "]: " << text << endl; 

        lock_guard<mutex> lock(members_mtx);
        
        // คำนำหน้าสำหรับ BoardCast (SAY) ให้แสดง SenderID และ RoomName**
        const string broadcast_text = "[Recieved Message from " + to_string(senderID) + " in room " + this->room_name + "]: " + text;

        for (auto c : members) {
            if (!c) continue;
            pool.enqueue([=]() {
                msg_buffer msg{};
                msg.msg_type = c->id;
                msg.client_pid = c->id;
                // ใช้ข้อความที่มีคำนำหน้า
                strncpy(msg.msg_text, broadcast_text.c_str(), sizeof(msg.msg_text) - 1);
                msg.send_timestamp = timestamp;

                if (msgsnd(msgid, &msg, sizeof(msg) - sizeof(long), 0) == -1)
                    perror("[Room] msgsnd to client failed");
            });
        }
    }
};

// คลาส Router
class Router {
private:
    map<int, Client *> clients;
    map<string, Room *> rooms;
    ThreadPool pool;

    // ส่งข้อความ error กลับไปยัง client
    void sendErrorToClient(int clientID, const string &err, long long timestamp = 0) const {
        if (clientID <= 0 || err.empty()) return;

        msg_buffer reply{};
        reply.msg_type = clientID;
        reply.client_pid = clientID;
        strncpy(reply.msg_text, ("[ERROR] " + err).c_str(), sizeof(reply.msg_text) - 1);
        reply.send_timestamp = timestamp ? timestamp : duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();

        if (msgsnd(msgid, &reply, sizeof(reply) - sizeof(long), 0) == -1)
            perror("[Router] Failed to send error to client");
        else
            cout << "[SendError][" << clientID << "]: " << err << endl;
    }

    // ส่งข้อความ success
    void sendInfoToClient(int clientID, const string &msg, long long clientTimestamp) const {
        if (clientID <= 0 || msg.empty()) return;

        // ใช้เวลาแบบ microseconds ให้ตรงกับ client
        auto now_us = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();

        msg_buffer reply{};
        reply.msg_type = clientID;
        reply.client_pid = clientID;

    
        snprintf(reply.msg_text, sizeof(reply.msg_text), "[INFO] %s", msg.c_str());

        reply.send_timestamp = now_us;

        if (msgsnd(msgid, &reply, sizeof(reply) - sizeof(long), 0) == -1) {
            perror("[Router] Failed to send info to client");
        } else {
            cout << "[SendInfo][" << clientID << "]: " << msg << endl;
        }
    }

public:
    explicit Router(int _msgid) : pool(CONFIG_BC_THREAD) { msgid = _msgid; }

    Client *CreateOrFindClient(int client_id) {
        if (client_id <= 0) {
            cerr << "[Router] Invalid client id: " << client_id << endl;
            return nullptr;
        }
        if (clients.find(client_id) != clients.end())
            return clients[client_id];

        try {
            Client *newclient = new Client(to_string(client_id), client_id);
            clients[client_id] = newclient;
            return newclient;
        } catch (const bad_alloc &) {
            cerr << "[Router] Memory allocation failed for client.\n";
            return nullptr;
        }
    }

    Room *CreateOrFindRoom(const string &name, bool createIfMissing = true) {
        if (name.empty()) {
            cerr << "[Router] Empty room name ignored.\n";
            return nullptr;
        }

        auto it = rooms.find(name);
        if (it != rooms.end()) return it->second;

        if (!createIfMissing) return nullptr;

        try {
            Room *newroom = new Room(name);
            rooms[name] = newroom;
            return newroom;
        } catch (const bad_alloc &) {
            cerr << "[Router] Memory allocation failed for room.\n";
            return nullptr;
        }
    }

    void start() {
        cout << "[Router] Started. Waiting for messages..." << endl;
        while (true) {
            msg_buffer message{};
            // รับข้อความจาก message type 1 (เป็น convention สำหรับ router/server)
            ssize_t result = msgrcv(msgid, &message, sizeof(message) - sizeof(long), 1, 0); 
            if (result < 0) {
                perror("[Router] msgrcv failed");
                this_thread::sleep_for(chrono::milliseconds(200));
                continue;
            }

            pool.enqueue([=]() {
                try {
                    handleMessage(message);
                } catch (const exception &e) {
                    cerr << "[Router] handleMessage exception: " << e.what() << endl;
                } catch (...) {
                    cerr << "[Router] Unknown error in handleMessage.\n";
                }
            });
        }
    }

    void handleMessage(const msg_buffer &message) {
        int clientID = message.client_pid; // นี่คือ Sender ID
        if (clientID <= 0) {
            sendErrorToClient(clientID, "Invalid client ID", message.send_timestamp);
            return;
        }

        char cmd[32] = {0}, roomname[64] = {0}, text[200] = {0};
        // พยายาม parse: cmd [roomname/targetID] [text...]
        int n = sscanf(message.msg_text, "%31s %63s %199[^\n]", cmd, roomname, text);
        if (n <= 0) {
            sendErrorToClient(clientID, "Invalid message format", message.send_timestamp);
            return;
        }

        string cmdStr = cmd;
        string roomStr = (n >= 2) ? roomname : "";
        string textStr = (n == 3) ? text : "";

        Client *client = CreateOrFindClient(clientID);
        if (!client) {
            sendErrorToClient(clientID, "Client creation failed", message.send_timestamp);
            return;
        }

        // JOIN
        if (cmdStr == "join") {
            if (n < 2) {
                sendErrorToClient(clientID, "Missing room name in join command", message.send_timestamp);
                return;
            }
            if (n > 2) {
                // อนุญาตให้มีแค่ 'join roomname' เท่านั้น
                sendErrorToClient(clientID, "Unexpected extra text after join command", message.send_timestamp);
                return;
            }
            if (Room *room = CreateOrFindRoom(roomStr)) {
                room->join(client);
                sendInfoToClient(clientID, "Joined room " + roomStr + " successfully", message.send_timestamp);
            } else {
                sendErrorToClient(clientID, "Cannot join room: " + roomStr, message.send_timestamp);
            }
        }

        // SAY
        else if (cmdStr == "say") {
            if (n < 2) {
                sendErrorToClient(clientID, "Missing room name in say command", message.send_timestamp);
                return;
            }
            if (n < 3 || textStr.empty()) {
                sendErrorToClient(clientID, "Missing message text in say command", message.send_timestamp);
                return;
            }
            Room *room = CreateOrFindRoom(roomStr, false); // ไม่สร้างถ้าไม่มี
            if (!room) {
                sendErrorToClient(clientID, "Room not found: " + roomStr, message.send_timestamp);
                return;
            }
            // ส่ง clientID (Sender)
            room->BoardCast(textStr, pool, message.send_timestamp, clientID);
        }

        // DM
        else if (cmdStr == "dm" && n >= 3) {
            try {
                int targetID = stoi(roomStr); // targetID อยู่ในตำแหน่ง roomStr
                
                // แสดงใน Server Console ให้ชัดเจนว่า DM ไปหาใคร
                cout << "[Route DM][From:" << clientID << "][To:" << targetID << "]: " << textStr << endl;
                
                if (Client *target = CreateOrFindClient(targetID))
                    // ส่ง clientID (Sender) ไปด้วย
                    target->boardcast(textStr, message.send_timestamp, clientID);
                else
                    sendErrorToClient(clientID, "Target client not found", message.send_timestamp);
            } catch (...) {
                sendErrorToClient(clientID, "Invalid target client ID: " + roomStr, message.send_timestamp);
            }
        }

        // LEAVE
        else if (cmdStr == "leave") {
            if (n < 2) {
                sendErrorToClient(clientID, "Missing room name in leave command", message.send_timestamp);
                return;
            }
            if (n > 2) {
                sendErrorToClient(clientID, "Unexpected extra text after leave command", message.send_timestamp);
                return;
            }
            Room *room = CreateOrFindRoom(roomStr, false);
            if (!room) {
                sendErrorToClient(clientID, "Room not found: " + roomStr, message.send_timestamp);
                return;
            }
            bool ok = room->leave(client);
            if (!ok)
                sendErrorToClient(clientID, "You are not in room: " + roomStr, message.send_timestamp);
            else
                sendInfoToClient(clientID, "Left room " + roomStr + " successfully", message.send_timestamp);
        }

        // online (ใช้ตรวจสอบสถานะ client)
        else if (cmdStr == "online") {
            // ส่ง clientID (Sender)
            string list;
            for (const auto &p : clients) {
                if (!list.empty()) list += ", ";
                list += to_string(p.first);
            }
            string info = "Online clients: [" + list + "]";
            sendInfoToClient(clientID, info, message.send_timestamp);
            cout << "[Info][" << client->name << "][Online] " << info << endl;

            // แจ้ง client อื่น ๆ ว่า client นี้ออนไลน์
            for (const auto &p : clients) {
                int otherID = p.first;
                if (otherID == clientID) continue;
                sendInfoToClient(otherID, "Client " + to_string(clientID) + " is online", message.send_timestamp);
            }
        }

        else if (cmdStr == "help") {
            string helpMsg =
                "Available commands:\n"
                "1. join <room_name> - Join a chat room\n"
                "2. leave <room_name> - Leave a chat room\n"
                "3. say <room_name> <message> - Send message to a room\n"
                "4. dm <target_client_id> <message> - Direct message to a client\n"
                "5. online - List online clients\n"
                "6. help - Show this help message\n";
            sendInfoToClient(clientID, helpMsg, message.send_timestamp);
        }

        // handle unknown command
        else {
            sendErrorToClient(clientID, "Unknown command: " + cmdStr, message.send_timestamp);
        }
    }

    ~Router() {
        for (auto &p : rooms) delete p.second;
        for (auto &p : clients) delete p.second;

        if (msgctl(msgid, IPC_RMID, nullptr) == -1) {
            perror("[Router] msgctl remove failed");
        }
    }
};

// ฟังก์ชัน main
int main() {
    // กำหนดค่า key สำหรับ Message Queue
    key_t key = ftok("progfile", 65);
    if (key == -1) {
        perror("[Main] ftok failed");
        return 1;
    }

    // สร้างหรือเข้าถึง Message Queue
    msgid = msgget(key, 0666 | IPC_CREAT);
    if (msgid == -1) {
        perror("[Main] msgget failed");
        return 1;
    }

    cout << "Enter number of threads in pool: ";
    if (!(cin >> CONFIG_BC_THREAD) || CONFIG_BC_THREAD <= 0) {
        cerr << "[Main] Invalid thread count, using default 4.\n";
        CONFIG_BC_THREAD = 4;
        cin.clear();
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
    }

    try {
        Router router(msgid);
        router.start();
    } catch (const exception &e) {
        cerr << "[Main] Router error: " << e.what() << endl;
    }

    // ลบ Message Queue ก่อนจบโปรแกรม (ทำใน destructor ของ Router แล้ว แต่ใส่ซ้ำเพื่อความมั่นใจ)
    if (msgctl(msgid, IPC_RMID, nullptr) == -1) {
        perror("[Main] msgctl remove failed on exit");
    }

    return 0;
}
