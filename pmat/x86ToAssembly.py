import os
import argparse

package = 'capstone'

try:
    __import__(package)
except ImportError:
    os.system("pip install --user " + package)

from capstone import *

parser = argparse.ArgumentParser(description='Convert x86_64 Machine Code to x86_64 Assembly')
parser.add_argument('code', type=str, help='String containing bytes to be dissected')
parser.add_argument('addr', type=str, help='Integer containing starting address of the instruction')
args = parser.parse_args()

md = Cs(CS_ARCH_X86, CS_MODE_64)
for i in md.disasm(bytes.fromhex(args.code), int(args.addr, 16)):
    print("0x%x:\t%s\t%s" %(i.address, i.mnemonic, i.op_str))

