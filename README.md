# Mini-UnionFS (CC-Jackfruit)

A simplified Union File System implemented in userspace using FUSE (Filesystem in Userspace). This project simulates the layered filesystem approach used by container engines like Docker, where a read-write "container layer" is stacked on top of a read-only "image layer".

## Features

- **Layer Stacking**: Merges a read-only `lower_dir` and a read-write `upper_dir` into a single virtual mount point.
- **Copy-on-Write (CoW)**: Modifications to files existing only in the lower layer trigger an automatic copy to the upper layer before writing.
- **Whiteout Mechanism**: Deleting a file or directory from the lower layer creates a `.wh.` marker in the upper layer to hide it from the merged view.
- **POSIX Support**: Implements `getattr`, `readdir`, `read`, `write`, `create`, `unlink`, `truncate`, `mkdir`, and `rmdir`.

## Prerequisites

Ensure you are running a Linux environment (Ubuntu 22.04+ recommended) with FUSE 3 installed:

```bash
sudo apt update
sudo apt install -y libfuse3-dev pkg-config build-essential
```

## Project Structure

- `mini_unionfs.c`: The core C implementation of the FUSE operations.
- `Makefile`: Build script configured for fuse3.
- `test_unionfs.sh`: Automated test suite covering 14 validation scenarios.
- `design_doc.docx`: Detailed documentation of data structures and edge cases.

## Build and Run Instructions

1. **Compile the project:**
   ```bash
   make clean
   make
   ```

2. **Prepare the layers:**
   ```bash
   mkdir -p lower upper mnt
   echo "Hello from lower" > lower/base.txt
   ```

3. **Mount the filesystem:**
   ```bash
   ./mini_unionfs lower upper mnt -f
   ```
   *(The `-f` flag runs it in the foreground so you can see logs/errors. Use a separate terminal to interact with `mnt/`.)*

4. **Unmount:**
   ```bash
   fusermount -u mnt
   ```

## Automated Tests

A comprehensive test suite is included to verify Layer Visibility, Upper Precedence, Copy-on-Write, Whiteouts, and Directory Operations.

```bash
chmod +x test_unionfs.sh
./test_unionfs.sh
```

## Design Overview

The system uses a Global State to track absolute paths for the upper and lower directories. Every request is handled by a Path Resolution logic that prioritizes the upper layer and checks for whiteout markers before falling back to the lower layer.

---

Developed as part of the Cloud Computing Project at PES University.
