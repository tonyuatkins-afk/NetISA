# Top-level Makefile for NetISA project
#
# Targets:
#   all       - Build everything
#   dos       - Build all DOS software
#   tsr       - Build NETISA.COM (TSR)
#   launcher  - Build NETISA.EXE (launcher)
#   test      - Build phase0/dos/nisatest.com
#   sim       - Run iverilog testbench
#   clean     - Remove build artifacts

NASM = nasm
IVERILOG = iverilog
VVP = vvp

all: dos test .SYMBOLIC

dos: .SYMBOLIC
	cd dos && wmake -f Makefile all

tsr: .SYMBOLIC
	cd dos && wmake -f Makefile tsr

launcher: .SYMBOLIC
	cd dos && wmake -f Makefile launcher

test: phase0\dos\nisatest.com .SYMBOLIC

phase0\dos\nisatest.com: phase0\dos\nisatest.asm
	$(NASM) -f bin -o phase0\dos\nisatest.com phase0\dos\nisatest.asm

sim: .SYMBOLIC
	cd phase0\cpld && $(IVERILOG) -o netisa_tb netisa.v netisa_tb.v && $(VVP) netisa_tb

clean: .SYMBOLIC
	cd dos && wmake -f Makefile clean
	-del phase0\dos\nisatest.com
	-del phase0\cpld\netisa_tb
	-del phase0\cpld\netisa_tb.vcd
