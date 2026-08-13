extern "C"
{
#include "memory.h"
#include "interrupt_controller.h"
#include "interrupt_timer.h"
#include "z80.h"
}

#include <cassert>
#include <cstdio>
#include <cstring>

static uint8_t no_input(void) { return 0; }
static void no_output(uint8_t) {}
static uint8_t no_sense(void) { return 0; }
static uint8_t no_port_input(uint8_t) { return 0; }
static void no_port_output(uint8_t, uint8_t) {}

static disk_controller_t no_disk = {
    no_output, no_input, no_output, no_input, no_output, no_input
};

static void reset_at(z80_t *cpu, uint16_t pc, uint16_t sp)
{
    memset(memory, 0, 65536);
    z80_reset(cpu, no_input, no_output, no_sense, &no_disk,
              no_port_input, no_port_output);
    cpu->registers.sp = sp;
    z80_examine(cpu, pc);
}

int main(void)
{
    z80_t cpu;

    reset_at(&cpu, 0x0100, 0xf000);
    memory[0x0100] = 0xfb; // EI
    memory[0x0101] = 0x00; // NOP
    z80_cycle(&cpu);
    assert(!z80_interrupt(&cpu, 0xff));
    z80_cycle(&cpu);
    assert(z80_interrupt(&cpu, 0xff));
    assert(cpu.registers.pc == 0x0038);
    assert(cpu.registers.sp == 0xeffe);
    assert(memory[0xeffe] == 0x02 && memory[0xefff] == 0x01);
    assert(!cpu.iff);

    reset_at(&cpu, 0x0200, 0xe000);
    memory[0x0200] = 0xfb; // EI
    memory[0x0201] = 0x00; // NOP
    memory[0x0202] = 0x76; // HALT
    z80_execute_instructions(&cpu, 3);
    assert(cpu.halted);
    assert(z80_interrupt(&cpu, 0xff));
    assert(!cpu.halted && cpu.registers.pc == 0x0038);

    reset_at(&cpu, 0x0500, 0xb000);
    memory[0x0038] = 0xc3; // JP 0600h
    memory[0x0039] = 0x00;
    memory[0x003a] = 0x06;
    memory[0x0500] = 0xfb; // EI
    memory[0x0501] = 0x76; // HALT
    memory[0x0502] = 0x00; // resumed NOP
    memory[0x0600] = 0xf5; // PUSH AF
    memory[0x0601] = 0x3a; // LD A,(0700h)
    memory[0x0602] = 0x00;
    memory[0x0603] = 0x07;
    memory[0x0604] = 0x3c; // INC A
    memory[0x0605] = 0x32; // LD (0700h),A
    memory[0x0606] = 0x00;
    memory[0x0607] = 0x07;
    memory[0x0608] = 0xf1; // POP AF
    memory[0x0609] = 0xfb; // EI
    memory[0x060a] = 0xed; // RETI
    memory[0x060b] = 0x4d;
    z80_execute_instructions(&cpu, 2);
    assert(z80_interrupt(&cpu, 0xff));
    z80_execute_instructions(&cpu, 8);
    assert(memory[0x0700] == 1);
    assert(cpu.registers.pc == 0x0502);
    assert(cpu.registers.sp == 0xb000);

    reset_at(&cpu, 0x0280, 0xe000);
    memory[0x0280] = 0xfb; // EI
    memory[0x0281] = 0x76; // HALT
    z80_execute_instructions(&cpu, 2);
    assert(cpu.halted);
    assert(z80_interrupt(&cpu, 0xff));
    assert(!cpu.halted && cpu.registers.pc == 0x0038);

    reset_at(&cpu, 0x0300, 0xd000);
    memory[0x0300] = 0xed; // IM 2
    memory[0x0301] = 0x5e;
    memory[0x0302] = 0x3e; // LD A,12h
    memory[0x0303] = 0x12;
    memory[0x0304] = 0xed; // LD I,A
    memory[0x0305] = 0x47;
    memory[0x0306] = 0xfb; // EI
    memory[0x0307] = 0x00; // NOP
    memory[0x1234] = 0x78;
    memory[0x1235] = 0x56;
    z80_execute_instructions(&cpu, 4);
    z80_execute_instructions(&cpu, 1);
    assert(cpu.interrupt_mode == 2);
    assert(z80_interrupt(&cpu, 0x34));
    assert(cpu.registers.pc == 0x5678);

    reset_at(&cpu, 0x0400, 0xc000);
    memory[0x0038] = 0xc9; // RET
    memory[0x0400] = 0xfb; // EI
    memory[0x0401] = 0x00; // NOP
    interrupt_controller_init();
    interrupt_timer_init();
    interrupt_timer_output(255);
    z80_execute_instructions(&cpu, 2);
    while (!interrupt_controller_service(&cpu))
    {
    }
    assert(cpu.registers.pc == 0x0038);
    interrupt_timer_output(0);

    interrupt_provider_id_t high_provider;
    interrupt_provider_id_t low_provider;
    const interrupt_provider_config_t high_config = {0xcf, NULL, NULL};
    const interrupt_provider_config_t low_config = {0xff, NULL, NULL};
    interrupt_controller_init();
    assert(interrupt_controller_register(&high_config, &high_provider));
    assert(interrupt_controller_register(&low_config, &low_provider));
    interrupt_controller_raise(low_provider);
    interrupt_controller_raise(high_provider);

    reset_at(&cpu, 0x0800, 0xa000);
    memory[0x0800] = 0xfb; // EI
    memory[0x0801] = 0x00; // NOP
    z80_execute_instructions(&cpu, 2);
    assert(interrupt_controller_service(&cpu));
    assert(cpu.registers.pc == 0x0008); // high-priority provider's RST 1

    reset_at(&cpu, 0x0900, 0x9000);
    memory[0x0900] = 0xfb; // EI
    memory[0x0901] = 0x00; // NOP
    z80_execute_instructions(&cpu, 2);
    assert(interrupt_controller_service(&cpu));
    assert(cpu.registers.pc == 0x0038); // lower-priority request remained pending

    puts("x80 interrupt tests passed");
    return 0;
}