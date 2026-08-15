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
    uint16_t event_address;
    uint8_t event_data;
    uint8_t event_status;
    assert(z80_take_display_event(&cpu, &event_address, &event_data,
                                  &event_status));
    assert(event_address == 0x0038);
    assert(event_data == memory[0x0038]);
    assert(event_status == 0x05); // interrupt acknowledge with stack access
    assert(!z80_take_display_event(&cpu, &event_address, &event_data,
                                   &event_status));

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
    assert((cpu.cpuStatus & 0x04) != 0); // RETI popped the interrupt return address

    reset_at(&cpu, 0x0a00, 0xb000);
    memory[0x0038] = 0xc3; // JP 0a20h
    memory[0x0039] = 0x20;
    memory[0x003a] = 0x0a;
    memory[0x0a00] = 0xfb; // EI
    memory[0x0a01] = 0x76; // HALT
    memory[0x0a02] = 0xf3; // DI
    memory[0x0a03] = 0x3a; // LD A,(0b00h)
    memory[0x0a04] = 0x00;
    memory[0x0a05] = 0x0b;
    memory[0x0a06] = 0xfe; // CP 2
    memory[0x0a07] = 0x02;
    memory[0x0a08] = 0x20; // JR NZ,0a00h
    memory[0x0a09] = 0xf6;
    memory[0x0a0a] = 0x76; // final HALT
    memory[0x0a20] = 0xf5; // PUSH AF
    memory[0x0a21] = 0x3a; // LD A,(0b00h)
    memory[0x0a22] = 0x00;
    memory[0x0a23] = 0x0b;
    memory[0x0a24] = 0x3c; // INC A
    memory[0x0a25] = 0x32; // LD (0b00h),A
    memory[0x0a26] = 0x00;
    memory[0x0a27] = 0x0b;
    memory[0x0a28] = 0xf1; // POP AF
    memory[0x0a29] = 0xfb; // EI
    memory[0x0a2a] = 0xed; // RETI
    memory[0x0a2b] = 0x4d;
    z80_execute_instructions(&cpu, 20);
    assert(cpu.halted && cpu.registers.pc == 0x0a02);
    assert(z80_interrupt(&cpu, 0xff));
    z80_execute_instructions(&cpu, 40);
    assert(memory[0x0b00] == 1);
    assert(cpu.halted && cpu.registers.pc == 0x0a02);
    assert(z80_interrupt(&cpu, 0xff));
    z80_execute_instructions(&cpu, 40);
    assert(memory[0x0b00] == 2);
    assert(cpu.halted && cpu.registers.pc == 0x0a0b);

    reset_at(&cpu, 0x0700, 0xa000);
    memory[0x0700] = 0xaf; // XRA A sets Z
    memory[0x0701] = 0xc0; // RET NZ, not taken
    z80_execute_instructions(&cpu, 2);
    assert(cpu.registers.pc == 0x0702);
    assert(cpu.registers.sp == 0xa000);
    assert((cpu.cpuStatus & 0x04) == 0);

    reset_at(&cpu, 0x0710, 0xa000);
    memory[0x0710] = 0xaf; // XRA A sets Z
    memory[0x0711] = 0xc4; // CALL NZ,1234h, not taken
    memory[0x0712] = 0x34;
    memory[0x0713] = 0x12;
    z80_execute_instructions(&cpu, 2);
    assert(cpu.registers.pc == 0x0714);
    assert(cpu.registers.sp == 0xa000);
    assert((cpu.cpuStatus & 0x04) == 0);

    reset_at(&cpu, 0x0720, 0xa000);
    memory[0x0720] = 0xcd; // CALL 1234h
    memory[0x0721] = 0x34;
    memory[0x0722] = 0x12;
    z80_execute_instructions(&cpu, 1);
    assert(cpu.registers.pc == 0x1234);
    assert(cpu.registers.sp == 0x9ffe);
    assert((cpu.cpuStatus & 0x04) != 0);

    reset_at(&cpu, 0x0740, 0xa000);
    memory[0x0740] = 0xdd; // LD IX,1234h
    memory[0x0741] = 0x21;
    memory[0x0742] = 0x34;
    memory[0x0743] = 0x12;
    memory[0x0744] = 0xdd; // PUSH IX
    memory[0x0745] = 0xe5;
    memory[0x0746] = 0xdd; // POP IX
    memory[0x0747] = 0xe1;
    z80_execute_instructions(&cpu, 3);
    assert(cpu.registers.pc == 0x0748);
    assert(cpu.registers.sp == 0xa000);
    assert(memory[0x9ffe] == 0x34 && memory[0x9fff] == 0x12);
    assert((cpu.cpuStatus & 0x04) != 0);

    reset_at(&cpu, 0x0760, 0xa000);
    memory[0x0760] = 0xed; // LD (2000h),SP
    memory[0x0761] = 0x73;
    memory[0x0762] = 0x00;
    memory[0x0763] = 0x20;
    memory[0x0764] = 0x31; // LD SP,9000h
    memory[0x0765] = 0x00;
    memory[0x0766] = 0x90;
    memory[0x0767] = 0xed; // LD SP,(2000h)
    memory[0x0768] = 0x7b;
    memory[0x0769] = 0x00;
    memory[0x076a] = 0x20;
    z80_execute_instructions(&cpu, 3);
    assert(memory[0x2000] == 0x00 && memory[0x2001] == 0xa0);
    assert(cpu.registers.sp == 0xa000);

    reset_at(&cpu, 0x0780, 0xa000);
    memory[0x0780] = 0xdd; // LD IX,8123h
    memory[0x0781] = 0x21;
    memory[0x0782] = 0x23;
    memory[0x0783] = 0x81;
    memory[0x0784] = 0xdd; // LD SP,IX
    memory[0x0785] = 0xf9;
    z80_execute_instructions(&cpu, 2);
    assert(cpu.registers.pc == 0x0786);
    assert(cpu.registers.sp == 0x8123);

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