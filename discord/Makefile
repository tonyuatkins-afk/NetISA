# DISCORD Makefile (Open Watcom V2 wmake)
# Targets:
#   wmake          DISCORD.EXE  (16-bit, 8088+, small model, stub backend)
#   wmake clean    remove build artifacts

CC   = wcc
LINK = wlink

CFLAGS = -0 -fpi -ms -s -ox -wx -zq -bt=dos -i=src -i=../lib

SRC_DIR    = src
LIB_DIR    = ..\lib
OBJ_DIR    = build

OBJS = &
    $(OBJ_DIR)\main.obj &
    $(OBJ_DIR)\discord.obj &
    $(OBJ_DIR)\render_dc.obj &
    $(OBJ_DIR)\input_dc.obj &
    $(OBJ_DIR)\stub_discord.obj &
    $(OBJ_DIR)\audio_dc.obj &
    $(OBJ_DIR)\config_dc.obj &
    $(OBJ_DIR)\search_dc.obj

LIB_OBJS = &
    $(OBJ_DIR)\screen.obj

.BEFORE
    @if not exist $(OBJ_DIR) mkdir $(OBJ_DIR)

all: DISCORD.EXE .SYMBOLIC

DISCORD.EXE: $(OBJS) $(LIB_OBJS)
    $(LINK) system dos name DISCORD option map=discord.map &
        file { $(OBJS) $(LIB_OBJS) }

$(OBJ_DIR)\main.obj:          $(SRC_DIR)\main.c          ; $(CC) $(CFLAGS) $< -fo=$@
$(OBJ_DIR)\discord.obj:       $(SRC_DIR)\discord.c       ; $(CC) $(CFLAGS) $< -fo=$@
$(OBJ_DIR)\render_dc.obj:     $(SRC_DIR)\render_dc.c     ; $(CC) $(CFLAGS) $< -fo=$@
$(OBJ_DIR)\input_dc.obj:      $(SRC_DIR)\input_dc.c      ; $(CC) $(CFLAGS) $< -fo=$@
$(OBJ_DIR)\stub_discord.obj:  $(SRC_DIR)\stub_discord.c  ; $(CC) $(CFLAGS) $< -fo=$@
$(OBJ_DIR)\audio_dc.obj:      $(SRC_DIR)\audio_dc.c      ; $(CC) $(CFLAGS) $< -fo=$@
$(OBJ_DIR)\config_dc.obj:     $(SRC_DIR)\config_dc.c     ; $(CC) $(CFLAGS) $< -fo=$@
$(OBJ_DIR)\search_dc.obj:     $(SRC_DIR)\search_dc.c     ; $(CC) $(CFLAGS) $< -fo=$@

$(OBJ_DIR)\screen.obj:        $(LIB_DIR)\screen.c        ; $(CC) $(CFLAGS) $< -fo=$@

clean: .SYMBOLIC
    @if exist $(OBJ_DIR)\*.obj del /q $(OBJ_DIR)\*.obj
    @if exist *.EXE del /q *.EXE
    @if exist *.exe del /q *.exe
    @if exist *.map del /q *.map
    @if exist *.err del /q *.err
