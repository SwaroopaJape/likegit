# LikeGit

**LikeGit** is a lightweight, custom version control system built from scratch in C++. It was developed as an educational project to understand the core mechanics and internals of Git. 

Rather than relying on magic, LikeGit implements everything from the ground up: SHA-1 hashing, zlib compression, object storage (blobs, trees, commits), a JSON-based staging index, branching, the Myers diff algorithm, and three-way merging.

## CLI Usage

```bash
# Initialize a new repository
./likegit . init

# Configure user identity
./likegit . config user.name "Your Name"
./likegit . config user.email "you@example.com"

# Stage a file
./likegit . add my_file.txt

# Commit changes
./likegit . commit -m "Initial commit"

# View commit history
./likegit . log

# Create a new branch
./likegit . branch feature-x

# Switch branches
./likegit . checkout feature-x

# Create and switch in one command
./likegit . checkout -b feature-y

# Merge a branch into the current branch
./likegit . merge feature-x
```

## Features & Implementation Details

- **Object Database**: Compresses and stores files as blobs, groups them into trees, and tracks snapshots as commits using SHA-1 hashes.
- **JSON Index**: Uses a simple `index.json` file to track the staging area.
- **Time Travel**: Create branches and checkout specific commits (including Detached HEAD states) to jump between different points in history.
- **Myers Diff Engine**: Includes a production-grade implementation of the Myers $O(ND)$ Difference Algorithm with line-hashing and prefix/suffix stripping for high-performance file comparisons.
- **Three-Way Merging**: Uses a Parallel Breadth-First Search (BFS) to find the Lowest Common Ancestor (LCA) of two branches, applies a 3-way merge matrix to resolve changes, and automatically writes standard conflict markers (`<<<<<<<`, `=======`, `>>>>>>>`) when files diverge.

## How It Works (Under the Hood)

1. **`.likegit/objects/`**: When you run `add` or `commit`, files are compressed with zlib and stored here in directories named after the first 2 characters of their SHA-1 hash.
2. **`.likegit/HEAD`**: Tracks your current location in the commit graph. It either points to a branch reference (e.g., `ref: refs/heads/main`) or directly to a commit hash (Detached HEAD).
3. **Safety Checks**: `checkout` and `merge` will refuse to run if you have uncommitted changes in your working directory that would be overwritten.
4. **Merge Matrix**: When merging, LikeGit decompresses the trees for the current branch, the target branch, and their LCA. If both branches modified the same file differently, the system halts and writes conflict markers into the working directory for manual resolution.

## Build Instructions

### Prerequisites
- C++17 compatible compiler
- CMake (3.10+)
- zlib
- GoogleTest (fetched automatically by CMake)
- nlohmann/json (fetched automatically by CMake)

### Building
```bash
mkdir build
cd build
cmake ..
make
```

### Running Tests
A comprehensive suite of unit tests covers everything from hashing to merge conflicts.
```bash
./likegit_tests
```
