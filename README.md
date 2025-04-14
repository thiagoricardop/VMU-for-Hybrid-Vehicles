[![Cppcheck](https://github.com/thiagoricardop/VMU-for-Hybrid-Vehicles/actions/workflows/static_analysis.yml/badge.svg?branch=develop&event=check_run)](https://github.com/thiagoricardop/VMU-for-Hybrid-Vehicles/actions/workflows/static_analysis.yml)

# Hybrid Vehicle Simulation

This project simulates a hybrid vehicle with three main modules:

* **VMU (Vehicle Management Unit):** Controls the overall simulation, manages the transition between electric and combustion engines, and updates shared data.
* **EV (Electric Vehicle Module):** Simulates the electric motor, responding to commands from the VMU.
* **IEC (Internal Combustion Engine Module):** Simulates the internal combustion engine, responding to commands from the VMU.

## Prerequisites

Before running the simulation, ensure you have the following software installed on your system:

* **GCC (GNU Compiler Collection):** Used to compile the C source files.
* **Make:** A build automation tool used to simplify the compilation process (you've already created the `Makefile`).
* **cppcheck:** A static analysis tool for C/C++ code. You can install it using your distribution's package manager (e.g., `sudo apt-get install cppcheck` on Debian/Ubuntu, `sudo yum install cppcheck` on Fedora/CentOS).

## Compilation

This project includes a `Makefile` to automate the compilation process. Open your terminal in the directory containing the source files (`vmu.c`, `ev.c`, `iec.c`, `vmu.h`, and `Makefile`) and run the following command:

```bash
make
```

This command will read the instructions in the `Makefile` and compile the source files, creating three executable files: `vmu`, `ev`, and `iec`.

To clean up the compiled files (object files and executables), you can run:

```bash
make clean
```

## Running the Simulation

To run the simulation, you need to open three separate terminal windows.

1.  **Terminal 1 (VMU):** Navigate to the directory containing the executables and run the VMU module **first**:

    ```bash
    ./vmu
    ```

    The VMU is responsible for creating the shared memory and message queues that the other modules depend on. **It is crucial to start the VMU before the other modules.**

2.  **Terminal 2 (EV):** In a new terminal window, navigate to the same directory and run the Electric Vehicle module:

    ```bash
    ./ev
    ```

3.  **Terminal 3 (IEC):** In another new terminal window, navigate to the same directory and run the Internal Combustion Engine module:

    ```bash
    ./iec
    ```

You should now see output in each terminal window indicating the status of the simulation. The VMU will print the overall vehicle state, while the EV and IEC modules will indicate when they receive commands and update their internal states.

You can stop the simulation in each terminal by pressing `Ctrl + C`. The modules are also configured to shut down gracefully upon receiving `SIGINT` or `SIGTERM` signals. You can also send a `SIGUSR1` signal to any of the processes (using the `kill -USR1 <pid>` command, where `<pid>` is the process ID) to toggle a pause state.

## Static Analysis with Cppcheck

Cppcheck is a tool for static analysis of C/C++ code. It can help identify potential bugs and style issues in your code without actually running it.

To run Cppcheck on the source files, open your terminal in the directory containing the source files and use the following commands:

```bash
cppcheck vmu.c
cppcheck ev.c
cppcheck iec.c
```

You can also analyze all C files in the current directory and create a report.txt using:

```bash
cppcheck --enable=all *.c 2> report.txt
```

Cppcheck will then output any warnings or errors it finds in your code. Review these messages carefully to identify and fix potential issues.
