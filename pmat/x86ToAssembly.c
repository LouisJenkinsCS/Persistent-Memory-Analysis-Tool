#include <capstone/capstone.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

int main(int argc, char **argv) {
    csh handle;
    cs_insn *insn;
    size_t count;
    
    if (cs_open(CS_ARCH_X86, CS_MODE_64, &handle) != CS_ERR_OK) return -1;
    count = cs_disasm(handle,  (uint8_t *) argv[1], strlen(argv[1]), atoll(argv[2]), 0, &insn);
    if (count > 0) {
        for (size_t i = 0; i < count; i++) {
            printf("0x%" PRIx64 ": %s %s\n", insn[i].address, insn[i].mnemonic, insn[i].op_str);
        }

        cs_free(insn, count);
    } else {
        return -1;
    }

    cs_close(&handle);
    return 0;
}
