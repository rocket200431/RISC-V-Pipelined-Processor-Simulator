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
- Debug output for pipeline execution

## Pipeline Architecture

The processor is organized into the following stages:

```text
        ┌────┐    ┌────┐    ┌────┐    ┌─────┐    ┌────┐
        │ IF │ -> │ ID │ -> │ EX │ -> │ MEM │ -> │ WB │
        └────┘    └────┘    └────┘    └─────┘    └────┘
          │          │         │          │          │
        IF/ID      ID/EX     EX/MEM     MEM/WB     Registers
