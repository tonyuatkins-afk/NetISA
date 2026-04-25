# CLAUDE Makefile (Open Watcom V2 wmake)
# Targets:
#   wmake          CLAUDE.EXE
#   wmake clean    remove build artifacts

CC   = wcc
LINK = wlink

CFLAGS = -0 -fpi -ms -s -ox -wx -zq -bt=dos -i=src -i=../lib

SRC_DIR    = src
LIB_DIR    = ..\lib
OBJ_DIR    = build

OBJS = &
    $(OBJ_DIR)\main.obj &
    $(OBJ_DIR)\claude.obj &
    $(OBJ_DIR)\chat.obj &
    $(OBJ_DIR)\compose.obj &
    $(OBJ_DIR)\agent.obj &
    $(OBJ_DIR)\splash.obj &
    $(OBJ_DIR)\stub_claude.obj

LIB_OBJS = &
    $(OBJ_DIR)\screen.obj

.BEFORE
    @if not exist $(OBJ_DIR) mkdir $(OBJ_DIR)

all: CLAUDE.EXE .SYMBOLIC

CLAUDE.EXE: $(OBJS) $(LIB_OBJS)
    $(LINK) system dos name CLAUDE option map=claude.map &
        file { $(OBJS) $(LIB_OBJS) }

$(OBJ_DIR)\main.obj:         $(SRC_DIR)\main.c         ; $(CC) $(CFLAGS) $< -fo=$@
$(OBJ_DIR)\claude.obj:       $(SRC_DIR)\claude.c       ; $(CC) $(CFLAGS) $< -fo=$@
$(OBJ_DIR)\chat.obj:         $(SRC_DIR)\chat.c         ; $(CC) $(CFLAGS) $< -fo=$@
$(OBJ_DIR)\compose.obj:      $(SRC_DIR)\compose.c      ; $(CC) $(CFLAGS) $< -fo=$@
$(OBJ_DIR)\agent.obj:        $(SRC_DIR)\agent.c        ; $(CC) $(CFLAGS) $< -fo=$@
$(OBJ_DIR)\splash.obj:       $(SRC_DIR)\splash.c       ; $(CC) $(CFLAGS) $< -fo=$@
$(OBJ_DIR)\stub_claude.obj:  $(SRC_DIR)\stub_claude.c  ; $(CC) $(CFLAGS) $< -fo=$@

$(OBJ_DIR)\screen.obj:       $(LIB_DIR)\screen.c       ; $(CC) $(CFLAGS) $< -fo=$@

clean: .SYMBOLIC
    @if exist $(OBJ_DIR)\*.obj del /q $(OBJ_DIR)\*.obj
    @if exist *.EXE del /q *.EXE
    @if exist *.exe del /q *.exe
    @if exist *.map del /q *.map
    @if exist *.err del /q *.err
