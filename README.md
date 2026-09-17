# RISC-V Pipelined Processor Simulator

A C++ implementation of a five-stage pipelined RISC-V processor simulator with support for instruction execution, data hazards, forwarding, branch handling, memory operations, and pipeline control.

## Features

- Five-stage instruction pipeline:
  - Instruction Fetch (IF)
  - Instruction Decode (ID)
  - Execute (EX)
  - Memory Access (MEM)
  - Write Back (WB)
- RISC-V instruction decoding
- Register file with 32 registers
- Arithmetic and logical ALU operations
- M-extension operations:
  - MUL
  - DIV
  - REM
- Immediate instruction support
- Load and store instructions
- Conditional branches
- JAL and JALR instructions
- LUI and AUIPC support
- Load-use hazard detection
- Pipeline stalls and bubbles
- Data forwarding from EX/MEM and MEM/WB
- Branch and jump pipeline flushing
- Separate instruction and data memory
- Binary instruction-file loading
- Register and memory state output
- Optional debug output for pipeline execution

## Pipeline Architecture

The processor is organized into the following five stages:

        ┌────┐    ┌────┐    ┌────┐    ┌─────┐    ┌────┐
        │ IF │ -> │ ID │ -> │ EX │ -> │ MEM │ -> │ WB │
        └────┘    └────┘    └────┘    └─────┘    └────┘
          │          │         │          │          │
        IF/ID      ID/EX     EX/MEM     MEM/WB     Registers

Pipeline registers are used to transfer information between consecutive stages.

## Hazard Handling

The simulator implements mechanisms for handling data and control hazards.

### Data Forwarding

Results can be forwarded from:

- EX/MEM
- MEM/WB

This reduces unnecessary pipeline stalls caused by data dependencies between instructions.

### Load-Use Hazard

When an instruction immediately following a load depends on the loaded register, the processor:

1. Stalls the PC.
2. Holds the IF/ID pipeline register.
3. Inserts a NOP bubble into the ID/EX pipeline register.

## Control Hazards

Branches are resolved in the EX stage.

The simulator uses a not-taken prediction approach. When a branch or jump is taken, the required pipeline stages are flushed and execution continues from the target address.

## Instruction Input

The simulator loads instructions from a text file containing 32-bit binary RISC-V instructions.

Each instruction must contain exactly 32 binary characters.

Example:

    00000000000100000000001010010011
    00000000001000001000001100110011
    00000000000000000000000001110011

The instruction file is not included in this repository. The user can provide their own instruction file and pass its filename as a command-line argument.

## Compilation

Compile using:

    g++ -std=c++17 -O2 pipelined_processor.cpp -o riscv_sim

## Running the Simulator

The simulator accepts the instruction filename as a command-line argument.

For example:

    ./riscv_sim output.txt

On Windows:

    .\riscv_sim.exe output.txt

If no filename is provided, the simulator looks for a file named `output.txt` in the current directory.

## Output

After execution, the simulator displays the final state of the processor.

The output includes:

- Total number of simulation cycles
- Final values of all 32 registers
- Data memory contents

An example of the register output is:

    --- Final Register State ---
    x 0: 0x00000000
    x 1: 0x00000000
    x 2: 0x00008000
    ...
    x31: 0x00000000

The data memory contents are also displayed for the initialized memory region.

Example:

    --- Data Memory [0x1000 - 0x100c] ---
      0x00001000: 0x00000001
      0x00001004: 0x00000002
      0x00001008: 0x00000003
      0x0000100c: 0x00000004

## Debug Mode

Detailed pipeline execution can be enabled by changing:

    cpu.runPipelinedCycle(false);

to:

    cpu.runPipelinedCycle(true);

Debug mode displays information from the different pipeline stages, including instruction fetch, register decoding, forwarding, memory operations, write-back operations, hazards, stalls, and branch decisions.

## Project Structure

    RISC-V-Pipelined-Processor-Simulator/
    │
    ├── README.md
    └── pipelined_processor.cpp

## Technologies

- C++
- RISC-V ISA
- Computer Architecture
- Processor Pipelining
- Data Hazard Detection
- Data Forwarding
- Control Hazard Handling

## Author

Mallika Bramaramba
