## **Course Project: Network File System (NFS)**  
This project entails the development of a simple distributed file system from scratch, allowing students to explore the intricacies of network communication and file management. Participants will implement key components such as clients, a naming server, and storage servers, enabling file operations like reading, writing, and streaming. The project aims to enhance understanding of distributed systems while emphasizing collaborative coding practices through GitHub. Students will learn to manage concurrency and optimize performance, providing a practical foundation in network file systems.

## Constraints
- Work in teams of 4
- Use only C language

## Specs

1. **Naming and Storage Servers**
   - **Initialization [60 Marks]**: Initialize the Naming Server (NM) and Storage Servers (SS), ensuring dynamic IP address registration for clients and servers.
   - **On Storage Servers (SS) [120 Marks]**: Implement functionalities for storage servers to handle commands from the Naming Server, including creating, deleting, and copying files/directories.
   - **On Naming Server (NM) [30 Marks]**: Store information from storage servers and provide timely feedback to clients upon task completion.

2. **Clients [80 Marks]**
   - **Path Finding**: Clients communicate with the NM to locate files across SS.
   - **Functionalities**:
     - Reading, writing, retrieving information about files, and streaming audio.
     - Creating and deleting files/folders.
     - Copying files/directories between storage servers.
     - Listing all accessible paths.

3. **Other Features**
   - **Asynchronous and Synchronous Writing [50 Marks]**: Support both asynchronous writes for large data inputs and synchronous priority writes when necessary.
   - **Multiple Clients [70 Marks]**: Ensure concurrent client access to the NM, with initial acknowledgments for received requests.
  
Other contributors: Divijh K Mangtani, Mehul Agrawal, Parth Jindal
