# CHIME Makefile (Open Watcom V2 wmake)
# Targets:
#   wmake          CHIME.EXE   (16-bit, 286+, large model)
#   wmake xm       CHIMEXM.EXE (32-bit, 386+, flat model)
#   wmake tests    test executable
#   wmake clean    remove build artifacts

CC16   = wcc
CC32   = wcc386
LINK   = wlink

# Drop -dHEARO_NOASM for the real-iron build path. Default leaves it on so the
# build is self-contained until probes.c paths are validated on the bench.
CF16 = -2 -ml -os -wx -zq -i=src -dHEARO_NOASM
CF32 = -3r -mf -ox -wx -zq -i=src -dHEARO_NOASM

SRC_DIR   = src
OBJ_DIR   = build
OBJ32_DIR = build32

# LIB_OBJS = everything except chime.obj (which has main()). Tests link their
# own main with LIB_OBJS; CHIME.EXE adds chime.obj on top.
LIB_OBJS = &
    $(OBJ_DIR)\cmdline.obj &
    $(OBJ_DIR)\config.obj &
    $(OBJ_DIR)\netisa_api.obj &
    $(OBJ_DIR)\timesrc.obj &
    $(OBJ_DIR)\dos_clock.obj &
    $(OBJ_DIR)\ui_status.obj

.BEFORE
    @if not exist $(OBJ_DIR) mkdir $(OBJ_DIR)
    @if not exist $(OBJ32_DIR) mkdir $(OBJ32_DIR)

all: CHIME.EXE tests .SYMBOLIC

CHIME.EXE: $(OBJ_DIR)\chime.obj $(LIB_OBJS)
    $(LINK) system dos option map=chime.map name CHIME file { $(OBJ_DIR)\chime.obj $(LIB_OBJS) }

xm: CHIMEXM.EXE .SYMBOLIC

CHIMEXM.EXE: $(SRC_DIR)\*.c
    $(CC32) $(CF32) -fo=$(OBJ32_DIR)\ $(SRC_DIR)\*.c
    $(LINK) system dos4g option map=chimexm.map name CHIMEXM file { $(OBJ32_DIR)\*.obj }

$(OBJ_DIR)\chime.obj:       $(SRC_DIR)\chime.c       ; $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\cmdline.obj:     $(SRC_DIR)\cmdline.c     ; $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\config.obj:      $(SRC_DIR)\config.c      ; $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\netisa_api.obj:  $(SRC_DIR)\netisa_api.c  ; $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\timesrc.obj:     $(SRC_DIR)\timesrc.c     ; $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\dos_clock.obj:   $(SRC_DIR)\dos_clock.c   ; $(CC16) $(CF16) $< -fo=$@
$(OBJ_DIR)\ui_status.obj:   $(SRC_DIR)\ui_status.c   ; $(CC16) $(CF16) $< -fo=$@

tests: TESTTIME.EXE .SYMBOLIC

TESTTIME.EXE: test\testtime.c $(LIB_OBJS)
    $(CC16) $(CF16) test\testtime.c -fo=$(OBJ_DIR)\testtime.obj
    $(LINK) system dos name TESTTIME file { $(OBJ_DIR)\testtime.obj $(LIB_OBJS) }

clean: .SYMBOLIC
    @if exist $(OBJ_DIR)\*.obj del /q $(OBJ_DIR)\*.obj
    @if exist $(OBJ32_DIR)\*.obj del /q $(OBJ32_DIR)\*.obj
    @if exist *.EXE del /q *.EXE
    @if exist *.exe del /q *.exe
    @if exist *.map del /q *.map
    @if exist *.err del /q *.err
