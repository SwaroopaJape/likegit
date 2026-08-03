# LikeGit

**LikeGit** is a version control system written in C++17, built from the ground up as a deep-dive into how Git actually works under the hood. Every layer — from SHA-1 hashing and object storage to diff and three-way merging — is implemented from scratch.

## What It Does

LikeGit replicates the fundamental workflow of Git:

- Initialize a repository (`init`)
- Stage files for a commit (`add`)
- Inspect staged vs. unstaged changes (`status`, `diff`)
- Snapshot the staging area as a commit (`commit`)
- Browse the commit history (`log`)
- Create and switch branches (`branch`, `checkout`)
- Merge branches, resolving or flagging conflicts (`merge`)
- Configure a user identity (`config`)
- Respect ignore patterns via `.likegitignore`

## Repository Structure

```
likegit/
├── include/
│   └── likegit/
│       ├── core.hpp      # Declarations for all VCS operations
│       ├── diff.hpp      # Myers diff data structures and interface
│       └── sha1.hpp      # Manual SHA-1 hash function declaration
├── src/
│   ├── main.cpp          # CLI entry point — parses commands and dispatches
│   ├── core.cpp          # All core VCS logic (objects, index, branches, merge)
│   ├── diff.cpp          # Myers O(ND) diff algorithm implementation
│   └── sha1.cpp          # SHA-1 hashing implemented from scratch
├── tests/
│   └── test_core.cpp     # GoogleTest unit tests for all major subsystems
└── CMakeLists.txt        # Build system — fetches GoogleTest and nlohmann/json
```

## What Was Implemented From Scratch

The following components were written entirely from scratch **without** using any cryptography or VCS libraries:

| Component | Location | Details |
|---|---|---|
| **SHA-1 Hashing** | `src/sha1.cpp` | Full implementation of the SHA-1 algorithm: message padding, schedule expansion, and the five-round compression loop — no `OpenSSL` or system hash APIs used. |
| **Myers Diff Algorithm** | `src/diff.cpp` | The O(ND) shortest edit script algorithm with line-hashing and prefix/suffix stripping for performance. Produces an `Edit` list of `EQUAL`, `INSERT`, and `DELETE` operations. |
| **Three-Way Merge** | `src/core.cpp` | Parallel BFS over the commit graph to locate the Lowest Common Ancestor (LCA) of two branches, followed by a 3-way merge matrix. Writes standard conflict markers (`<<<<<<<`, `=======`, `>>>>>>>`) on divergence. |
| **Object Storage** | `src/core.cpp` | Content-addressed object store mimicking Git's layout: blobs, trees, and commits are zlib-compressed and stored under `.likegit/objects/<2-char-prefix>/<remaining-hash>`. |
| **Staging Index** | `src/core.cpp` | A `index.json` file (managed via `nlohmann/json`) that tracks filename-to-hash mappings for the staging area. |
| **Tree Generation** | `src/core.cpp` | Builds a deterministic tree object from the current index to snapshot directory structure at commit time. |
| **Branch & HEAD Management** | `src/core.cpp` | Symbolic refs (`ref: refs/heads/<name>`) and detached HEAD support, along with uncommitted-change safety checks before checkout/merge. |

### External Libraries Used

| Library | Purpose | How it's obtained |
|---|---|---|
| `zlib` | Compressing and decompressing object data | System package (`find_package`) |
| `nlohmann/json` | Reading and writing `index.json` and `config.json` | Auto-fetched by CMake via `FetchContent` |
| `GoogleTest` | Unit testing framework | Auto-fetched by CMake via `FetchContent` |

## Building

### Prerequisites

- A C++17-compatible compiler (GCC 8+, Clang 7+, or MSVC 2017+)
- CMake 3.14+
- `zlib` development headers (e.g. `sudo apt install zlib1g-dev` on Ubuntu)

GoogleTest and nlohmann/json are fetched automatically at configure time — no manual installation required.

### Build Steps

```bash
mkdir build
cd build
cmake ..
make
```

The build produces two binaries inside `build/`:
- `likegit` — the main CLI tool
- `likegit_tests` — the test suite runner

## Running the CLI

All commands follow the pattern:

```bash
./likegit <path/to/repo> <command> [args...]
```

```bash
# Initialize a new repository
./likegit /path/to/repo init

# Configure user identity
./likegit /path/to/repo config user.name "Your Name"
./likegit /path/to/repo config user.email "you@example.com"

# Stage a specific file
./likegit /path/to/repo add my_file.txt

# Stage all files (respects .likegitignore)
./likegit /path/to/repo add .

# View repository status (staged vs. unstaged vs. untracked)
./likegit /path/to/repo status

# View unstaged changes as a diff
./likegit /path/to/repo diff

# Commit the staged snapshot
./likegit /path/to/repo commit -m "Initial commit"

# View commit history
./likegit /path/to/repo log

# Create a new branch
./likegit /path/to/repo branch feature-x

# Switch to a branch (or a commit hash for detached HEAD)
./likegit /path/to/repo checkout feature-x

# Create and switch in one step
./likegit /path/to/repo checkout -b feature-y

# Merge a branch into the current branch
./likegit /path/to/repo merge feature-x
```

## Running Tests

```bash
cd build
./likegit_tests
```

The test suite covers SHA-1 hashing, object storage, tree generation, diff correctness, branch operations, and merge conflict detection.
