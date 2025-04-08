#ifndef OOO_PROCESSOR_H
#define OOO_PROCESSOR_H

#include <cstdint>
#include <vector>
#include <queue>
#include <unordered_map>
#include "memory.h"
#include "regfile.h"
#include "ALU.h"
#include "control.h"

// Structures for out-of-order pipeline remain unchanged.
struct ROBEntry {
    int dest_arch_reg;
    int dest_phys_reg;
    bool ready;
    uint32_t value;
    bool is_mem;
    bool is_branch;
    bool mispredicted;
};

struct ReservationStation {
    bool busy;
    int opcode;
    int src1, src2;        // Physical register IDs.
    bool src1_ready, src2_ready;
    int dest;              // Destination physical register.
    int rob_index;         // Index into the ROB.
};

// Remove the IF_ID_reg structure; instead, use an instruction queue.
// Limit the Instruction Queue size to a fixed capacity, e.g., 16.
struct InstructionQueue {
    std::queue<uint32_t> instQueue;
    const size_t capacity;  // Maximum number of instructions allowed.

    // Constructor: default capacity provided as 16.
    InstructionQueue(size_t cap = 16) : capacity(cap) {}

    // Check if the queue is full.
    bool full() const {
        return instQueue.size() >= capacity;
    }

    // Push an instruction if there is room. Returns true if successful.
    bool push(uint32_t instruction) {
        if (full())
            return false;  // Queue is full; cannot push.
        instQueue.push(instruction);
        return true;
    }

    // Other helper functions.
    bool empty() const {
        return instQueue.empty();
    }

    uint32_t front() {
        return instQueue.front();
    }

    void pop() {
        instQueue.pop();
    }
};


struct ID_EX_reg {
    uint32_t read_data_1;
    uint32_t read_data_2;
    int opcode;
    int rs;
    int rt;
    int rd;
    int shamt;
    int funct;
    uint32_t imm;
    // Control signals.
    bool ALU_src;
    bool reg_dest;
    unsigned ALU_op : 2;
    bool shift;
    bool mem_read;
    bool mem_write;
    bool halfword;
    bool byte;
    bool reg_write;
    bool mem_to_reg;
    // Branch/Jump control.
    bool branch;
    bool bne;
    bool jump;
    bool jump_reg;
    bool link;
    uint32_t branch_target;
    uint32_t jump_target;
    uint32_t pc;
};

struct EX_MEM_reg {
    uint32_t alu_result;
    uint32_t write_data;
    int write_reg;
    bool mem_read;
    bool mem_write;
    bool halfword;
    bool byte;
    bool reg_write;
    bool mem_to_reg;
    bool branch_taken;
    uint32_t branch_target;
    bool jump;
    uint32_t jump_target;
    uint32_t pc;
    bool link;
};

struct MEM_WB_reg {
    uint32_t write_data;
    int write_reg;
    bool reg_write;
    uint32_t pc;
};

// New: Physical Register File structure.
struct PhysicalRegisterFile {
    std::vector<uint32_t> regs;
    // Construct 'num_regs' physical registers (initialized to 0).
    PhysicalRegisterFile(int num_regs) : regs(num_regs, 0) {}
    uint32_t read(int index) const {
        return regs[index];
    }
    void write(int index, uint32_t value) {
        regs[index] = value;
    }
};

class OooProcessor {
public:
    // Constructor now also initializes the physical register file.
    OooProcessor(Memory* mem, Registers &rf, ALU &alu);
    
    // Main advance routine for one OoO cycle.
    void advance();

private:
    // Instead of a separate IF/ID register, we use an instruction queue.
    InstructionQueue instQueue;
    
    // Remaining pipeline registers.
    ID_EX_reg   id_ex;
    EX_MEM_reg  ex_mem;
    MEM_WB_reg  mem_wb;
    uint32_t current_pc;
    
    // Data structures for out-of-order execution.
    std::vector<ROBEntry> rob;
    std::vector<ReservationStation> reservationStations;
    std::unordered_map<int, int> rat;      // Register Alias Table: arch reg -> physical reg.
    std::queue<int> free_list;             // Pool of free physical registers.
    
    // New physical register file.
    PhysicalRegisterFile phys_reg_file;
    
    // External components.
    Memory*   memory;
    Registers regfile;
    ALU&      alu;
    control_t control;
    
    // Internal helper functions representing pipeline stages.
    void fetchStage();
    void decodeStage();
    void registerRenamingStage();
    void executeStage();
    void memoryStage();
    void commitStage();
};

#endif // OOO_PROCESSOR_H
