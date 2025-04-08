#include "ooo_processor.h"
#include <iostream>
#include <cstring>  // For memset

OooProcessor::OooProcessor(Memory* mem, Registers &rf, ALU &alu_ref)
    : memory(mem),
      regfile(rf),
      alu(alu_ref),
      current_pc(0),
      phys_reg_file(64),
      instQueue(16)
{
    // Initialize pipeline registers.
    memset(&id_ex, 0, sizeof(ID_EX_reg));
    memset(&ex_mem, 0, sizeof(EX_MEM_reg));
    memset(&mem_wb, 0, sizeof(MEM_WB_reg));
    
    // Initialize free list (assume physical registers 32-63 are available for renaming).
    for (int i = 32; i < 64; ++i) {
        free_list.push(i);
    }
    
    // Initialize the Register Alias Table (RAT) so each architectural register initially maps to the same physical register.
    // For registers 0 to 31.
    for (int reg = 0; reg < 32; ++reg) {
        rat[reg] = reg;
    }
    
    // Initialize ROB as an empty vector.
    rob.clear();
    
    // Initialize a fixed number of reservation station entries (e.g., 8 entries).
    reservationStations.resize(8);
    for (size_t i = 0; i < reservationStations.size(); i++) {
        reservationStations[i].busy = false;
        reservationStations[i].opcode = 0;
        reservationStations[i].src1 = 0;
        reservationStations[i].src2 = 0;
        reservationStations[i].src1_ready = false;
        reservationStations[i].src2_ready = false;
        reservationStations[i].dest = 0;
        reservationStations[i].rob_index = -1;  // -1 indicates an invalid/unused entry.
    }
    
    // The InstructionQueue (instQueue) is default constructed.
    
    std::cout << "Initialized OoO processor:\n"
              << "  Free list contains " << free_list.size() << " registers.\n"
              << "  RAT mapping established for 32 registers.\n"
              << "  " << reservationStations.size() << " reservation station entries allocated.\n"
              << "  Physical register file size: " << phys_reg_file.regs.size() << " registers."
              << std::endl;
}


void OooProcessor::fetchStage() {
    // Fetch the next instruction from memory.
    uint32_t instruction;
    if (memory->access(current_pc, instruction, 0, 1, 0)) {
        // Only push if there is room; otherwise, stall this stage.
        if (instQueue.push(instruction)) {
            std::cout << "Fetched instruction 0x" << std::hex << instruction
                      << " from PC = 0x" << std::hex << current_pc << std::dec << std::endl;
            current_pc += 4;
        } else {
            std::cerr << "Instruction Queue full! Stalling fetch stage." << std::endl;
            // Optionally add stall logic here.
        }
    }
}

void OooProcessor::decodeStage() {
    // Check if there is an instruction in the instruction queue.
    if (instQueue.empty()) {
        return;
    }
    
    // Retrieve and remove the front instruction from the queue.
    uint32_t instruction = instQueue.front();
    instQueue.pop();
    
    // Decode the instruction using a temporary control unit.
    control_t control;
    control.decode(instruction);
    
    // Fill in the ID/EX pipeline register fields.
    id_ex.opcode = (instruction >> 26) & 0x3f;
    id_ex.rs = (instruction >> 21) & 0x1f;
    id_ex.rt = (instruction >> 16) & 0x1f;
    id_ex.rd = (instruction >> 11) & 0x1f;
    id_ex.shamt = (instruction >> 6) & 0x1f;
    id_ex.funct = instruction & 0x3f;
    id_ex.imm = (instruction & 0xffff);
    id_ex.pc = current_pc - 4; // The PC corresponding to the fetched instruction.
    
    // Sign or zero extend the immediate.
    id_ex.imm = control.zero_extend ? id_ex.imm : ((id_ex.imm >> 15) ? (0xffff0000 | id_ex.imm) : id_ex.imm);
    
    // Calculate jump and branch targets.
    uint32_t jump_addr = instruction & 0x03ffffff;
    id_ex.jump_target = (id_ex.pc & 0xf0000000) | (jump_addr << 2);
    id_ex.branch_target = id_ex.pc + 4 + (id_ex.imm << 2);
    
    // Read operand values from the architectural register file.
    // (In a full OoO design, this will eventually come from the physical register file after renaming.)
    regfile.access(id_ex.rs, id_ex.rt, id_ex.read_data_1, id_ex.read_data_2, 0, false, 0);
    
    // Pass through control signals.
    id_ex.ALU_src    = control.ALU_src;
    id_ex.reg_dest   = control.reg_dest;
    id_ex.ALU_op     = control.ALU_op;
    id_ex.shift      = control.shift;
    id_ex.mem_read   = control.mem_read;
    id_ex.mem_write  = control.mem_write;
    id_ex.reg_write  = control.reg_write;
    id_ex.mem_to_reg = control.mem_to_reg;
    id_ex.halfword   = control.halfword;
    id_ex.byte       = control.byte;
    
    // Branch/Jump controls.
    id_ex.branch     = control.branch;
    id_ex.bne        = control.bne;
    id_ex.jump       = control.jump;
    id_ex.jump_reg   = control.jump_reg;
    id_ex.link       = control.link;
    
    std::cout << "Decoded instruction: opcode " << id_ex.opcode 
              << ", rs R" << id_ex.rs << ", rt R" << id_ex.rt 
              << ", rd R" << id_ex.rd << " at PC = 0x" 
              << std::hex << id_ex.pc << std::dec << std::endl;
}

void OooProcessor::registerRenamingStage() {
    // Update the source operands by retrieving their physical register mappings from the RAT.
    // id_ex.rs and id_ex.rt originally hold architectural register numbers.
    id_ex.rs = rat[id_ex.rs];
    id_ex.rt = rat[id_ex.rt];

    // Determine the architectural destination register.
    // If this is a jump-and-link instruction, destination is register 31.
    // Otherwise, use id_ex.rd if reg_dest is asserted; if not, the destination might be in rt.
    int dest_arch = -1;
    if (id_ex.link) {
        dest_arch = 31;
    } else {
        dest_arch = id_ex.reg_dest ? id_ex.rd : id_ex.rt;
    }
    
    // Check that a free physical register is available.
    if (!free_list.empty()) {
        // Allocate a new physical register for the destination.
        int new_phys = free_list.front();
        free_list.pop();
        
        // Update the RAT: architectural destination now maps to the newly allocated physical register.
        rat[dest_arch] = new_phys;
        
        // Update the ID/EX pipeline register to hold the renamed destination.
        // (This overwrites id_ex.rd, so later stages can use it as the destination physical register.)
        id_ex.rd = new_phys;
        
        // Create a new ROB entry for the instruction.
        ROBEntry new_rob_entry;
        new_rob_entry.dest_arch_reg = dest_arch;
        new_rob_entry.dest_phys_reg = new_phys;
        new_rob_entry.ready = false;      // The instruction hasn't completed yet.
        new_rob_entry.value = 0;          // The result will be written when execution completes.
        new_rob_entry.is_mem = id_ex.mem_read || id_ex.mem_write;
        new_rob_entry.is_branch = id_ex.branch || id_ex.jump || id_ex.jump_reg;
        new_rob_entry.mispredicted = false;
        
        // Add the new ROB entry to the reorder buffer.
        rob.push_back(new_rob_entry);
        
        // (Optionally, you could store the new ROB entry index in id_ex if further tracking is desired.)
    } else {
        // No free physical registers available: the pipeline must stall until one is freed.
        std::cerr << "Error: No free physical registers available! Stalling register renaming." << std::endl;
        // Insert your stall-handling logic here.
    }
}

void OooProcessor::executeStage() {
    // Forwarding: update ID/EX from later stages if needed.
    if (ex_mem.reg_write && ex_mem.write_reg != 0) {
        if (id_ex.rs == ex_mem.write_reg)
            id_ex.read_data_1 = ex_mem.alu_result;
        if (id_ex.rt == ex_mem.write_reg)
            id_ex.read_data_2 = ex_mem.alu_result;
    }
    
    uint32_t alu_zero = 0;
    uint32_t operand_1 = id_ex.shift ? id_ex.shamt : id_ex.read_data_1;
    uint32_t operand_2 = id_ex.ALU_src ? id_ex.imm : id_ex.read_data_2;
    
    alu.generate_control_inputs(id_ex.ALU_op, id_ex.funct, id_ex.opcode);
    uint32_t ex_result = alu.execute(operand_1, operand_2, alu_zero);
    
    bool actual_branch_taken = (id_ex.branch && !id_ex.bne && alu_zero) ||
                                 (id_ex.bne && !alu_zero);
    
    uint32_t new_pc = current_pc;
    if (actual_branch_taken || id_ex.jump || id_ex.jump_reg) {
        if (id_ex.jump_reg) {
            new_pc = id_ex.read_data_1;
        } else if (id_ex.jump) {
            new_pc = id_ex.jump_target;
        } else {
            new_pc = id_ex.branch_target;
        }
    }
    
    // Update EX/MEM pipeline register.
    ex_mem.alu_result = ex_result;
    ex_mem.write_data = id_ex.read_data_2;
    ex_mem.write_reg = id_ex.link ? 31 : (id_ex.reg_dest ? id_ex.rd : id_ex.rt);
    ex_mem.mem_read = id_ex.mem_read;
    ex_mem.mem_write = id_ex.mem_write;
    ex_mem.reg_write = id_ex.reg_write;
    ex_mem.mem_to_reg = id_ex.mem_to_reg;
    ex_mem.branch_taken = actual_branch_taken;
    ex_mem.branch_target = id_ex.branch_target;
    ex_mem.jump = id_ex.jump || id_ex.jump_reg;
    ex_mem.jump_target = id_ex.jump_target;
    ex_mem.pc = id_ex.pc;
    ex_mem.byte = id_ex.byte;
    ex_mem.halfword = id_ex.halfword;
    ex_mem.link = id_ex.link;
    
    // Update current PC in case of taken branch/jump.
    current_pc = new_pc;
}

void OooProcessor::memoryStage() {
    // Forward results from MEM/WB to earlier stages, if needed.
    if (mem_wb.reg_write && mem_wb.write_reg != 0) {
        if (id_ex.rs == mem_wb.write_reg)
            id_ex.read_data_1 = mem_wb.write_data;
        if (id_ex.rt == mem_wb.write_reg)
            id_ex.read_data_2 = mem_wb.write_data;
    }
    
    uint32_t read_data_mem = 0, write_data_mem = 0;
    if (ex_mem.mem_read || ex_mem.mem_write) {
        if (ex_mem.mem_read) {
            if (!memory->access(ex_mem.alu_result, read_data_mem, ex_mem.write_data,
                                ex_mem.mem_read, ex_mem.mem_write))
                return;
        }
        if (ex_mem.mem_write) {
            if (ex_mem.halfword || ex_mem.byte) {
                if (!memory->access(ex_mem.alu_result, read_data_mem, ex_mem.write_data,
                                    ex_mem.mem_read, ex_mem.mem_write))
                    return;
                write_data_mem = ex_mem.halfword ? 
                    (read_data_mem & 0xffff0000) | (ex_mem.write_data & 0xffff) :
                    ex_mem.byte ? (read_data_mem & 0xffffff00) | (ex_mem.write_data & 0xff) :
                    ex_mem.write_data;
            } else {
                write_data_mem = ex_mem.write_data;
            }
            if (!memory->access(ex_mem.alu_result, read_data_mem, write_data_mem,
                                ex_mem.mem_read, ex_mem.mem_write))
                return;
        }
        read_data_mem &= ex_mem.halfword ? 0xffff : ex_mem.byte ? 0xff : 0xffffffff;
    }
    
    uint32_t write_data = ex_mem.mem_to_reg ? read_data_mem : ex_mem.alu_result;
    if (ex_mem.link) {
        write_data = ex_mem.pc + 8;
    }
    
    mem_wb.write_data = write_data;
    mem_wb.write_reg = ex_mem.write_reg;
    mem_wb.reg_write = ex_mem.reg_write;
    mem_wb.pc = ex_mem.pc;
}

void OooProcessor::commitStage() {
    // For now, simply update the regfile using the results at MEM/WB.
    if (mem_wb.reg_write) {
        uint32_t read_data_1, read_data_2;
        regfile.access(0, 0, read_data_1, read_data_2, mem_wb.write_reg, true, mem_wb.write_data);
    }
    regfile.pc = mem_wb.pc;
}

void OooProcessor::advance() {
    // In a cycle-accurate simulation, these stages would run concurrently.
    // For this example, we simulate a single cycle by invoking each stage in order.
    commitStage();
    memoryStage();
    executeStage();
    registerRenamingStage();
    decodeStage();
    fetchStage();
    
    std::cout << "OoO advance complete for cycle. Current PC = 0x" 
              << std::hex << current_pc << std::dec << std::endl;
}
