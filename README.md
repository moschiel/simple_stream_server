# Simple Stream Server

`simple_stream_server` is a **simple TCP server** written in C that accepts client connections, stores received data in a file, and returns the full content of this file to the client. 

It can run in normal mode or as a **daemon**, allowing it to execute in the background.

This application is used as an external package for Buildroot.

## 📂 Repository Structure

- **`simple_stream_server.c`**: Main server source code.
- **`Makefile`**: Script to compile the project.
- **`README.md`**: This documentation file.

## Usage in Buildroot

This package is integrated into [buildroot_external_example](https://github.com/moschiel/buildroot_external_example) and can be selected in `menuconfig`. Buildroot will automatically fetch, compile, and install it.

## 🚀 To Manually Compile and Executie

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

📌 **Summary:**  
- The server **stores all received messages** and **returns the full file content** at the end of each interaction.  
- Whenever a client connects, it **receives the complete stored history**.

