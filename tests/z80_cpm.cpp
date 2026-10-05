// Runs a CP/M program on the bare Z80 core and prints what it writes to the
// console. Used for the classic instruction exercisers (prelim, zexdoc,
// zexall), which report "ERROR" on a mismatch.
//
//   z80_cpm program.com

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/z80.h"
#include "test_bus.h"

int main(int argc, char* argv[])
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s program.com\n", argv[0]);
        return 2;
    }
    std::ifstream file(argv[1], std::ios::binary);
    if (!file) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    const std::vector<char> image((std::istreambuf_iterator<char>(file)), {});
    if (image.size() > 0xFF00 - 0x100) {
        std::fprintf(stderr, "%s is too large for a CP/M program\n", argv[1]);
        return 2;
    }

    TestBus bus;
    tuxape::Z80<TestBus> cpu(bus);
    std::copy(image.begin(), image.end(), bus.ram + 0x100);
    cpu.pc = 0x100;
    cpu.sp = 0xFF00;

    std::string output;
    const auto start = std::chrono::steady_clock::now();
    for (;;) {
        if (cpu.pc == 0x0000)  // warm boot: the program is done
            break;
        if (cpu.pc == 0x0005) {  // BDOS call
            if (cpu.reg[cpu.C] == 2) {
                output += static_cast<char>(cpu.reg[cpu.E]);
                std::putchar(cpu.reg[cpu.E]);
            } else if (cpu.reg[cpu.C] == 9) {
                for (uint16_t a = cpu.de(); bus.ram[a] != '$'; ++a) {
                    output += static_cast<char>(bus.ram[a]);
                    std::putchar(bus.ram[a]);
                }
            }
            std::fflush(stdout);
            // Return to the caller.
            cpu.pc = static_cast<uint16_t>(bus.ram[cpu.sp] | bus.ram[static_cast<uint16_t>(cpu.sp + 1)] << 8);
            cpu.sp += 2;
            continue;
        }
        cpu.step();
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("\n%llu T-states in %.1f s (%.0f MHz)\n", static_cast<unsigned long long>(bus.tstates),
                seconds, bus.tstates / seconds / 1e6);

    return output.find("ERROR") == std::string::npos ? 0 : 1;
}
