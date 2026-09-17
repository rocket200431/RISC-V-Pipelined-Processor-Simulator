#include <iostream>
#include <vector>
#include <cstdint>
#include <cassert>
#include <iomanip>
#include <fstream>
#include <string>

// Forward Declaration
class PipelinedProcessor;

// Helper function declaration
void loadInstructionsFromFile(PipelinedProcessor& cpu, const std::string& filename);

// ====================================================================
// Enums and Structs (Pipeline components)
// ====================================================================

enum ALUOp {
    ALU_ADD, ALU_SUB, ALU_AND, ALU_OR, ALU_XOR,
    ALU_SLL, ALU_SRL, ALU_SRA,
    ALU_SLT, ALU_SLTU,
    ALU_MUL, ALU_DIV, ALU_REM,
    ALU_NOP
};

struct ControlSignals {
    bool RegWrite = false;
    bool MemRead  = false;
    bool MemWrite = false;
    bool Branch   = false;
    bool MemToReg = false;
    ALUOp ALUControl = ALU_NOP;
    bool ALUSrc   = false;
    bool Jump     = false;
    bool UType    = false; // For LUI/AUIPC (not implemented here, but useful)
};

// ====================================================================
// Pipeline Registers (Latches)
// ====================================================================

// IF/ID Latch: Data transferred from IF to ID stage
struct IF_ID_Latch {
    uint32_t PC = 0;
    uint32_t PCplus4 = 0;
    uint32_t Instruction = 0x00000013; // Default to NOP (addi x0, x0, 0)
    bool is_valid = true; // Tracks if a NOP bubble was inserted
};

// ID/EX Latch: Data transferred from ID to EX stage
struct ID_EX_Latch {
    uint32_t PCplus4 = 0;
    ControlSignals ctrl;
    uint32_t ReadData1 = 0; // Value of Rs1
    uint32_t ReadData2 = 0; // Value of Rs2 (used for R-Type, Store)
    int32_t Immediate = 0;  // Immediate value
    uint8_t Rs1 = 0;        // Rs1 register index (for forwarding/hazard)
    uint8_t Rs2 = 0;        // Rs2 register index (for forwarding/hazard)
    uint8_t Rd = 0;         // Rd register index (for forwarding/hazard)
    uint8_t Funct3 = 0;     // Funct3 (needed for branches, loads, stores, shifters)
    uint8_t Opcode = 0;     // Opcode (needed for identifying instruction type)
    bool is_valid = true;
};

// EX/MEM Latch: Data transferred from EX to MEM stage
struct EX_MEM_Latch {
    uint32_t PCplus4 = 0;
    ControlSignals ctrl; // Includes MemRead, MemWrite, RegWrite, MemToReg
    uint32_t ALUResult = 0;
    uint32_t WriteData = 0; // Data to be written to memory (from RegVal2)
    uint8_t Rd = 0;
    bool is_valid = true;
    bool is_taken = false; // Branch prediction outcome (EX stage resolution)
    int32_t BranchTarget = 0; // The calculated branch target address offset
};

// MEM/WB Latch: Data transferred from MEM to WB stage
struct MEM_WB_Latch {
    ControlSignals ctrl; // Includes RegWrite, MemToReg
    uint32_t ReadData = 0;  // Data read from memory
    uint32_t ALUResult = 0; // ALU result (for R-Type/I-Type)
    uint8_t Rd = 0;
    bool is_valid = true;
};

// ====================================================================
// Components
// ====================================================================

class RegisterFile {
private:
    std::vector<uint32_t> registers;
public:
    RegisterFile() : registers(32, 0) {}
    uint32_t read(uint8_t reg) const {
        if (reg == 0) return 0;
        assert(reg < 32);
        return registers[reg];
    }
    void write(uint8_t reg, uint32_t val) {
        if (reg == 0) return;
        assert(reg < 32);
        registers[reg] = val;
    }
};

class ALU {
public:
    static uint32_t operate(ALUOp op, uint32_t input1, uint32_t input2) {
        // Implementation remains the same as single-cycle
        switch(op) {
            case ALU_ADD:  return input1 + input2;
            case ALU_SUB:  return input1 - input2;
            case ALU_AND:  return input1 & input2;
            case ALU_OR:   return input1 | input2;
            case ALU_XOR:  return input1 ^ input2;
            case ALU_SLL:  return input1 << (input2 & 0x1F);
            case ALU_SRL:  return input1 >> (input2 & 0x1F);
            case ALU_SRA:  return static_cast<uint32_t>(static_cast<int32_t>(input1) >> (input2 & 0x1F));
            case ALU_SLT:  return (static_cast<int32_t>(input1) < static_cast<int32_t>(input2)) ? 1u : 0u;
            case ALU_SLTU: return (input1 < input2) ? 1u : 0u;
            case ALU_MUL:  return input1 * input2;
            case ALU_DIV:
                if (input2 == 0) return static_cast<uint32_t>(-1);
                return static_cast<uint32_t>(static_cast<int32_t>(input1) / static_cast<int32_t>(input2));
            case ALU_REM:
                if (input2 == 0) return input1;
                return static_cast<uint32_t>(static_cast<int32_t>(input1) % static_cast<int32_t>(input2));
            case ALU_NOP:
            default: return 0;
        }
    }
};

class ImmediateGenerator {
public:
    static int32_t generateIType(uint32_t inst) {
        // I-Type: [31:20]
        int32_t imm = (inst >> 20) & 0xFFF;
        if (imm & 0x800) imm |= 0xFFFFF000; // Sign-extend
        return imm;
    }
    static int32_t generateSType(uint32_t inst) {
        // S-Type: [31:25][11:7]
        int32_t imm = (((inst >> 25) & 0x7F) << 5) | ((inst >> 7) & 0x1F);
        if (imm & 0x800) imm |= 0xFFFFF000; // Sign-extend
        return imm;
    }
    static int32_t generateBType(uint32_t inst) {
        // B-Type: [31][7][30:25][11:8] -> [12][11][10:5][4:1]
        int32_t imm = (((inst >> 31) & 0x1) << 12) |
                      (((inst >> 25) & 0x3F) << 5) |
                      (((inst >> 8) & 0xF) << 1) |
                      (((inst >> 7) & 0x1) << 11);
        if (imm & 0x1000) imm |= 0xFFFFE000; // Sign-extend
        return imm;
    }
    static int32_t generateJType(uint32_t inst) {
        // J-Type: [31][19:12][20][30:21] -> [20][10:1][11][19:12]
        int32_t imm = (((inst >> 31) & 0x1) << 20) |
                      (((inst >> 21) & 0x3FF) << 1) |
                      (((inst >> 20) & 0x1) << 11) |
                      (((inst >> 12) & 0xFF) << 12);
        if (imm & (1 << 20)) imm |= ~((1 << 21) - 1); // Sign-extend
        return imm;
    }
};

class ControlUnit {
public:
    static ControlSignals generate(uint32_t inst) {
        ControlSignals ctrl;
        uint8_t opcode = inst & 0x7F;
        uint8_t funct3 = (inst >> 12) & 0x7;
        uint8_t funct7 = (inst >> 25) & 0x7F;

        // Note: LUI/AUIPC are not implemented in ALU/ImmediateGen here, but we set a flag
        // R-Type
        if (opcode == 0x33) {
            ctrl.RegWrite = true;
            // M-Extension (MUL, DIV, REM)
            if (funct7 == 0x01) {
                if (funct3 == 0x0) ctrl.ALUControl = ALU_MUL;
                else if (funct3 == 0x4) ctrl.ALUControl = ALU_DIV;
                else if (funct3 == 0x6) ctrl.ALUControl = ALU_REM;
                else ctrl.ALUControl = ALU_NOP;
            } else {
                // RV32I R-Types
                if (funct3 == 0x0 && funct7 == 0x00) ctrl.ALUControl = ALU_ADD;
                else if (funct3 == 0x0 && funct7 == 0x20) ctrl.ALUControl = ALU_SUB;
                else if (funct3 == 0x7) ctrl.ALUControl = ALU_AND;
                else if (funct3 == 0x6) ctrl.ALUControl = ALU_OR;
                else if (funct3 == 0x4) ctrl.ALUControl = ALU_XOR;
                else if (funct3 == 0x1) ctrl.ALUControl = ALU_SLL;
                else if (funct3 == 0x5 && funct7 == 0x00) ctrl.ALUControl = ALU_SRL;
                else if (funct3 == 0x5 && funct7 == 0x20) ctrl.ALUControl = ALU_SRA;
                else if (funct3 == 0x2) ctrl.ALUControl = ALU_SLT;
                else if (funct3 == 0x3) ctrl.ALUControl = ALU_SLTU;
                else ctrl.ALUControl = ALU_NOP;
            }
        }
        // I-Type (ALU immediate)
        else if (opcode == 0x13) {
            ctrl.RegWrite = true; ctrl.ALUSrc = true;
            if (funct3 == 0x0) ctrl.ALUControl = ALU_ADD;
            else if (funct3 == 0x7) ctrl.ALUControl = ALU_AND;
            else if (funct3 == 0x6) ctrl.ALUControl = ALU_OR;
            else if (funct3 == 0x4) ctrl.ALUControl = ALU_XOR;
            else if (funct3 == 0x1) ctrl.ALUControl = ALU_SLL;
            else if (funct3 == 0x5) { // SRLI/SRAI
                if ((inst >> 30) & 1) ctrl.ALUControl = ALU_SRA;
                else ctrl.ALUControl = ALU_SRL;
            }
            else if (funct3 == 0x2) ctrl.ALUControl = ALU_SLT;
            else if (funct3 == 0x3) ctrl.ALUControl = ALU_SLTU;
            else ctrl.ALUControl = ALU_NOP;
        }
        // I-Type (Loads)
        else if (opcode == 0x03) {
            ctrl.RegWrite = true; ctrl.ALUSrc = true; ctrl.MemRead = true;
            ctrl.MemToReg = true; ctrl.ALUControl = ALU_ADD;
        }
        // S-Type (Stores)
        else if (opcode == 0x23) {
            ctrl.ALUSrc = true; ctrl.MemWrite = true;
            ctrl.ALUControl = ALU_ADD;
        }
        // B-Type (Branches)
        else if (opcode == 0x63) {
            ctrl.Branch = true;
            ctrl.ALUControl = ALU_SUB; // ALU used for branch condition check (val1 - val2)
        }
        // J-Type (JAL)
        else if (opcode == 0x6F) {
            ctrl.RegWrite = true; ctrl.Jump = true;
        }
        // I-Type (JALR)
        else if (opcode == 0x67) {
            ctrl.RegWrite = true; ctrl.Jump = true; ctrl.ALUSrc = true;
            ctrl.ALUControl = ALU_ADD; // ALU calculates target address (Rs1 + Imm)
        }
        // U-Type (LUI/AUIPC) - Simplified logic since immediate gen only handles I/S/B/J
        else if (opcode == 0x37 || opcode == 0x17) {
            ctrl.RegWrite = true;
            ctrl.UType = true;
        }

        return ctrl;
    }
};

class Memory {
private:
    std::vector<uint8_t> mem;
public:
    Memory(size_t size) : mem(size, 0) {}
    uint32_t readWord(uint32_t addr) const {
        if (addr + 3 >= mem.size()) {
            // Memory read for instruction or data access out of bounds
            return 0;
        }
        // Little-endian load (RISC-V is typically little-endian)
        return (uint32_t)mem[addr] | ((uint32_t)mem[addr+1] << 8) |
               ((uint32_t)mem[addr+2] << 16) | ((uint32_t)mem[addr+3] << 24);
    }
    void writeWord(uint32_t addr, uint32_t data) {
        if (addr + 3 >= mem.size()) {
            // Memory write for data access out of bounds
            return;
        }
        // Little-endian store
        mem[addr]  = data & 0xFF;
        mem[addr+1] = (data >> 8) & 0xFF;
        mem[addr+2] = (data >> 16) & 0xFF;
        mem[addr+3] = (data >> 24) & 0xFF;
    }
    size_t size() const { return mem.size(); }
};

// ====================================================================
// Pipelined Processor
// ====================================================================

class PipelinedProcessor {
private:
    RegisterFile regs;
    Memory instrMem;
    Memory dataMem;
    uint32_t PC;
    uint32_t nextPC;

    // Pipeline Registers
    IF_ID_Latch IF_ID_new, IF_ID_old;
    ID_EX_Latch ID_EX_new, ID_EX_old;
    EX_MEM_Latch EX_MEM_new, EX_MEM_old;
    MEM_WB_Latch MEM_WB_new, MEM_WB_old;

    // Control/Hazard Flags
    bool PC_Write = true;
    bool IF_ID_Write = true;
    bool ID_EX_Flush = false;
    bool IF_ID_Flush = false;

public:
    PipelinedProcessor(size_t instrMemSize, size_t dataMemSize) :
        instrMem(instrMemSize), dataMem(dataMemSize), PC(0), nextPC(0) {}

    // Public utilities for setup
    void loadInstruction(uint32_t addr, uint32_t inst) { instrMem.writeWord(addr, inst); }
    void loadData(uint32_t addr, uint32_t val) { dataMem.writeWord(addr, val); }
    void setRegister(uint8_t reg, uint32_t val) { regs.write(reg, val); }
    uint32_t getPC() const { return PC; }
    size_t getInstrMemSize() const { return instrMem.size(); }
    uint32_t readInstrWordForDebug(uint32_t addr) const { return instrMem.readWord(addr); }
    uint32_t readDataWordForDebug(uint32_t addr) const { return dataMem.readWord(addr); }

    void printRegisters() const {
        std::cout << "\n--- Final Register State ---\n";
        for (int i = 0; i < 32; ++i) {
            std::cout << "x" << std::setw(2) << i << ": 0x"
                      << std::hex << std::setfill('0') << std::setw(8)
                      << regs.read(i) << std::dec << "\n";
        }
    }
    
    void printDataMemory(uint32_t startAddr, uint32_t numWords) const {
        std::cout << "\n--- Data Memory [0x" << std::hex << startAddr << " - 0x" << (startAddr + 4*numWords - 4) << "] ---\n" << std::dec;
        for(uint32_t i=0; i<numWords; ++i) {
            uint32_t addr = startAddr + 4*i;
            uint32_t val = dataMem.readWord(addr);
            std::cout << "  0x" << std::hex << std::setfill('0') << std::setw(8) << addr
                      << ": 0x" << std::setw(8) << val << std::dec << "\n";
        }
        std::cout << std::setfill(' '); // Reset fill
    }

    void runPipelinedCycle(bool debug=false) {
        // Run stages in reverse order to ensure new state doesn't affect earlier stages in the same cycle
        // WB -> MEM -> EX -> ID -> IF
        
        stage_WB(debug);
        stage_MEM(debug);
        stage_EX(debug);
        
        // Hazard Detection and ID
        hazardDetectionUnit(debug); // <<< FIXED: Pass debug flag here
        stage_ID(debug);
        
        // IF
        stage_IF(debug);

        // Update Latches for the next cycle
        updatePipelineRegisters();
    }

private:
    
    // ====================================================================
    // Hazard Detection Unit (ID Stage Logic)
    // Checks for Load-Use Hazard and controls stalls/flushes
    // ====================================================================
    void hazardDetectionUnit(bool debug) { // <<< FIXED: Accept debug flag
        uint32_t inst_id = IF_ID_old.Instruction;
        uint8_t rs1_id = (inst_id >> 15) & 0x1F;
        uint8_t rs2_id = (inst_id >> 20) & 0x1F;
        
        // Check for Load-Use Hazard:
        // IF/ID.Rs1/Rs2 is same as EX/MEM.Rd AND EX/MEM is a Load instruction (MemRead)
        bool load_use_hazard = (ID_EX_old.ctrl.MemRead) && (ID_EX_old.Rd != 0) &&
                               (ID_EX_old.Rd == rs1_id || ID_EX_old.Rd == rs2_id);

        if (load_use_hazard) {
            // STALL:
            // 1. Prevent PC and IF/ID from updating (PC_Write = 0, IF_ID_Write = 0)
            PC_Write = false;
            IF_ID_Write = false;
            // 2. Insert NOP into ID/EX register (ID_EX_new = NOP)
            ID_EX_new.is_valid = false;
            ID_EX_new.ctrl.RegWrite = false; // All control signals to 0/NOP
            
            if (debug) std::cout << "    [Hazard] STALL: Load-Use Hazard detected on x" << (int)ID_EX_old.Rd << "\n";
        } else {
            PC_Write = true;
            IF_ID_Write = true;
            ID_EX_new.is_valid = true;
        }

        // Handle Branch/Jump Flush (if EX stage signals a taken branch/misprediction)
        if (EX_MEM_old.is_taken) {
            IF_ID_Flush = true; // Flush IF/ID
            ID_EX_Flush = true; // Flush ID/EX
            PC_Write = true; // Ensure PC is written with the new target
            IF_ID_Write = true; // IF/ID will be written with NOP after flush
        } else {
            IF_ID_Flush = false;
            ID_EX_Flush = false;
        }
    }

    // ====================================================================
    // Stage Implementations
    // ====================================================================

    void stage_WB(bool debug) {
        // Only write back if the instruction in this stage is valid and wants to write
        if (!MEM_WB_old.is_valid || !MEM_WB_old.ctrl.RegWrite) return;

        uint32_t writeData = 0;
        
        if (MEM_WB_old.ctrl.MemToReg) {
            // LW instruction result
            writeData = MEM_WB_old.ReadData;
        } else {
            // R-Type/I-Type/JAL instruction result (from ALU or PC+4)
            writeData = MEM_WB_old.ALUResult;
        }

        regs.write(MEM_WB_old.Rd, writeData);
        if (debug) std::cout << "  WB: Write x" << (int)MEM_WB_old.Rd << " <= 0x" << std::hex << writeData << std::dec << "\n";
    }

    void stage_MEM(bool debug) {
        EX_MEM_new = EX_MEM_old;
        if (!EX_MEM_old.is_valid) { MEM_WB_new.is_valid = false; return; }

        MEM_WB_new.is_valid = true;
        MEM_WB_new.ctrl = EX_MEM_old.ctrl;
        MEM_WB_new.ALUResult = EX_MEM_old.ALUResult;
        MEM_WB_new.Rd = EX_MEM_old.Rd;

        // Data Memory Read (Load)
        if (EX_MEM_old.ctrl.MemRead) {
            uint32_t addr = EX_MEM_old.ALUResult;
            MEM_WB_new.ReadData = dataMem.readWord(addr);
            if (debug) std::cout << "  MEM: Read 0x" << std::hex << MEM_WB_new.ReadData << " from [0x" << addr << "]\n";
        }

        // Data Memory Write (Store)
        if (EX_MEM_old.ctrl.MemWrite) {
            uint32_t addr = EX_MEM_old.ALUResult;
            dataMem.writeWord(addr, EX_MEM_old.WriteData);
            if (debug) std::cout << "  MEM: Write 0x" << std::hex << EX_MEM_old.WriteData << " to [0x" << addr << "]\n";
        }
    }

    void stage_EX(bool debug) {
        ID_EX_new = ID_EX_old;
        if (!ID_EX_old.is_valid) { EX_MEM_new.is_valid = false; return; }

        EX_MEM_new.is_valid = true;
        EX_MEM_new.ctrl = ID_EX_old.ctrl;
        EX_MEM_new.Rd = ID_EX_old.Rd;
        EX_MEM_new.PCplus4 = ID_EX_old.PCplus4;
        EX_MEM_new.WriteData = ID_EX_old.ReadData2; // Data for store instructions

        uint32_t aluInput1 = ID_EX_old.ReadData1;
        uint32_t aluInput2 = ID_EX_old.ctrl.ALUSrc ? ID_EX_old.Immediate : ID_EX_old.ReadData2;
        
        // --- Forwarding Unit (Data Hazard Resolution) ---

        // Forwarding Logic: Determines where aluInput1/2 should come from
        uint8_t forwardA = 0; // 0: No forward, 1: EX/MEM, 2: MEM/WB
        uint8_t forwardB = 0;

        // Check Forward A (Rs1)
        if (EX_MEM_old.ctrl.RegWrite && EX_MEM_old.Rd != 0 && EX_MEM_old.Rd == ID_EX_old.Rs1) {
            forwardA = 1; // Forward from EX/MEM (ALU result)
        } else if (MEM_WB_old.ctrl.RegWrite && MEM_WB_old.Rd != 0 && MEM_WB_old.Rd == ID_EX_old.Rs1) {
            // Must check if MEM/WB is a load and EX/MEM is not
            if (!(EX_MEM_old.ctrl.RegWrite && EX_MEM_old.Rd != 0 && EX_MEM_old.Rd == ID_EX_old.Rs1)) {
                forwardA = 2; // Forward from MEM/WB
            }
        }

        // Check Forward B (Rs2)
        if (EX_MEM_old.ctrl.RegWrite && EX_MEM_old.Rd != 0 && EX_MEM_old.Rd == ID_EX_old.Rs2) {
            forwardB = 1; // Forward from EX/MEM (ALU result)
        } else if (MEM_WB_old.ctrl.RegWrite && MEM_WB_old.Rd != 0 && MEM_WB_old.Rd == ID_EX_old.Rs2) {
            if (!(EX_MEM_old.ctrl.RegWrite && EX_MEM_old.Rd != 0 && EX_MEM_old.Rd == ID_EX_old.Rs2)) {
                forwardB = 2; // Forward from MEM/WB
            }
        }
        
        // Apply Forwarding to aluInput1
        if (forwardA == 1) {
            aluInput1 = EX_MEM_old.ALUResult;
            if (debug) std::cout << "  EX: Forwarding Rs1 from EX/MEM\n";
        } else if (forwardA == 2) {
            aluInput1 = MEM_WB_old.ctrl.MemToReg ? MEM_WB_old.ReadData : MEM_WB_old.ALUResult;
            if (debug) std::cout << "  EX: Forwarding Rs1 from MEM/WB\n";
        }

        // Apply Forwarding to aluInput2 (only if not immediate)
        if (!ID_EX_old.ctrl.ALUSrc) {
            if (forwardB == 1) {
                aluInput2 = EX_MEM_old.ALUResult;
                if (debug) std::cout << "  EX: Forwarding Rs2 from EX/MEM\n";
            } else if (forwardB == 2) {
                aluInput2 = MEM_WB_old.ctrl.MemToReg ? MEM_WB_old.ReadData : MEM_WB_old.ALUResult;
                if (debug) std::cout << "  EX: Forwarding Rs2 from MEM/WB\n";
            }
        }
        
        // ALU Operation
        EX_MEM_new.ALUResult = ALU::operate(ID_EX_old.ctrl.ALUControl, aluInput1, aluInput2);
        
        // --- Branch Resolution (Control Hazard) ---
        EX_MEM_new.is_taken = false;
        if (ID_EX_old.ctrl.Branch) {
            uint32_t result = EX_MEM_new.ALUResult; // ALU performed subtraction (Rs1 - Rs2)
            bool condition = false;
            
            // Branch decision is based on funct3 and result of ALU subtraction
            switch (ID_EX_old.Funct3) {
                case 0x0: condition = (result == 0); break;      // BEQ (Rs1 == Rs2)
                case 0x1: condition = (result != 0); break;      // BNE (Rs1 != Rs2)
                case 0x4: condition = (static_cast<int32_t>(result) < 0); break; // BLT (Rs1 < Rs2 signed)
                case 0x5: condition = (static_cast<int32_t>(result) >= 0); break; // BGE (Rs1 >= Rs2 signed)
                case 0x6: condition = (result < 0); break;       // BLTU (Rs1 < Rs2 unsigned)
                case 0x7: condition = (result >= 0); break;      // BGEU (Rs1 >= Rs2 unsigned)
                default: break;
            }
            
            if (condition) {
                EX_MEM_new.is_taken = true; // Signal to the ID stage to flush IF/ID and ID/EX
                nextPC = ID_EX_old.PCplus4 - 4 + ID_EX_old.Immediate;
                if (debug) std::cout << "  EX: BRANCH TAKEN to 0x" << std::hex << nextPC << std::dec << "\n";
            }
        }

        // --- JAL/JALR Write Back Data ---
        if (ID_EX_old.ctrl.Jump) {
            if (ID_EX_old.Opcode == 0x6F) { // JAL
                EX_MEM_new.ALUResult = ID_EX_old.PCplus4; // Rd = PC+4
                nextPC = ID_EX_old.PCplus4 - 4 + ID_EX_old.Immediate;
            } else if (ID_EX_old.Opcode == 0x67) { // JALR
                EX_MEM_new.ALUResult = ID_EX_old.PCplus4; // Rd = PC+4
                nextPC = EX_MEM_new.ALUResult & ~1U; // Target is ALU result, LSB is zeroed
            }
            // All jumps are taken, so flush IF/ID and ID/EX immediately
            EX_MEM_new.is_taken = true;
        }

        // --- LUI/AUIPC ---
        if (ID_EX_old.ctrl.UType) {
            uint32_t imm_u = (ID_EX_old.Immediate << 12); // U-Type immediate is 20 bits
            if (ID_EX_old.Opcode == 0x37) { // LUI
                EX_MEM_new.ALUResult = imm_u;
            } else if (ID_EX_old.Opcode == 0x17) { // AUIPC
                EX_MEM_new.ALUResult = (ID_EX_old.PCplus4 - 4) + imm_u;
            }
        }
        
        if (debug) {
            std::cout << "  EX: Rd=x" << (int)ID_EX_old.Rd << ", ALU Result=0x" << std::hex << EX_MEM_new.ALUResult << std::dec << "\n";
        }
    }
    
    void stage_ID(bool debug) {
        // ID_EX_new is either a NOP (from hazard unit) or a real instruction (from IF/ID_old)
        if (ID_EX_new.is_valid == false) {
             // Already set to NOP by hazard unit
             if (debug) std::cout << "  ID: NOP (Stall Bubble)\n";
             return;
        }
        
        if (!IF_ID_old.is_valid || IF_ID_Flush) {
             // Insert NOP (Bubble) into ID/EX
             ID_EX_new.is_valid = false;
             ID_EX_new.ctrl.RegWrite = false;
             if (debug) std::cout << "  ID: NOP (Flush Bubble)\n";
             return;
        }

        uint32_t inst = IF_ID_old.Instruction;
        ID_EX_new.PCplus4 = IF_ID_old.PCplus4;
        ID_EX_new.ctrl = ControlUnit::generate(inst);
        
        ID_EX_new.Rd = (inst >> 7) & 0x1F;
        ID_EX_new.Rs1 = (inst >> 15) & 0x1F;
        ID_EX_new.Rs2 = (inst >> 20) & 0x1F;
        ID_EX_new.Funct3 = (inst >> 12) & 0x7;
        ID_EX_new.Opcode = inst & 0x7F;

        // Register Read
        ID_EX_new.ReadData1 = regs.read(ID_EX_new.Rs1);
        ID_EX_new.ReadData2 = regs.read(ID_EX_new.Rs2);

        // Immediate Generation (select type based on opcode)
        uint8_t opcode = inst & 0x7F;
        if (opcode == 0x6F) { // JAL
            ID_EX_new.Immediate = ImmediateGenerator::generateJType(inst);
        } else if (opcode == 0x63) { // Branch
            ID_EX_new.Immediate = ImmediateGenerator::generateBType(inst);
        } else if (opcode == 0x23) { // Store (S-Type)
            ID_EX_new.Immediate = ImmediateGenerator::generateSType(inst);
        } else { // I-Type (ALU Imm, Load, JALR) or U-Type (LUI/AUIPC)
            ID_EX_new.Immediate = ImmediateGenerator::generateIType(inst);
        }

        if (debug) {
            std::cout << "  ID: PC=0x" << std::hex << (IF_ID_old.PC) << ", Opcode=0x" << (int)ID_EX_new.Opcode
                      << ", Rs1=x" << (int)ID_EX_new.Rs1 << "(0x" << ID_EX_new.ReadData1 << ")"
                      << ", Rd=x" << (int)ID_EX_new.Rd << std::dec << "\n";
        }
    }

    void stage_IF(bool debug) {
        if (PC_Write == false) {
            if (debug) std::cout << "  IF: Stall (PC not written)\n";
            return; // PC is not written, IF_ID_new is also not written (remains old value)
        }

        // PC Update (if jump/branch taken, PC is already set by EX)
        if (EX_MEM_old.is_taken) {
            PC = nextPC;
        } else {
            PC = PC + 4;
        }

        // Check for program termination or out of bounds
        if (PC >= instrMem.size()) {
             // Treat this as a halt/termination, IF/ID will get a NOP
             IF_ID_new.is_valid = false;
             IF_ID_new.Instruction = 0x00000013;
             if (debug) std::cout << "  IF: HALT PC out of bounds\n";
             return;
        }

        // Instruction Fetch
        uint32_t inst = instrMem.readWord(PC);
        
        // Check for ecall (0x00000073u) which is the termination instruction
        if (inst == 0x00000073u) {
            IF_ID_new.is_valid = false;
            IF_ID_new.Instruction = inst;
            if (debug) std::cout << "  IF: HALT (ecall)\n";
            return;
        }
        
        if (IF_ID_Write) {
            IF_ID_new.PC = PC;
            IF_ID_new.PCplus4 = PC + 4;
            IF_ID_new.Instruction = inst;
            IF_ID_new.is_valid = true;
        }

        // Branch Prediction: Predict Not Taken. Jumps are resolved immediately in ID/EX.
        // nextPC is only used by EX to signal a taken branch.

        if (debug) {
            std::cout << "  IF: PC=0x" << std::hex << PC << ", Inst=0x" << inst << std::dec << "\n";
        }
    }

    void updatePipelineRegisters() {
        // IF/ID Latch update: Apply Flush/Stall logic
        if (IF_ID_Flush) {
            IF_ID_old.Instruction = 0x00000013; // Inject NOP
            IF_ID_old.is_valid = false;
        } else if (IF_ID_Write) {
            IF_ID_old = IF_ID_new;
        }
        
        // ID/EX Latch update: Apply Flush logic
        if (ID_EX_Flush) {
            ID_EX_old.ctrl.RegWrite = false; // All control signals to 0/NOP
            ID_EX_old.is_valid = false;
        } else {
            ID_EX_old = ID_EX_new;
        }

        EX_MEM_old = EX_MEM_new;
        MEM_WB_old = MEM_WB_new;
        
        // Reset flags for the next cycle
        PC_Write = true;
        IF_ID_Write = true;
        ID_EX_Flush = false;
        IF_ID_Flush = false;
    }
};

// Global function to load instructions
void loadInstructionsFromFile(PipelinedProcessor& cpu, const std::string& filename) {
    std::ifstream infile(filename);
    if (!infile) {
        std::cerr << "Cannot open instruction file " << filename << "\n";
        return;
    }
    std::string line;
    uint32_t addr = 0;
    int instructions_loaded = 0;
    
    while (std::getline(infile, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') continue;
        
        if (line.size() != 32) {
            std::cout << "Skipping line with invalid length: " << line.size() << " characters\n";
            continue;
        }
        
        uint32_t inst = 0;
        bool valid = true;
        for (int i = 0; i < 32; i++) {
            if (line[i] == '1') {
                inst |= (1U << (31 - i));
            } else if (line[i] != '0') {
                valid = false;
                break;
            }
        }
        
        if (!valid) {
            std::cout << "Skipping line with non-binary characters\n";
            continue;
        }
        
        if (addr + 3 >= cpu.getInstrMemSize()) {
            std::cerr << "Instruction file contains more instructions than instruction memory can hold; stopping load.\n";
            break;
        }
        
        cpu.loadInstruction(addr, inst);
        addr += 4;
        instructions_loaded++;
    }
    infile.close();
    
    std::cout << "Successfully loaded " << instructions_loaded << " instructions from " << filename << std::endl;
}

int main(int argc, char** argv) {
    // Create processor with 128KB instruction memory and 64KB data memory
    PipelinedProcessor cpu(128*1024, 64*1024);

    // Load instructions from file
    std::string filename = "output.txt";
    if (argc > 1) filename = argv[1];
    loadInstructionsFromFile(cpu, filename);

    // Initialize data memory with example array
    const uint32_t ARRAY_BASE = 0x1000;
    cpu.loadData(ARRAY_BASE + 0, 1);
    cpu.loadData(ARRAY_BASE + 4, 2);
    cpu.loadData(ARRAY_BASE + 8, 3);
    cpu.loadData(ARRAY_BASE + 12, 4);

    // Initialize stack pointer (x2)
    cpu.setRegister(2, 0x8000);
    // Initialize array base address into a register (e.g., x10)
    cpu.setRegister(10, ARRAY_BASE);
    // Initialize accumulator/temporary register (e.g., x5)
    cpu.setRegister(5, 0);

    // Run program until halt
    const uint32_t MAX_CYCLES = 20000;
    uint32_t current_pc = 0;
    int instructions_executed = 0;
    
    std::cout << "\n--- Starting Pipelined Simulation ---\n";

    for (uint32_t cycle = 1; cycle < MAX_CYCLES; ++cycle) {
        current_pc = cpu.getPC();
        
        // Read instruction at current PC for halt check
        if (current_pc < cpu.getInstrMemSize()) {
            uint32_t inst = cpu.readInstrWordForDebug(current_pc);
            if (inst == 0x00000073u) { // ecall (Halt)
                if (cycle > 5) break; // Allow pipeline to drain
            }
        } else if (current_pc >= cpu.getInstrMemSize() && cycle > 5) {
            // PC went out of bounds and pipeline has drained
             break;
        }

        if (cycle % 1000 == 0) {
            std::cout << "Cycle: " << cycle << "\n";
        }

        // Run one pipeline cycle
        cpu.runPipelinedCycle(false); // Set to 'true' for detailed debug output

        instructions_executed++;
    }
    
    std::cout << "\n--- Simulation Finished ---\n";
    std::cout << "Total Cycles: " << instructions_executed << "\n";

    // Print final state
    cpu.printRegisters();
    cpu.printDataMemory(0x1000, 4); // Print the 4 words of the array

    return 0;
}
