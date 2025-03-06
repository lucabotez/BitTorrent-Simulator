Copyright @lucabotez

# BitTorrent Simulator

## Overview
This project simulates the **BitTorrent peer-to-peer file-sharing protocol** using **MPI (Message Passing Interface)**. The system consists of two types of tasks: **clients** and a **tracker**. Clients act as **seeders, peers, or leechers**, while the tracker manages **swarm information and file segment distribution**.

## Features
- **BitTorrent Tracker**: Manages file availability and tracks connected peers.
- **BitTorrent Client**: Handles file downloads and uploads in parallel.
- **Swarm-Based File Sharing**: Updates and retrieves peer lists dynamically.
- **Segmented File Downloading**: Ensures efficient data distribution.
- **MPI-Based Communication**: Uses message passing for synchronization.
- **Multi-Threaded Execution**: Supports concurrent file download and upload.

## Implementation Details
### **1. BitTorrent Tracker**
- **Initialization Phase:**
  - Receives information from clients about available files (file names, segment count, segment hashes).
  - Creates and maintains **swarm lists** for each shared file.
  - Waits for all clients to register before starting execution.

- **Execution Phase:**
  - Responds to:
    - `FILE_REQUEST` – Provides file details upon client request.
    - `FILE_UPDATE` – Updates swarm information for a given file.
    - `DONE` – Marks a client as finished.
  - Monitors client progress and **shuts down** when all requested files are fully downloaded.

### **2. BitTorrent Client**
Each client has two main execution phases: **initialization** and **file transfer**.

#### **2.1 Client Download Thread**
- Requests file information from the tracker.
- Downloads file **segments** in a circular manner across multiple peers to balance network load.
- Periodically updates swarm information from the tracker.
- Notifies the tracker when all requested files are downloaded.

#### **2.2 Client Upload Thread**
- Listens for incoming **file segment requests**.
- Sends `OK` if the requested segment is available, otherwise returns `NOT_OK`.
- Waits for a `SHUTDOWN` message to terminate.

## Notes
- The **tracker manages peer connections and file segment updates**.
- **Clients download and upload segments in parallel** using multi-threading.
- **Swarm lists are updated dynamically** to reflect available peers.
- The **SHUTDOWN message** ensures proper termination of the system.
