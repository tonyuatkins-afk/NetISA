# dos/Makefile - Build the NetISA TSR + launcher.
#
# CATHODE, DISCORD, and CLAUDE have all migrated to top-level suite-app
# directories. Run wmake from ../cathode/, ../discord/, ../claude/.
# This Makefile only covers the TSR and the launcher.
#
# Shared screen + netisa library lives at ../lib/. Object files land in
# build/ to keep generated artifacts separate from source.
#
# Requires: OpenWatcom 2.0 (C:\WATCOM), NASM
# Usage:    wmake -f Makefile [target]
#
# Targets:
#   all          - Build everything (tsr + launcher)
#   tsr          - Build NETISA.COM (TSR)
#   launcher     - Build NETISA.EXE (launcher with stubs)
#   launcher_hw  - Build NETISA_HW.EXE (launcher with real INT 63h)
#   clean        - Remove build artifacts

# Tools (full paths for build reliability)
CC = C:\WATCOM\binnt64\wcc
LINKER = C:\WATCOM\binnt64\wlink
LIB = C:\WATCOM\binnt64\wlib
ASM = nasm

SRC_LIB = ..\lib
OBJ_DIR = build

# Compiler flags: 8088 target, small model, max optimization.
# Include path picks up shared headers from ../lib and the launcher tree.
CFLAGS = -0 -fpi -ms -s -ox -w4 -zq -bt=dos -i=$(SRC_LIB) -i=launcher

!ifdef __LOADDLL__
!  loaddll wcc  wccd
!  loaddll wlink wlinkd
!  loaddll wlib wlibd
!endif

# Object files
LIB_SCREEN = $(OBJ_DIR)\screen.obj
LIB_STUB   = $(OBJ_DIR)\netisa_stub.obj
LIB_REAL   = $(OBJ_DIR)\netisa.obj

LAUNCHER_OBJS = $(OBJ_DIR)\main.obj $(OBJ_DIR)\menu.obj $(OBJ_DIR)\wifi.obj $(OBJ_DIR)\status.obj

.BEFORE
    @if not exist $(OBJ_DIR) mkdir $(OBJ_DIR)

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
		file $(OBJ_DIR)\main.obj &
		file $(OBJ_DIR)\menu.obj &
		file $(OBJ_DIR)\wifi.obj &
		file $(OBJ_DIR)\status.obj &
		file $(OBJ_DIR)\screen.obj &
		file $(OBJ_DIR)\netisa_stub.obj &
		option quiet

# Launcher (real hardware version)
netisa_hw.exe: $(LAUNCHER_OBJS) $(LIB_SCREEN) $(LIB_REAL)
	$(LINKER) system dos name netisa_hw.exe &
		file $(OBJ_DIR)\main.obj &
		file $(OBJ_DIR)\menu.obj &
		file $(OBJ_DIR)\wifi.obj &
		file $(OBJ_DIR)\status.obj &
		file $(OBJ_DIR)\screen.obj &
		file $(OBJ_DIR)\netisa.obj &
		option quiet

# Shared library objects compiled from ../lib
$(OBJ_DIR)\screen.obj: $(SRC_LIB)\screen.c $(SRC_LIB)\screen.h
	$(CC) $(CFLAGS) -fo=$@ $(SRC_LIB)\screen.c

$(OBJ_DIR)\netisa_stub.obj: $(SRC_LIB)\netisa_stub.c $(SRC_LIB)\netisa.h
	$(CC) $(CFLAGS) -fo=$@ $(SRC_LIB)\netisa_stub.c

$(OBJ_DIR)\netisa.obj: $(SRC_LIB)\netisa.c $(SRC_LIB)\netisa.h
	$(CC) $(CFLAGS) -fo=$@ $(SRC_LIB)\netisa.c

# Launcher source files
$(OBJ_DIR)\main.obj: launcher\main.c $(SRC_LIB)\screen.h $(SRC_LIB)\netisa.h launcher\menu.h
	$(CC) $(CFLAGS) -fo=$@ launcher\main.c

$(OBJ_DIR)\menu.obj: launcher\menu.c $(SRC_LIB)\screen.h $(SRC_LIB)\netisa.h launcher\menu.h
	$(CC) $(CFLAGS) -fo=$@ launcher\menu.c

$(OBJ_DIR)\wifi.obj: launcher\wifi.c $(SRC_LIB)\screen.h $(SRC_LIB)\netisa.h
	$(CC) $(CFLAGS) -fo=$@ launcher\wifi.c

$(OBJ_DIR)\status.obj: launcher\status.c $(SRC_LIB)\screen.h $(SRC_LIB)\netisa.h
	$(CC) $(CFLAGS) -fo=$@ launcher\status.c

clean: .SYMBOLIC
	-del $(OBJ_DIR)\*.obj
	-del netisa.com
	-del netisa.exe
	-del netisa_hw.exe
	-del *.err
