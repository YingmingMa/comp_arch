#include <cstdint>
#include <iostream>
#include "processor.h"
using namespace std;

#ifdef ENABLE_DEBUG
#define DEBUG(x) x
#else
#define DEBUG(x) 
#endif

void Processor::initialize(int level) {
    // Initialize Control
    control = {.reg_dest = 0, 
               .jump = 0,
               .jump_reg = 0,
               .link = 0,
               .shift = 0,
               .branch = 0,
               .bne = 0,
               .mem_read = 0,
               .mem_to_reg = 0,
               .ALU_op = 0,
               .mem_write = 0,
               .halfword = 0,
               .byte = 0,
               .ALU_src = 0,
               .reg_write = 0,
               .zero_extend = 0};
   
    opt_level = level;
    // Optimization level-specific initialization
}

void Processor::advance() {
    switch (opt_level) {
        case 0: single_cycle_processor_advance();
                break;
        case 1: pipelined_processor_advance();
                break;
        // other optimization levels go here
        default: break;
    }
}

void Processor::single_cycle_processor_advance() {
    // fetch
    uint32_t instruction;
    memory->access(regfile.pc, instruction, 0, 1, 0);
    DEBUG(cout << "\nPC: 0x" << std::hex << regfile.pc << std::dec << "\n");
    // increment pc
    regfile.pc += 4;
    
    // decode into contol signals
    control.decode(instruction);
    DEBUG(control.print());

    // extract rs, rt, rd, imm, funct 
    int opcode = (instruction >> 26) & 0x3f;
    int rs = (instruction >> 21) & 0x1f;
    int rt = (instruction >> 16) & 0x1f;
    int rd = (instruction >> 11) & 0x1f;
    int shamt = (instruction >> 6) & 0x1f;
    int funct = instruction & 0x3f;
    uint32_t imm = (instruction & 0xffff);
    int addr = instruction & 0x3ffffff;
    // Variables to read data into
    uint32_t read_data_1 = 0;
    uint32_t read_data_2 = 0;
    
    // Read from reg file
    regfile.access(rs, rt, read_data_1, read_data_2, 0, 0, 0);
    
    // Execution 
    alu.generate_control_inputs(control.ALU_op, funct, opcode);
   
    // Sign Extend Or Zero Extend the immediate
    // Using Arithmetic right shift in order to replicate 1 
    imm = control.zero_extend ? imm : (imm >> 15) ? 0xffff0000 | imm : imm;
    
    // Find operands for the ALU Execution
    // Operand 1 is always R[rs] -> read_data_1, except sll and srl
    // Operand 2 is immediate if ALU_src = 1, for I-type
    uint32_t operand_1 = control.shift ? shamt : read_data_1;
    uint32_t operand_2 = control.ALU_src ? imm : read_data_2;
    uint32_t alu_zero = 0;

    uint32_t alu_result = alu.execute(operand_1, operand_2, alu_zero);
    
    
    uint32_t read_data_mem = 0;
    uint32_t write_data_mem = 0;

    // Memory
    // First read no matter whether it is a load or a store
    memory->access(alu_result, read_data_mem, 0, control.mem_read | control.mem_write, 0);
    // Stores: sb or sh mask and preserve original leftmost bits
    write_data_mem = control.halfword ? (read_data_mem & 0xffff0000) | (read_data_2 & 0xffff) : 
                    control.byte ? (read_data_mem & 0xffffff00) | (read_data_2 & 0xff): read_data_2;
    // Write to memory only if mem_write is 1, i.e store
    memory->access(alu_result, read_data_mem, write_data_mem, control.mem_read, control.mem_write);
    // Loads: lbu or lhu modify read data by masking
    read_data_mem &= control.halfword ? 0xffff : control.byte ? 0xff : 0xffffffff;

    int write_reg = control.link ? 31 : control.reg_dest ? rd : rt;

    uint32_t write_data = control.link ? regfile.pc+8 : control.mem_to_reg ? read_data_mem : alu_result;  

    // Write Back
    regfile.access(0, 0, read_data_2, read_data_2, write_reg, control.reg_write, write_data);
    
    // Update PC
    regfile.pc += (control.branch && !control.bne && alu_zero) || (control.bne && !alu_zero) ? imm << 2 : 0; 
    regfile.pc = control.jump_reg ? read_data_1 : control.jump ? (regfile.pc & 0xf0000000) & (addr << 2): regfile.pc;
}

// void Processor::pipelined_processor_advance() {
//     // pipelined processor logic goes here
//     // does nothing currently -- if you call it from the cmd line, you'll run into an infinite loop
//     // might be helpful to implement stages in a separate module

//     // fetch
//     uint32_t instruction;
//     memory->access(regfile.pc, instruction, 0, 1, 0);
//     DEBUG(cout << "\nPC: 0x" << std::hex << regfile.pc << std::dec << "\n");
//     // increment pc
//     regfile.pc += 4;
    
//     // decode into contol signals
//     control.decode(instruction);
//     DEBUG(control.print());

//     // extract rs, rt, rd, imm, funct 
//     int opcode = (instruction >> 26) & 0x3f;
//     int rs = (instruction >> 21) & 0x1f;
//     int rt = (instruction >> 16) & 0x1f;
//     int rd = (instruction >> 11) & 0x1f;
//     int shamt = (instruction >> 6) & 0x1f;
//     int funct = instruction & 0x3f;
//     uint32_t imm = (instruction & 0xffff);
//     int addr = instruction & 0x3ffffff;
//     // Variables to read data into
//     uint32_t read_data_1 = 0;
//     uint32_t read_data_2 = 0;
    
//     // Read from reg file
//     regfile.access(rs, rt, read_data_1, read_data_2, 0, 0, 0);
    
//     // Execution 
//     alu.generate_control_inputs(control.ALU_op, funct, opcode);
   
//     // Sign Extend Or Zero Extend the immediate
//     // Using Arithmetic right shift in order to replicate 1 
//     imm = control.zero_extend ? imm : (imm >> 15) ? 0xffff0000 | imm : imm;
    
//     // Find operands for the ALU Execution
//     // Operand 1 is always R[rs] -> read_data_1, except sll and srl
//     // Operand 2 is immediate if ALU_src = 1, for I-type
//     uint32_t operand_1 = control.shift ? shamt : read_data_1;
//     uint32_t operand_2 = control.ALU_src ? imm : read_data_2;
//     uint32_t alu_zero = 0;

//     uint32_t alu_result = alu.execute(operand_1, operand_2, alu_zero);
    
    
//     uint32_t read_data_mem = 0;
//     uint32_t write_data_mem = 0;

//     // Memory
//     // First read no matter whether it is a load or a store
//     memory->access(alu_result, read_data_mem, 0, control.mem_read | control.mem_write, 0);
//     // Stores: sb or sh mask and preserve original leftmost bits
//     write_data_mem = control.halfword ? (read_data_mem & 0xffff0000) | (read_data_2 & 0xffff) : 
//                     control.byte ? (read_data_mem & 0xffffff00) | (read_data_2 & 0xff): read_data_2;
//     // Write to memory only if mem_write is 1, i.e store
//     memory->access(alu_result, read_data_mem, write_data_mem, control.mem_read, control.mem_write);
//     // Loads: lbu or lhu modify read data by masking
//     read_data_mem &= control.halfword ? 0xffff : control.byte ? 0xff : 0xffffffff;

//     int write_reg = control.link ? 31 : control.reg_dest ? rd : rt;

//     uint32_t write_data = control.link ? regfile.pc+8 : control.mem_to_reg ? read_data_mem : alu_result;  

//     // Write Back
//     regfile.access(0, 0, read_data_2, read_data_2, write_reg, control.reg_write, write_data);
    
//     // Update PC
//     regfile.pc += (control.branch && !control.bne && alu_zero) || (control.bne && !alu_zero) ? imm << 2 : 0; 
//     regfile.pc = control.jump_reg ? read_data_1 : control.jump ? (regfile.pc & 0xf0000000) & (addr << 2): regfile.pc;

    
// }
struct IF_ID_reg {
    uint32_t instruction;
};

struct ID_EX_reg {
    // Data read from registers
    uint32_t read_data_1;
    uint32_t read_data_2;
    
    // Instruction fields decoded in ID
    int opcode;
    int rs;
    int rt;
    int rd;
    int shamt;
    int funct;
    uint32_t imm;
    
    // Control signals
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

    // Branch/Jump control
    bool branch;
    bool bne;
    bool jump;
    bool jump_reg;
    bool link;
    uint32_t branch_target;
    uint32_t jump_target;
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
    
    // Branch results
    bool branch_taken;
    uint32_t branch_target;
    bool jump;
    uint32_t jump_target;
};

struct MEM_WB_reg {
    uint32_t read_data;
    uint32_t alu_result;
    int write_reg;
    
    bool reg_write;
    bool mem_to_reg;
};

void Processor::pipelined_processor_advance() {
    static IF_ID_reg if_id;
    static ID_EX_reg id_ex;
    static EX_MEM_reg ex_mem;
    static MEM_WB_reg mem_wb;
    
    static uint32_t current_ex_result;
    static uint32_t current_mem_result;

    bool flush = false;
    uint32_t new_pc = regfile.pc + 4;  // Default next PC


    // WB Stage
    if (mem_wb.reg_write) {
        uint32_t read_data_1, read_data_2;
        uint32_t write_data = mem_wb.mem_to_reg ? mem_wb.read_data : mem_wb.alu_result;
        regfile.access(0, 0, read_data_1, read_data_2, mem_wb.write_reg, true, write_data);
    }
  
    // MEM Stage
    if (ex_mem.mem_read || ex_mem.mem_write) {
        memory->access(ex_mem.alu_result, current_mem_result,
                      ex_mem.write_data,
                      ex_mem.mem_read,
                      ex_mem.mem_write);
    }
  

    // Check for hazards
    bool stall = false;
    if (ex_mem.mem_read && ex_mem.write_reg != 0) {
        if (id_ex.rs == ex_mem.write_reg || id_ex.rt == ex_mem.write_reg) {
            stall = true;
        }
    }



    // EX Stage
    uint32_t forward_data1 = id_ex.read_data_1;
    uint32_t forward_data2 = id_ex.read_data_2;

    // Forward from EX/MEM
    if (ex_mem.reg_write && ex_mem.write_reg != 0 && !ex_mem.mem_read) {
        if (id_ex.rs == ex_mem.write_reg) {
            forward_data1 = current_ex_result;

        }
        if (id_ex.rt == ex_mem.write_reg) {
            forward_data2 = current_ex_result;

        }
    }

    // Forward from MEM/WB
    if (mem_wb.reg_write && mem_wb.write_reg != 0) {
        if (id_ex.rs == mem_wb.write_reg) {
            forward_data1 = mem_wb.mem_to_reg ? mem_wb.read_data : mem_wb.alu_result;

        }
        if (id_ex.rt == mem_wb.write_reg) {
            forward_data2 = mem_wb.mem_to_reg ? mem_wb.read_data : mem_wb.alu_result;

        }
    }

    uint32_t alu_zero;
    uint32_t operand_1 = id_ex.shift ? id_ex.shamt : forward_data1;
    uint32_t operand_2 = id_ex.ALU_src ? id_ex.imm : forward_data2;
    
    alu.generate_control_inputs(id_ex.ALU_op, id_ex.funct, id_ex.opcode);
    current_ex_result = alu.execute(operand_1, operand_2, alu_zero);
   
    // Branch/Jump decision in EX stage
    bool actual_branch_taken = (id_ex.branch && !id_ex.bne && alu_zero) || 
                             (id_ex.branch && id_ex.bne && !alu_zero);
    
    if (actual_branch_taken || id_ex.jump || id_ex.jump_reg) {
        flush = true;
        if (id_ex.jump_reg) {
            new_pc = forward_data1;
        } else if (id_ex.jump) {
            new_pc = id_ex.jump_target;
        } else {
            new_pc = id_ex.branch_target;
        }
    }




    // Update pipeline registers
    if (!stall) {
        // MEM/WB ← EX/MEM
        mem_wb.alu_result = ex_mem.alu_result;
        mem_wb.read_data = current_mem_result;
        mem_wb.write_reg = ex_mem.write_reg;
        mem_wb.reg_write = ex_mem.reg_write;
        mem_wb.mem_to_reg = ex_mem.mem_to_reg;

        // EX/MEM ← ID/EX
        ex_mem.alu_result = current_ex_result;
        ex_mem.write_data = forward_data2;
        ex_mem.write_reg = id_ex.reg_dest ? id_ex.rd : id_ex.rt;
        ex_mem.mem_read = id_ex.mem_read;
        ex_mem.mem_write = id_ex.mem_write;
        ex_mem.reg_write = id_ex.reg_write;
        ex_mem.mem_to_reg = id_ex.mem_to_reg;
        ex_mem.branch_taken = actual_branch_taken;
        ex_mem.branch_target = id_ex.branch_target;
        ex_mem.jump = id_ex.jump || id_ex.jump_reg;
        ex_mem.jump_target = id_ex.jump_target;

        if (!flush) {
            // ID/EX ← IF/ID
            control_t control;
            control.decode(if_id.instruction);
            
            id_ex.opcode = (if_id.instruction >> 26) & 0x3f;
            id_ex.rs = (if_id.instruction >> 21) & 0x1f;
            id_ex.rt = (if_id.instruction >> 16) & 0x1f;
            id_ex.rd = (if_id.instruction >> 11) & 0x1f;
            id_ex.shamt = (if_id.instruction >> 6) & 0x1f;
            id_ex.funct = if_id.instruction & 0x3f;
            id_ex.imm = (if_id.instruction & 0xffff);
            
            id_ex.imm = control.zero_extend ? id_ex.imm : (id_ex.imm >> 15) ? 0xffff0000 | id_ex.imm : id_ex.imm;
            
            regfile.access(id_ex.rs, id_ex.rt, id_ex.read_data_1, id_ex.read_data_2, 0, false, 0);
            
            // Control signals
            id_ex.ALU_src = control.ALU_src;
            id_ex.reg_dest = control.reg_dest;
            id_ex.ALU_op = control.ALU_op;
            id_ex.shift = control.shift;
            id_ex.mem_read = control.mem_read;
            id_ex.mem_write = control.mem_write;
            id_ex.reg_write = control.reg_write;
            id_ex.mem_to_reg = control.mem_to_reg;
            
            // Branch/Jump control
            id_ex.branch = control.branch;
            id_ex.bne = control.bne;
            id_ex.jump = control.jump;
            id_ex.jump_reg = control.jump_reg;
            id_ex.link = control.link;

            } else {
            // Clear ID/EX on flush
            memset(&id_ex, 0, sizeof(ID_EX_reg));
        }

        // IF Stage
        if (!flush) {
            uint32_t next_instruction;
            memory->access(regfile.pc, next_instruction, 0, true, false);
            if_id.instruction = next_instruction;
            } else {
            memset(&if_id, 0, sizeof(IF_ID_reg));
        }
        regfile.pc = new_pc + 4;
    } else {
        // Stall case
        // Update MEM/WB normally
        mem_wb.alu_result = ex_mem.alu_result;
        mem_wb.read_data = current_mem_result;
        mem_wb.write_reg = ex_mem.write_reg;
        mem_wb.reg_write = ex_mem.reg_write;
        mem_wb.mem_to_reg = ex_mem.mem_to_reg;

        // Clear EX/MEM 
        memset(&ex_mem, 0, sizeof(EX_MEM_reg));
    }

}