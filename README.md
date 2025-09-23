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

