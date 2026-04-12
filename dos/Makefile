# dos/Makefile - Build all DOS software for NetISA
#
# Requires: OpenWatcom 2.0 (C:\WATCOM), NASM
# Usage:    wmake -f Makefile [target]
#
# Targets:
#   all       - Build everything
#   tsr       - Build NETISA.COM (TSR)
#   launcher  - Build NETISA.EXE (launcher with stubs)
#   launcher_hw - Build NETISA.EXE (launcher with real INT 63h)
#   clean     - Remove build artifacts

# Tools
CC = wcc
LINKER = wlink
LIB = wlib
ASM = nasm

# Compiler flags: 8088 target, small model, max optimization
CFLAGS = -0 -fpi -ms -s -ox -w4 -zq -bt=dos -i=lib

# Output directory
!ifdef __LOADDLL__
!  loaddll wcc  wccd
!  loaddll wlink wlinkd
!  loaddll wlib wlibd
!endif

# Object files
LIB_SCREEN = lib\screen.obj
LIB_STUB   = lib\netisa_stub.obj
LIB_REAL   = lib\netisa.obj

LAUNCHER_OBJS = launcher\main.obj launcher\menu.obj launcher\wifi.obj launcher\status.obj

all: tsr launcher .SYMBOLIC

tsr: netisa.com .SYMBOLIC

launcher: netisa.exe .SYMBOLIC

launcher_hw: netisa_hw.exe .SYMBOLIC

# TSR: NASM -> .COM
netisa.com: tsr\netisa_tsr.asm
	$(ASM) -f bin -o netisa.com tsr\netisa_tsr.asm

# Launcher (stub version for DOSBox-X testing)
netisa.exe: $(LAUNCHER_OBJS) $(LIB_SCREEN) $(LIB_STUB)
	$(LINKER) system dos name netisa.exe &
		file launcher\main.obj &
		file launcher\menu.obj &
		file launcher\wifi.obj &
		file launcher\status.obj &
		file lib\screen.obj &
		file lib\netisa_stub.obj &
		option quiet

# Launcher (real hardware version)
netisa_hw.exe: $(LAUNCHER_OBJS) $(LIB_SCREEN) $(LIB_REAL)
	$(LINKER) system dos name netisa_hw.exe &
		file launcher\main.obj &
		file launcher\menu.obj &
		file launcher\wifi.obj &
		file launcher\status.obj &
		file lib\screen.obj &
		file lib\netisa.obj &
		option quiet

# Pattern rules for C files
.c.obj:
	$(CC) $(CFLAGS) -fo=$^@ $<

# Explicit rules for each C source
lib\screen.obj: lib\screen.c lib\screen.h
	$(CC) $(CFLAGS) -fo=lib\screen.obj lib\screen.c

lib\netisa_stub.obj: lib\netisa_stub.c lib\netisa.h
	$(CC) $(CFLAGS) -fo=lib\netisa_stub.obj lib\netisa_stub.c

lib\netisa.obj: lib\netisa.c lib\netisa.h
	$(CC) $(CFLAGS) -fo=lib\netisa.obj lib\netisa.c

launcher\main.obj: launcher\main.c lib\screen.h lib\netisa.h launcher\menu.h
	$(CC) $(CFLAGS) -fo=launcher\main.obj launcher\main.c

launcher\menu.obj: launcher\menu.c lib\screen.h lib\netisa.h launcher\menu.h
	$(CC) $(CFLAGS) -fo=launcher\menu.obj launcher\menu.c

launcher\wifi.obj: launcher\wifi.c lib\screen.h lib\netisa.h
	$(CC) $(CFLAGS) -fo=launcher\wifi.obj launcher\wifi.c

launcher\status.obj: launcher\status.c lib\screen.h lib\netisa.h
	$(CC) $(CFLAGS) -fo=launcher\status.obj launcher\status.c

clean: .SYMBOLIC
	-del lib\*.obj
	-del launcher\*.obj
	-del netisa.com
	-del netisa.exe
	-del netisa_hw.exe
	-del *.err
