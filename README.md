# Hybrid Vehicle Simulation

This project simulates a hybrid vehicle with three main modules:

* **VMU (Vehicle Management Unit):** Controls the overall simulation, manages the transition between electric and combustion engines, and updates shared data.
* **EV (Electric Vehicle Module):** Simulates the electric motor, responding to commands from the VMU.
* **IEC (Internal Combustion Engine Module):** Simulates the internal combustion engine, responding to commands from the VMU.

## Introduction

This project includes source code, unit tests, and a Makefile for building, testing, and analyzing code coverage. The development environment and build process are managed using Docker to ensure consistency.

## Prerequisites

Before you begin, ensure you have the following installed on your system:

* **Docker:** For building and running the containerized development environment.
    * [Install Docker](https://docs.docker.com/get-docker/)
* **Make:** To use the provided Makefile.
    * **Linux (Debian/Ubuntu):** `sudo apt-get update && sudo apt-get install make`
    * **Linux (Fedora):** `sudo dnf install make`
    * **macOS (using Homebrew):** `brew install make` (Make is often pre-installed on macOS, but Homebrew can provide a newer version)
* **Git:** To clone the repository.
    * [Install Git](https://git-scm.com/downloads)
* **tmux:** A terminal multiplexer used by the `run` target for managing multiple application processes.
    * **Linux (Debian/Ubuntu):** `sudo apt-get update && sudo apt-get install tmux`
    * **Linux (Fedora):** `sudo dnf install tmux`
    * **macOS (using Homebrew):** `brew install tmux`
* **xdg-utils:** Required for the `show` target to automatically open the coverage report (typically available on Linux desktops).
    * **Linux (Debian/Ubuntu):** `sudo apt-get update && sudo apt-get install xdg-utils`
    * **Linux (Fedora):** `sudo dnf install xdg-utils`
    * (xdg-utils is primarily a Linux utility; equivalent functionality on macOS or Windows would be different)

## Getting Started

### 1. Clone the repository
    ```bash
    git clone <repository_url>
    cd <repository_directory>
    ```

To set up the development environment and build the project, you will primarily interact with the provided `Makefile`.

### 2. Build the Docker Environment

The first step is to build the Docker image that contains all the necessary tools and dependencies (GCC, LCOV, etc.).

```bash
make docker
```

This command builds the Docker image tagged as `vmu-dev`.

### 3. Using Makefile Commands (Inside Docker)

Most of the Makefile targets (`all`, default build, `test`, `coverage`, `clean`) are designed to be executed *inside* the Docker container using the `vmu-dev` image.

You will use the `docker run` command to execute these targets. The pattern is generally:

```bash
docker run --rm -v $(pwd):/app vmu-dev make <target>
```

* `docker run`: Runs a command in a new container.
* `--rm`: Automatically removes the container when it exits.
* `-v $(pwd):/app`: Mounts your current project directory on the host (`$(pwd)`) to the `/app` directory inside the container. This allows the container to access your source code and the Makefile, and for generated files (like executables and reports) to appear in your local directory.
* `vmu-dev`: The name of the Docker image to use.
* `make <target>`: The Make command to execute inside the container.

Here are the common commands to run inside the Docker environment:

* **Build all executables:**
    ```bash
    docker run --rm -v $(pwd):/app vmu-dev make all
    ```
    or simply:
    ```bash
    docker run --rm -v $(pwd):/app vmu-dev make
    ```
    (as `all` is the default target). This compiles the main executables (`vmu`, `ev`, `iec`) and the test executables (`test_vmu`, `test_ev`, `test_iec`) into the `bin` directory.

* **Run unit tests:**
    ```bash
    docker run --rm -v $(pwd):/app vmu-dev make test
    ```
    This compiles the test executables (if not already built) and then runs them inside the Docker container.

* **Generate code coverage report:**
    ```bash
    docker run --rm -v $(pwd):/app vmu-dev make coverage
    ```
    This runs the unit tests (if not already executed) and then uses LCOV/genhtml within the container to generate the code coverage report in the `coverage` directory in your project's root.

* **Clean generated files:**
    ```bash
    docker run --rm -v $(pwd):/app vmu-dev make clean
    ```
    This removes the `bin` directory, the `coverage` directory, and the `coverage.info` file from your project.

### 4. Running the Application (Outside Docker)

The `make run` command is intended to execute the main application components (`vmu`, `ev`, `iec`) in separate `tmux` panes on your local system. 

```bash
make run
```

You should now see output in each terminal window indicating the status of the simulation. The VMU will print the overall vehicle state, while the EV and IEC modules will indicate when they receive commands and update their internal states.

You can stop the simulation by pressing Ctrl + C in the VMU terminal, and this command will shut down the modules iec and ev automatically. The modules are also configured to shut down gracefully upon receiving SIGINT or SIGTERM signals.

### 5. Viewing Coverage Report (Outside Docker)

After running `make coverage` (inside Docker), the report is generated in the `coverage` directory in your local project folder. You can attempt to open this report using the `make show` command:

```bash
make show
```

This command tries to open the `coverage/index.html` file using `xdg-open`. Note that `xdg-open` might not work in all environments (e.g., headless servers). If it fails, it will print a message. You can always open the `coverage/index.html` file directly in your web browser.

### 6. Stopping the Application (Outside Docker)

If you started the application using `make run`, you can stop the `tmux` session and the running components with the `make kill` command:

```bash
make kill
```
This command attempts to kill the `tmux` session named `meu_sistema`.

### 7. Example Workflow

1.  Build the Docker image:
    ```bash
    make docker
    ```

2.  Build the project inside Docker:
    ```bash
    docker run --rm -it -v $(pwd):/app vmu-dev make all
    ```

3.  Run tests and generate coverage inside Docker:
    ```bash
    docker run --rm -it -v $(pwd):/app vmu-dev make coverage
    ```

4.  View the coverage report on your host:
    ```bash
    make show
    ```

5.  Run the application in tmux (on your host):
    ```bash
    make run
    ```

6.  Clean up build files and reports inside Docker:
    ```bash
    docker run --rm -it -v $(pwd):/app vmu-dev make clean
    ```
