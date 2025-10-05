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
#include <future>

using namespace std;

// =============================
// Message Struct
// =============================
struct msg_buffer {
    long msg_type;
    int client_pid;
    char msg_text[256];
};

int msgid; // Global message queue ID

// =============================
// Client Class
// =============================
class Client {
public:
    string name;
    int id;
    Client(string n, int i) : name(n), id(i) {}

    void boardcast(const string &text) {
        msg_buffer msg;
        msg.msg_type = id;
        strncpy(msg.msg_text, text.c_str(), sizeof(msg.msg_text) - 1);
        msg.msg_text[sizeof(msg.msg_text) - 1] = '\0';

        if (msgsnd(msgid, &msg, sizeof(msg) - sizeof(long), 0) == -1)
            perror("msgsnd to client failed");
    }
};

// =============================
// Room Class (Multithreaded Broadcast)
// =============================
class Room {
public:
    string room_name;
    vector<Client *> members;
    mutex members_mtx;

    Room(string n) : room_name(n) {}

    void join(Client *client) {
        lock_guard<mutex> lock(members_mtx);
        members.push_back(client);
        cout << "[Join] " << client->name << " joined " << room_name << endl;
    }

    bool leave(Client *client) {
        lock_guard<mutex> lock(members_mtx);
        for (auto it = members.begin(); it != members.end(); ++it) {
            if ((*it)->id == client->id) {
                members.erase(it);
                cout << "[Leave] " << client->name << " left " << room_name << endl;
                return true;
            }
        }
        return false;
    }

    // ✅ Multithreaded Broadcast
    void BoardCast(const string &text) {
        lock_guard<mutex> lock(members_mtx);
        cout << "[BROADCAST][" << room_name << "]: " << text << endl;

        vector<future<void>> tasks;
        for (auto c : members) {
            tasks.push_back(async(launch::async, [=]() {
                msg_buffer msg;
                msg.msg_type = c->id;
                strncpy(msg.msg_text, text.c_str(), sizeof(msg.msg_text) - 1);
                msg.msg_text[sizeof(msg.msg_text) - 1] = '\0';
                if (msgsnd(msgid, &msg, sizeof(msg) - sizeof(long), 0) == -1)
                    perror("msgsnd failed");
                else
                    cout << "[Sent][" << room_name << " -> " << c->name << "] " << text << endl;
            }));
        }

        // Wait for all to finish
        for (auto &t : tasks)
            t.get();
    }
};

// =============================
// Broadcast Task Struct
// =============================
struct BroadcastTask {
    string cmd;
    string room_name;
    string message;
    int client_pid;
};

// =============================
// Router Class (Multithreaded)
// =============================
class Router {
private:
    map<int, Client *> clients;
    map<string, Room *> rooms;
    queue<BroadcastTask> taskQueue;
    mutex mtx;
    condition_variable cv;
    bool running = true;
    vector<thread> workers;

public:
    Router(int _msgid) { msgid = _msgid; }

    Client *CreateOrFindClient(int client_id) {
        if (clients.find(client_id) != clients.end())
            return clients[client_id];
        Client *newclient = new Client(to_string(client_id), client_id);
        clients[client_id] = newclient;
        return newclient;
    }

    Room *CreateOrFindRoom(string name) {
        if (rooms.find(name) != rooms.end())
            return rooms[name];
        Room *newroom = new Room(name);
        rooms[name] = newroom;
        return newroom;
    }

    // =============== Task Queue ===============
    void enqueueTask(BroadcastTask task) {
        {
            lock_guard<mutex> lock(mtx);
            taskQueue.push(task);
        }
        cv.notify_one();
    }

    // =============== Worker Thread ===============
    void workerThread() {
        while (running) {
            BroadcastTask task;
            {
                unique_lock<mutex> lock(mtx);
                cv.wait(lock, [&] { return !taskQueue.empty() || !running; });
                if (!running && taskQueue.empty()) break;
                task = taskQueue.front();
                taskQueue.pop();
            }

            Client *client = CreateOrFindClient(task.client_pid);

            if (task.cmd == "join") {
                Room *room = CreateOrFindRoom(task.room_name);
                room->join(client);
            } else if (task.cmd == "say") {
                Room *room = CreateOrFindRoom(task.room_name);
                room->BoardCast(task.message); // ✅ Multithreaded broadcast
            } else if (task.cmd == "dm") {
                Client *target = CreateOrFindClient(stoi(task.room_name)); // dm <pid> text
                target->boardcast(task.message);
            } else if (task.cmd == "leave") {
                Room *room = CreateOrFindRoom(task.room_name);
                room->leave(client);
            } else if (task.cmd == "pulse") {
                cout << "[Info] " << client->name << " [Online]" << endl;
            }
        }
    }

    // =============== Receiver Thread ===============
    void receiverThread() {
        while (running) {
            msg_buffer message;
            if (msgrcv(msgid, &message, sizeof(message) - sizeof(long), 1, 0) < 0) {
                perror("msgrcv failed");
                break;
            }

            int clientID = message.client_pid;
            char cmd[10] = "", roomname[50] = "", text[200] = "";
            int n = sscanf(message.msg_text, "%s %s %[^\n]", cmd, roomname, text);

            BroadcastTask task;
            task.cmd = cmd;
            task.room_name = (n >= 2) ? roomname : "";
            task.message = (n == 3) ? text : "";
            task.client_pid = clientID;

            enqueueTask(task);
        }
    }

    // =============== Start & Stop ===============
    void start(int numWorkers = 3) {
        cout << "Router started. Spawning " << numWorkers << " workers..." << endl;

        for (int i = 0; i < numWorkers; ++i)
            workers.emplace_back(&Router::workerThread, this);

        receiverThread(); // main receiver loop
    }

    void stop() {
        {
            lock_guard<mutex> lock(mtx);
            running = false;
        }
        cv.notify_all();

        for (auto &w : workers)
            if (w.joinable())
                w.join();
    }

    ~Router() {
        stop();
        for (auto const &pair : rooms)
            delete pair.second;
        for (auto const &pair : clients)
            delete pair.second;
        msgctl(msgid, IPC_RMID, NULL);
    }
};

// =============================
// MAIN
// =============================
int main() {
    key_t key = ftok("progfile", 65);
    msgid = msgget(key, 0666 | IPC_CREAT);
    if (msgid == -1) {
        perror("msgget failed");
        return 1;
    }

    Router router(msgid);
    router.start(4); // 4 worker threads

    return 0;
}