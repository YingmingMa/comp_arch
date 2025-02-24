#include <cstdint>
#include <iostream>
#include "processor.h"
using namespace std;
#define ENABLE_DEBUG
#ifdef ENABLE_DEBUG
#define DEBUG(x) x
#else
#define DEBUG(x) 
#endif

#include <cstring> 

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
    // std::cout <<  std::hex << regfile.pc << " Instruction: 0x" << std::hex << instruction << std::dec << "\n";

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

//     // Print instruction fields
// std::cout << "\n===== Instruction Fields =====\n";
// std::cout << "instruction: 0x" << std::hex << instruction << std::dec << "\n";
// std::cout << "opcode: " << opcode << " (0x" << std::hex << opcode << ")\n";
// std::cout << "rs: " << std::dec << rs << " (0x" << std::hex << rs << ")\n";
// std::cout << "rt: " << std::dec << rt << " (0x" << std::hex << rt << ")\n";
// std::cout << "rd: " << std::dec << rd << " (0x" << std::hex << rd << ")\n";
// std::cout << "shamt: " << std::dec << shamt << " (0x" << std::hex << shamt << ")\n";
// std::cout << "funct: " << std::dec << funct << " (0x" << std::hex << funct << ")\n";
// std::cout << "immediate: " << std::dec << imm << " (0x" << std::hex << imm << ")\n";
// std::cout << "jump address: " << std::dec << addr << " (0x" << std::hex << addr << ")\n";
// std::cout << "read_data_1: " << std::dec << read_data_1 << " (0x" << std::hex << read_data_1 << ")\n";
// std::cout << "read_data_2: " << std::dec << read_data_2 << " (0x" << std::hex << read_data_2 << ")\n";
// std::cout << "=============================\n" << std::dec;
    
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
    DEBUG(std::cout << "EX: imm = " << std::hex << imm <<::dec << "\n");
    DEBUG(std::cout << "EX: ALU result = " << std::hex << alu_result << std::dec << "\n");
    DEBUG(std::cout << "EX: ALU op = " << std::hex << operand_1 << " "<< operand_2<<::dec << "\n");

    
    
    
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

struct IF_ID_reg {
    uint32_t instruction;
    uint32_t pc;
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
    int addr;

    // Control signals
    bool ALU_src;           // 0 if second operand is from reg_file, 1 if imm
    bool reg_dest;          // 0 if rt, 1 if rd
    unsigned ALU_op : 2;    // 10 for R-type, 00 for LW/SW, 01 for BEQ/BNE, 11 for others
    bool shift;             // 1 if sll or srl
    bool mem_read;          // 1 if memory needs to be read
    bool mem_write;         // 1 if needs to be written to memory
    bool halfword;          // 1 if loading/storing halfword from memory
    bool byte;              // 1 if loading/storing a byte from memory
    bool reg_write;         // 1 if need to write back to reg file
    bool mem_to_reg;        // 1 if memory needs to written to reg

    // Branch/Jump control
    bool branch;            // 1 if branch
    bool bne;               // 1 if bne   
    bool jump;              // 1 if jummp
    bool jump_reg;          // 1 if jr
    bool link;              // 1 if jal
    uint32_t branch_target;
    uint32_t jump_target;

    uint32_t pc;
};

struct EX_MEM_reg {
    uint32_t alu_result;
    uint32_t write_data;
    int write_reg;
    
    bool mem_read;          // 1 if memory needs to be read
    bool mem_write;         // 1 if needs to be written to memory
    bool halfword;          // 1 if loading/storing halfword from memory
    bool byte;              // 1 if loading/storing a byte from memory
    bool reg_write;         // 1 if need to write back to reg file
    bool mem_to_reg;        // 1 if memory needs to written to reg
    
    // Branch results
    bool branch_taken;
    uint32_t branch_target;
    bool jump;
    uint32_t jump_target;

    uint32_t pc;
};

// struct MEM_WB_reg {
//     uint32_t read_data;
//     uint32_t alu_result;
//     int write_reg;
    
//     bool reg_write;
//     bool mem_to_reg;

//     uint32_t pc;
// };



void Processor::pipelined_processor_advance() {
    static IF_ID_reg if_id;
    static ID_EX_reg id_ex;
    static EX_MEM_reg ex_mem;
    // static MEM_WB_reg mem_wb;

    // fetch
    uint32_t instruction;
    memory->access(regfile.pc, instruction, 0, 1, 0);
    // std::cout <<  std::hex << regfile.pc << " Instruction: 0x" << std::hex << instruction << std::dec << "\n";

    // increment pc
    if_id.pc = regfile.pc + 4;

    //todo: may need conditiond so that will will stall
    if_id.instruction = instruction;
    
    // decode into contol signals
    control.decode(if_id.instruction);
    DEBUG(control.print());

    // Copy Control Signals Control signals
    id_ex.ALU_src = control.ALU_src;
    id_ex.reg_dest = control.reg_dest;
    id_ex.ALU_op = control.ALU_op;
    id_ex.shift = control.shift;
    id_ex.mem_read = control.mem_read;
    id_ex.mem_write = control.mem_write;
    id_ex.reg_write = control.reg_write;
    id_ex.mem_to_reg = control.mem_to_reg;
    id_ex.branch = control.branch;
    id_ex.bne = control.bne;
    id_ex.jump = control.jump;
    id_ex.jump_reg = control.jump_reg;
    id_ex.link = control.link;
    id_ex.halfword = control.halfword;
    id_ex.byte = control.byte;

    // extract rs, rt, rd, imm, funct 
    id_ex.opcode = (if_id.instruction >> 26) & 0x3f;
    id_ex.rs = (if_id.instruction >> 21) & 0x1f;
    id_ex.rt = (if_id.instruction >> 16) & 0x1f;
    id_ex.rd = (if_id.instruction >> 11) & 0x1f;
    id_ex.shamt = (if_id.instruction >> 6) & 0x1f;
    id_ex.funct = if_id.instruction & 0x3f;
    id_ex.imm = (if_id.instruction & 0xffff);
    id_ex.addr = if_id.instruction & 0x3ffffff;

    // //may not be necessary in decode
    // // Calculate jump and branch targets in decode stage
    // uint32_t jump_addr = if_id.instruction & 0x03FFFFFF;  
    // id_ex.jump_target = (if_id.pc & 0xF0000000) | (jump_addr << 2);  
    // id_ex.branch_target = if_id.pc + 4 + (id_ex.imm << 2); 
    
    // Sign Extend Or Zero Extend the immediate (seems like it is in ID stage from graph)
    // Using Arithmetic right shift in order to replicate 1 
    id_ex.imm = control.zero_extend ? id_ex.imm : (id_ex.imm >> 15) ? 0xffff0000 | id_ex.imm : id_ex.imm;

    // Read from reg file
    regfile.access(id_ex.rs, id_ex.rt, id_ex.read_data_1, id_ex.read_data_2, 0, 0, 0);
    
    // Execution 
    alu.generate_control_inputs(id_ex.ALU_op, id_ex.funct, id_ex.opcode);
    
    // Find operands for the ALU Execution
    // Operand 1 is always R[rs] -> read_data_1, except sll and srl
    // Operand 2 is immediate if ALU_src = 1, for I-type
    uint32_t operand_1 = id_ex.shift ? id_ex.shamt : read_data_1;
    uint32_t operand_2 = id_ex.ALU_src ? id_ex.imm : read_data_2;
    uint32_t alu_zero = 0;

    uint32_t alu_result = alu.execute(operand_1, operand_2, alu_zero);
    DEBUG(std::cout << "EX: imm = " << std::hex << id_ex.imm <<::dec << "\n");
    DEBUG(std::cout << "EX: ALU result = " << std::hex << alu_result << std::dec << "\n");
    DEBUG(std::cout << "EX: ALU op = " << std::hex << operand_1 << " "<< operand_2<<::dec << "\n");

    // EX/MEM ← ID/EX
    ex_mem.alu_result = alu_result;
    ex_mem.write_data = id_ex.imm; //todo: may need to change when implementing forward
    ex_mem.write_reg = id_ex.reg_dest ? id_ex.rd : id_ex.rt;
    ex_mem.mem_read = id_ex.mem_read;
    ex_mem.mem_write = id_ex.mem_write;
    ex_mem.reg_write = id_ex.reg_write;
    ex_mem.mem_to_reg = id_ex.mem_to_reg;
    ex_mem.branch_taken = actual_branch_taken;
    ex_mem.branch_target = id_ex.branch_target;
    ex_mem.jump = id_ex.jump || id_ex.jump_reg;
    ex_mem.jump_target = id_ex.jump_target;
    ex_mem.pc = id_ex.pc; 

    
    
    // Memory    
    uint32_t read_data_mem = 0;
    uint32_t write_data_mem = 0;

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

