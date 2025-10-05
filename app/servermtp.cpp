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

using namespace std;

// =========================
// CONFIG
// =========================
int CONFIG_BC_THREAD = 4; // จำนวน thread ใน pool

// =========================
// Message Struct
// =========================
struct msg_buffer {
    long msg_type;
    int client_pid;
    char msg_text[256];
};

int msgid; // global message queue id

// =========================
// ThreadPool Implementation
// =========================
class ThreadPool {
    vector<thread> workers;
    queue<function<void()>> tasks;
    mutex queue_mutex;
    condition_variable cv;
    bool stop = false;

public:
    ThreadPool(size_t threads = thread::hardware_concurrency()) {
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
                    task();
                }
            });
        }
    }

    template <class F>
    void enqueue(F&& f) {
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
        for (auto& w : workers)
            w.join();
    }
};

// =========================
// Client Class
// =========================
class Client {
public:
    string name;
    int id;

    Client(string n, int i) : name(n), id(i) {}

    void boardcast(const string& text) {
        cout << "[DM][" << id << "]: " << text << endl;

        msg_buffer msg;
        msg.msg_type = id;
        strncpy(msg.msg_text, text.c_str(), sizeof(msg.msg_text) - 1);
        msg.msg_text[sizeof(msg.msg_text) - 1] = '\0';

        if (msgsnd(msgid, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
            perror("msgsnd to client failed");
        }
    }
};

// =========================
// Room Class
// =========================
class Room {
public:
    string room_name;
    vector<Client*> members;
    mutex members_mtx;

    Room(string n) : room_name(n) {}

    void join(Client* client) {
        lock_guard<mutex> lock(members_mtx);
        members.push_back(client);
        cout << "[Join][" << client->name << "][To][" << room_name << "]\n";
    }

    bool leave(Client* client) {
        lock_guard<mutex> lock(members_mtx);
        for (auto it = members.begin(); it != members.end(); ++it) {
            if ((*it)->id == client->id) {
                members.erase(it);
                cout << "[Left][" << client->name << "][From][" << room_name << "]\n";
                return true;
            }
        }
        return false;
    }

    void BoardCast(const string& text, ThreadPool& pool) {
        cout << "[BROADCAST][" << room_name << "]: " << text << endl;
        lock_guard<mutex> lock(members_mtx);

        for (auto c : members) {
            pool.enqueue([=]() {
                msg_buffer msg;
                msg.msg_type = c->id;
                strncpy(msg.msg_text, text.c_str(), sizeof(msg.msg_text) - 1);
                msg.msg_text[sizeof(msg.msg_text) - 1] = '\0';

                if (msgsnd(msgid, &msg, sizeof(msg) - sizeof(long), 0) == -1)
                    perror("msgsnd to client failed");
            });
        }
    }
};

// =========================
// Router Class
// =========================
class Router {
private:
    map<int, Client*> clients;
    map<string, Room*> rooms;
    ThreadPool pool; // ใช้ทั้ง router และ broadcast

public:
    Router(int _msgid) : pool(CONFIG_BC_THREAD) {
        msgid = _msgid;
    }

    Client* CreateOrFindClient(int client_id) {
        if (clients.find(client_id) != clients.end())
            return clients[client_id];

        Client* newclient = new Client(to_string(client_id), client_id);
        clients[client_id] = newclient;
        return newclient;
    }

    Room* CreateOrFindRoom(string name) {
        if (rooms.find(name) != rooms.end())
            return rooms[name];

        Room* newroom = new Room(name);
        rooms[name] = newroom;
        return newroom;
    }

    // -----------------------
    // Multithreaded start()
    // -----------------------
    void start() {
        cout << "[Router] Started. Waiting for messages..." << endl;

        while (true) {
            msg_buffer message;
            if (msgrcv(msgid, &message, sizeof(message) - sizeof(long), 1, 0) < 0) {
                perror("msgrcv failed");
                break;
            }

            // ✅ โยนการประมวลผล message ให้ ThreadPool
            pool.enqueue([=]() {
                handleMessage(message);
            });
        }
    }

    void handleMessage(const msg_buffer& message) {
        int clientID = message.client_pid;
        char cmd[10], roomname[50], text[200];
        int n = sscanf(message.msg_text, "%s %s %[^\n]", cmd, roomname, text);

        string cmdStr = cmd;
        string roomStr = (n >= 2) ? roomname : "";
        string textStr = (n == 3) ? text : "";

        Client* client = CreateOrFindClient(clientID);

        if (cmdStr == "join" && n >= 2) {
            Room* room = CreateOrFindRoom(roomStr);
            room->join(client);
        } else if (cmdStr == "say" && n >= 3) {
            Room* room = CreateOrFindRoom(roomStr);
            room->BoardCast(textStr, pool);
        } else if (cmdStr == "dm" && n >= 3) {
            Client* target = CreateOrFindClient(stoi(roomStr));
            target->boardcast(textStr);
        } else if (cmdStr == "leave" && n >= 2) {
            Room* room = CreateOrFindRoom(roomStr);
            room->leave(client);
        } else if (cmdStr == "Pulse") {
            cout << "[Info][" << client->name << "][Online]\n";
        } else {
            cout << "[Unknown command: " << cmdStr << "]\n";
        }
    }

    ~Router() {
        for (auto& p : rooms)
            delete p.second;
        for (auto& p : clients)
            delete p.second;
        msgctl(msgid, IPC_RMID, NULL);
    }
};

// =========================
// MAIN
// =========================
int main() {
    key_t key = ftok("progfile", 65);
    msgid = msgget(key, 0666 | IPC_CREAT);
    if (msgid == -1) {
        perror("msgget failed");
        return 1;
    }

    Router router(msgid);
    router.start();

    msgctl(msgid, IPC_RMID, NULL);
    return 0;
}