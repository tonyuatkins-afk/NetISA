# HEARO Makefile (Open Watcom V2 wmake)
# Targets:
#   wmake           HEARO.EXE   (16-bit, 286+, large model)
#   wmake xm        HEAROXM.EXE (32-bit, 386+, flat model)
#   wmake tests     all six test executables
#   wmake clean     remove build artifacts
#   wmake all       everything

CC16   = wcc
CC32   = wcc386
LINK   = wlink
ASM    = nasm

# HEARO_NOASM swaps every detect probe for a C fallback that returns plausible
# values. Drop the -dHEARO_NOASM tokens below to compile the real probes in
# src/platform/probes.c (Watcom inline asm + INT 10h/12h/2Fh/33h/67h calls).
# Default leaves the gate ON so the codebase can build clean before the
# probes.c path has been validated on bench iron.
#
# -zc places const data items (including string literals) in the code segment
# instead of DGROUP. With ~70 unlock rules + menu definitions + status output
# format strings, DGROUP overruns 64K without it.
CF16 = -2 -ml -os -wx -zq -zc -i=src -dHEARO_NOASM
CF32 = -3r -mf -ox -wx -zq -i=src -dHEARO_NOASM

SRC_DIR     = src
OBJ_DIR     = build
OBJ32_DIR   = build32

SRCS = &
    $(SRC_DIR)\hearo.c &
    $(SRC_DIR)\detect\detect.c &
    $(SRC_DIR)\detect\cpu.c &
    $(SRC_DIR)\detect\fpu.c &
    $(SRC_DIR)\detect\video.c &
    $(SRC_DIR)\detect\audio.c &
    $(SRC_DIR)\detect\memory.c &
    $(SRC_DIR)\detect\netisa.c &
    $(SRC_DIR)\detect\input.c &
    $(SRC_DIR)\unlock\unlock.c &
    $(SRC_DIR)\unlock\hall.c &
    $(SRC_DIR)\unlock\whisper.c &
    $(SRC_DIR)\ui\screen.c &
    $(SRC_DIR)\ui\boot.c &
    $(SRC_DIR)\ui\menu.c &
    $(SRC_DIR)\ui\browser.c &
    $(SRC_DIR)\ui\nowplay.c &
    $(SRC_DIR)\ui\spectrum.c &
    $(SRC_DIR)\ui\settings.c &
    $(SRC_DIR)\ui\hallview.c &
    $(SRC_DIR)\ui\ui.c &
    $(SRC_DIR)\config\config.c &
    $(SRC_DIR)\config\cmdline.c &
    $(SRC_DIR)\math\cordic.c &
    $(SRC_DIR)\math\bipartite.c &
    $(SRC_DIR)\math\tables.c &
    $(SRC_DIR)\math\quire.c &
    $(SRC_DIR)\stub\netisa_stub.c &
    $(SRC_DIR)\platform\dos.c &
    $(SRC_DIR)\platform\timer.c &
    $(SRC_DIR)\platform\probes.c

# LIB_OBJS = everything except hearo.obj (which has main()). Test programs link
# their own main with LIB_OBJS; HEARO.EXE adds hearo.obj on top.
LIB_OBJS = &
    $(OBJ_DIR)\detect.obj &
    $(OBJ_DIR)\cpu.obj &
    $(OBJ_DIR)\fpu.obj &
    $(OBJ_DIR)\video.obj &
    $(OBJ_DIR)\audio.obj &
    $(OBJ_DIR)\memory.obj &
    $(OBJ_DIR)\netisa.obj &
    $(OBJ_DIR)\input.obj &
    $(OBJ_DIR)\unlock.obj &
    $(OBJ_DIR)\hall.obj &
    $(OBJ_DIR)\whisper.obj &
    $(OBJ_DIR)\screen.obj &
    $(OBJ_DIR)\boot.obj &
    $(OBJ_DIR)\menu.obj &
    $(OBJ_DIR)\browser.obj &
    $(OBJ_DIR)\nowplay.obj &
    $(OBJ_DIR)\spectrum.obj &
    $(OBJ_DIR)\settings.obj &
    $(OBJ_DIR)\hallview.obj &
    $(OBJ_DIR)\ui.obj &
    $(OBJ_DIR)\config.obj &
    $(OBJ_DIR)\cmdline.obj &
    $(OBJ_DIR)\cordic.obj &
    $(OBJ_DIR)\bipartite.obj &
    $(OBJ_DIR)\tables.obj &
    $(OBJ_DIR)\quire.obj &
    $(OBJ_DIR)\netisa_stub.obj &
    $(OBJ_DIR)\dos.obj &
    $(OBJ_DIR)\timer.obj &
    $(OBJ_DIR)\probes.obj

.BEFORE
    @if not exist $(OBJ_DIR) mkdir $(OBJ_DIR)
    @if not exist $(OBJ32_DIR) mkdir $(OBJ32_DIR)

# Default
all: HEARO.EXE tests .SYMBOLIC

HEARO.EXE: $(OBJ_DIR)\hearo.obj $(LIB_OBJS)
    $(LINK) system dos option map=hearo.map name HEARO file { $(OBJ_DIR)\hearo.obj $(LIB_OBJS) }

xm: HEAROXM.EXE .SYMBOLIC

HEAROXM.EXE: $(SRCS)
    @echo Building 32-bit HEAROXM.EXE
    $(CC32) $(CF32) -fo=$(OBJ32_DIR)\ $(SRCS)
    $(LINK) system dos4g option map=hearoxm.map name HEAROXM file { $(OBJ32_DIR)\*.obj }

# Pattern rules: every .c under src is compiled into OBJ_DIR
$(OBJ_DIR)\hearo.obj:        $(SRC_DIR)\hearo.c        ;  $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\detect.obj:       $(SRC_DIR)\detect\detect.c     ;  $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\cpu.obj:          $(SRC_DIR)\detect\cpu.c        ;  $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\fpu.obj:          $(SRC_DIR)\detect\fpu.c        ;  $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\video.obj:        $(SRC_DIR)\detect\video.c      ;  $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\audio.obj:        $(SRC_DIR)\detect\audio.c      ;  $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\memory.obj:       $(SRC_DIR)\detect\memory.c     ;  $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\netisa.obj:       $(SRC_DIR)\detect\netisa.c     ;  $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\input.obj:        $(SRC_DIR)\detect\input.c      ;  $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\unlock.obj:       $(SRC_DIR)\unlock\unlock.c     ;  $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\hall.obj:         $(SRC_DIR)\unlock\hall.c       ;  $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\whisper.obj:      $(SRC_DIR)\unlock\whisper.c    ;  $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\screen.obj:       $(SRC_DIR)\ui\screen.c         ;  $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\boot.obj:         $(SRC_DIR)\ui\boot.c           ;  $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\menu.obj:         $(SRC_DIR)\ui\menu.c           ;  $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\browser.obj:      $(SRC_DIR)\ui\browser.c        ;  $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\nowplay.obj:      $(SRC_DIR)\ui\nowplay.c        ;  $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\spectrum.obj:     $(SRC_DIR)\ui\spectrum.c       ;  $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\settings.obj:     $(SRC_DIR)\ui\settings.c       ;  $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\hallview.obj:     $(SRC_DIR)\ui\hallview.c       ;  $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\ui.obj:           $(SRC_DIR)\ui\ui.c             ;  $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\config.obj:       $(SRC_DIR)\config\config.c     ;  $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\cmdline.obj:      $(SRC_DIR)\config\cmdline.c    ;  $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\cordic.obj:       $(SRC_DIR)\math\cordic.c       ;  $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\bipartite.obj:    $(SRC_DIR)\math\bipartite.c    ;  $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\tables.obj:       $(SRC_DIR)\math\tables.c       ;  $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\quire.obj:        $(SRC_DIR)\math\quire.c        ;  $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\netisa_stub.obj:  $(SRC_DIR)\stub\netisa_stub.c  ;  $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\dos.obj:          $(SRC_DIR)\platform\dos.c      ;  $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\timer.obj:        $(SRC_DIR)\platform\timer.c    ;  $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\probes.obj:       $(SRC_DIR)\platform\probes.c   ;  $(CC16) $(CF16) $< -fo=$@

# Test programs
tests: TESTDET.EXE TESTBOOT.EXE TESTCORD.EXE TESTQUIR.EXE TESTBIP.EXE TESTUI.EXE .SYMBOLIC

TESTDET.EXE: test\testdet.c $(LIB_OBJS)
    $(CC16) $(CF16) test\testdet.c -fo=$(OBJ_DIR)\testdet.obj
    $(LINK) system dos name TESTDET file { $(OBJ_DIR)\testdet.obj $(LIB_OBJS) }

TESTBOOT.EXE: test\testboot.c $(LIB_OBJS)
    $(CC16) $(CF16) test\testboot.c -fo=$(OBJ_DIR)\testboot.obj
    $(LINK) system dos name TESTBOOT file { $(OBJ_DIR)\testboot.obj $(LIB_OBJS) }

TESTCORD.EXE: test\testcord.c $(OBJ_DIR)\cordic.obj
    $(CC16) $(CF16) test\testcord.c -fo=$(OBJ_DIR)\testcord.obj
    $(LINK) system dos name TESTCORD file { $(OBJ_DIR)\testcord.obj $(OBJ_DIR)\cordic.obj }

TESTQUIR.EXE: test\testquir.c $(OBJ_DIR)\quire.obj
    $(CC16) $(CF16) test\testquir.c -fo=$(OBJ_DIR)\testquir.obj
    $(LINK) system dos name TESTQUIR file { $(OBJ_DIR)\testquir.obj $(OBJ_DIR)\quire.obj }

TESTBIP.EXE: test\testbip.c $(OBJ_DIR)\bipartite.obj $(OBJ_DIR)\tables.obj
    $(CC16) $(CF16) test\testbip.c -fo=$(OBJ_DIR)\testbip.obj
    $(LINK) system dos name TESTBIP file { $(OBJ_DIR)\testbip.obj $(OBJ_DIR)\bipartite.obj $(OBJ_DIR)\tables.obj }

TESTUI.EXE: test\testui.c $(LIB_OBJS)
    $(CC16) $(CF16) test\testui.c -fo=$(OBJ_DIR)\testui.obj
    $(LINK) system dos name TESTUI file { $(OBJ_DIR)\testui.obj $(LIB_OBJS) }

clean: .SYMBOLIC
    @if exist $(OBJ_DIR)\*.obj del /q $(OBJ_DIR)\*.obj
    @if exist $(OBJ32_DIR)\*.obj del /q $(OBJ32_DIR)\*.obj
    @if exist *.EXE del /q *.EXE
    @if exist *.exe del /q *.exe
    @if exist *.map del /q *.map
    @if exist *.err del /q *.err
