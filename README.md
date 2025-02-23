# Simple Stream Server

`simple_stream_server` is a **simple TCP server** written in C that accepts client connections, stores received data in a file, and returns the full content of this file to the client.
Is supports multiple simultaneous connections using threads. Each incoming connection is handled by a separate thread, ensuring parallel processing of client requests.

It can run either in the foreground or as a **daemon**, allowing it to execute in the background.

This application is **designed to be used as an example** of a **External Package to be integrated into Buildroot** and configured to **start automatically** using **BusyBox init**.

## 📂 Repository Structure

- **`simple_stream_server.c`**: Main server source code.
- **`thread_list.c/h`**: Manages the linked list of active threads.
- **`connection_handler.c/h`**: Handles client connections in separate threads.
- **`server_utils.c/h`**: Contains helper functions for managing the server.
- **`Makefile`**: Script to compile the project.
- **`start-stop`**: Startup script compatible with BusyBox init.
- **`README.md`**: This documentation file.


## 🛠 Features

### 🔹 Easy Client Interaction
   - Clients can connect using `netcat (nc)` or `telnet`.
   - The server stores received messages and sends back the entire content of the file.

### 🔹 Daemon Mode Support
   - The server can run in **normal mode** or as a **background daemon**.

### 🔹 Multithreading Support
   - The server supports **multiple simultaneous connections**.

   - Each connection spawns a **new thread** to handle the interaction.

   - A **linked list** is used to manage active threads.

   - Threads are properly joined using `pthread_join()` (no detached threads).

### 🔹 Thread-Safe File Writing
   - A mutex (pthread_mutex_t********) ensures that data written by different clients does not intermix.

   - Example:
      - If one client writes `12345678` and another writes `abcdefg`, the file will always contain ordered entries like:
      12345678
      abcdefg

   - It will not result in interleaved data like `123abc456defg`.

### 🔹 Graceful Shutdown on SIGTERM/SIGINT
   - The server catches termination signals (SIGTERM, SIGINT).

   - When exiting, it requests all threads to terminate and waits for their completion.


## Using with Buildroot (as a External Package)

This package is integrated as as external package into [buildroot_external_example](https://github.com/moschiel/buildroot_external_example) and can be selected in `menuconfig`.

When included in a Buildroot-based system, the following steps are automatically handled: 

✅ Cloning this repository.

✅ Compiling simple_stream_server.

✅ Installing the executable in `/usr/bin/`.

✅ Installing the startup script (`start-stop`) in `/etc/init.d/`, so the server starts on boot.


## 🚀 Standalone Manual Compilation and Execution (Outside buildroot)
If you want to compile and execute this application as a standalone, do the following:

### 🔹 Compilation

1. **Clone the repository**:
   ```bash
   git clone https://github.com/moschiel/simple_stream_server.git
   cd simple_stream_server
   ```

2. **Compile the project** using the provided `Makefile`:
   ```bash
   make
   ```
   This will generate an executable called `simple_stream_server`.

### 🔹 Execution

Run the server with or without daemon mode:

- **Normal mode**:
  ```bash
  ./simple_stream_server
  ```

- **Daemon mode**:
  ```bash
  ./simple_stream_server -d
  ```

By default, the server listens on port `9000


## 🔄 Testing the Server

To test the server, use **`netcat (nc)`** or **`telnet`** as a client to send data and verify the response.

### **🔹 Step 1: Start the Server**
In one terminal, start the server:
```bash
./simple_stream_server
```

### **🔹 Step 2: Connect as a Client**
Open another terminal and use **netcat (`nc`)** to connect to the server:
```bash
nc localhost 9000
```

### **🔹 Step 3: Send Data**
Type a message and press `Enter`. For example:
```text
Hello, Server!
```

The server will store this text in the file and send it back. 📩

### **🔹 Step 4: Verify the Response**
After sending the data, the server will respond with **the full content of the stored file**. If you send multiple messages, you will see all of them concatenated.

#### **Example of interaction with the server**
```
$ nc localhost 9000
Hello, Server!    # <- Client sending
Hello, Server!    # <- Server returns the current file content

$ nc localhost 9000
Another message   # <- Client sends a new message
Hello, Server!    # <- Server returns the stored history
Another message
```

---

## 🔄 Configuring Auto-Start on Boot with BusyBox init (Outside Buildroot)

If running **outside Buildroot**, you can manually configure the server to start at boot **just like Buildroot does** using the included `start-stop` script.

### 🔹 Manually Installing the Startup Script
1. **Move the server executable to `/usr/bin/` (so it can be executed from any directory):**:  
   ```bash
   sudo cp simple_stream_server /usr/bin/
   sudo chmod +x /usr/bin/simple_stream_server
   ```

2. **Move the `start-stop` script to `/etc/init.d/` and rename to `S99simple_stream_server` (so it runs at boot):**  
   ```bash
   sudo cp start-stop /etc/init.d/S99simple_stream_server
   sudo chmod +x /etc/init.d/S99simple_stream_server
   ```
   **BusyBox init** uses the beginnig of the file name **(SXX)** to identify startup priority of the init.d script.

   A lower number starts earlier; a higher number starts later.

   That's why it starts with **S99**, where **S** means **start** and **99** indicates it is probably the last one to be executed.


**Note:** These steps **manually replicate** what **Buildroot does automatically** when the package is installed ([buildroot_external_example](https://github.com/moschiel/buildroot_external_example)). If using [buildroot_external_example](https://github.com/moschiel/buildroot_external_example), you **do not need to do this manually**—the system will handle it during the build process.

---


📌 **Summary:**  
- The server **stores all received messages** and **returns the full file content** at the end of each interaction.  
- Whenever a client connects, it **receives the complete stored history**.
- **Integrated with Buildroot** in the repository [buildroot_external_example](https://github.com/moschiel/buildroot_external_example), where it is automatically installed and configured.  
- **Can be manually installed and set to start on boot** outside Buildroot.  
