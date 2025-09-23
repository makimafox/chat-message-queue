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

using namespace std;

// Room boardcast only
int CONFIG_BC_THREAD = 1;

// Updated message buffer to receive the client's PID
struct msg_buffer
{
    long msg_type;
    int client_pid;
    char msg_text[256];
};

int msgid; // global msgid

class Client
{
public:
    string name;
    int id; // PID
    Client(string n, int i) : name(n), id(i) {}

    void boardcast(const string &text)
    {
        cout << "[BROADCAST][" << "To" << "]: " << text << endl;

        msg_buffer msg;
        strncpy(msg.msg_text, text.c_str(), sizeof(msg.msg_text) - 1);
        msg.msg_text[sizeof(msg.msg_text) - 1] = '\0';

        msg.msg_type = id;
        if (msgsnd(msgid, &msg, sizeof(msg) - sizeof(long), 0) == -1)
        {
            perror("msgsnd to client failed");
        }
    }
};

class Room
{
public:
    string room_name;
    vector<Client *> members;

    mutex members_mtx;
    Room(string n) : room_name(n) {}

    void join(Client *client, string room_name)
    {
        members.push_back(client);
        cout << "[Join]" << "[" << client->name << "]" << "[To]" << "[" << room_name << "]";
    }

    bool leave(Client *client)
    {
        for (auto it = members.begin(); it != members.end(); ++it)
        {
            if ((*it)->id == client->id)
            {
                members.erase(it);
                cout << "[Left] " << client->name << " [From] " << room_name << endl;
                return true;
            }
        }
        return false;
    }

    void BoardCast(const string &text)
    {
        cout << "[BROADCAST][" << room_name << "]: " << text << endl;

        msg_buffer msg;
        strncpy(msg.msg_text, text.c_str(), sizeof(msg.msg_text) - 1);
        msg.msg_text[sizeof(msg.msg_text) - 1] = '\0';

        lock_guard<mutex> lock(members_mtx);
        for (auto c : members)
        {
            msg.msg_type = c->id; // Send back to client PID
            if (msgsnd(msgid, &msg, sizeof(msg) - sizeof(long), 0) == -1)
            {
                perror("msgsnd to client failed");
            }
        }
    }
};

struct BroadcastTask
{
    string message;
    string room_name;      // For room broadcasts
    int recipient_pid = 0; // For direct messages
};

class Router
{
private:
    map<int, Client *> clients;
    map<string, Room *> rooms;

    queue<BroadcastTask> taskQueue;
    mutex mtx;
    condition_variable cv;
    bool running = true;

public:
    Router(int _msgid) { msgid = _msgid; }

    Client *CreateOrFindClient(int client_id)
    {
        if (clients.find(client_id) != clients.end())
        {
            return clients[client_id];
        }

        Client *newclient = new Client(to_string(client_id), client_id);
        clients[client_id] = newclient;

        return newclient;
    }

    Room *CreateOrFindRoom(string name)
    {
        if (rooms.find(name) != rooms.end())
        {
            return rooms[name];
        }
        Room *newroom = new Room(name);
        rooms[name] = newroom;

        return newroom;
    }

    void BoardCastWorker(int type, string roomname)
    {
    }

    void start()
    {

        while (true)
        {
            msg_buffer message;
            if (msgrcv(msgid, &message, sizeof(message) - sizeof(long), 1, 0) < 0)
            {
                perror("msgrcv failed");
                break;
            }

            // Print the message *before* parsing it with sscanf
            printf("[RCV][%d] %s\n", message.client_pid, message.msg_text);

            int clientID = message.client_pid;
            char cmd[10], roomname[50], text[200];
            int n = sscanf(message.msg_text, "%s %s %[^\n]", cmd, roomname, text);

            string cmdStr = cmd;
            string roomStr = roomname;
            string textStr = (n == 3) ? text : "";

            Client *client = CreateOrFindClient(clientID);

            if (cmdStr == "help" && n >= 1)
            {
                // boardcast only client
            }
            else if (cmdStr == "join" && n >= 2)
            {
                Room *room = CreateOrFindRoom(roomStr);
                room->join(client, room->room_name);
            }
            else if (cmdStr == "say" && n >= 2)
            {
                Room *room = CreateOrFindRoom(roomStr);
                room->BoardCast(text);
            }
            else if (cmdStr == "dm" && n >= 2)
            {
                Client *client = CreateOrFindClient(stoi(roomStr));
                client->boardcast(text);
            }
            else if (cmdStr == "leave" && n >= 2)
            {
                Room *room = CreateOrFindRoom(roomStr);
                room->leave(client);
            }
            else if (cmdStr == "Pulse" && n >= 2)
            {
                cout << "[Info]" << client->name << "[Online]";
            }
            else
            {
                // boardcast only client
            }
        }
    }

    ~Router()
    {
        for (auto const &pair : rooms)
            delete pair.second;
        for (auto const &pair : clients)
            delete pair.second;
        msgctl(msgid, IPC_RMID, NULL);
    }
};

int main()
{
    key_t key = ftok("progfile", 65);
    msgid = msgget(key, 0666 | IPC_CREAT);
    if (msgid == -1)
    {
        perror("msgget failed");
        return 1;
    }

    Router router(msgid);
    cout << "Router started. Waiting for clients..." << endl;
    router.start();
    msgctl(msgid, IPC_RMID, NULL);

    return 0;
}