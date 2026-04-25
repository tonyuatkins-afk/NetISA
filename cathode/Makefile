# CATHODE Makefile (Open Watcom V2 wmake)
# Targets:
#   wmake          CATHODE.EXE (16-bit, 8088+, small model, stub backend)
#   wmake hw       CATHODE_HW.EXE (real INT 63h backend, requires NetISA card)
#   wmake clean    remove build artifacts

CC   = wcc
LINK = wlink

# CATHODE keeps the 8088 floor: -0 is required so the binary runs on
# XT-class machines. -fpi makes calls into the FP emulator when host has
# no FPU. -ms small model. -s no stack checks. -ox optimize. -bt=dos.
CFLAGS = -0 -fpi -ms -s -ox -wx -zq -bt=dos -i=src -i=../lib

SRC_DIR    = src
LIB_DIR    = ..\lib
OBJ_DIR    = build

OBJS = &
    $(OBJ_DIR)\main.obj &
    $(OBJ_DIR)\browser.obj &
    $(OBJ_DIR)\render.obj &
    $(OBJ_DIR)\input.obj &
    $(OBJ_DIR)\page.obj &
    $(OBJ_DIR)\urlbar.obj &
    $(OBJ_DIR)\stub_pages.obj &
    $(OBJ_DIR)\utf8.obj &
    $(OBJ_DIR)\url.obj &
    $(OBJ_DIR)\htmltok.obj &
    $(OBJ_DIR)\htmlout.obj &
    $(OBJ_DIR)\fetch.obj &
    $(OBJ_DIR)\search.obj &
    $(OBJ_DIR)\bookmark.obj

LIB_OBJS = &
    $(OBJ_DIR)\screen.obj &
    $(OBJ_DIR)\netisa_stub.obj

LIB_HW_OBJS = &
    $(OBJ_DIR)\screen.obj &
    $(OBJ_DIR)\netisa.obj

.BEFORE
    @if not exist $(OBJ_DIR) mkdir $(OBJ_DIR)

all: CATHODE.EXE .SYMBOLIC

CATHODE.EXE: $(OBJS) $(LIB_OBJS)
    $(LINK) system dos name CATHODE option map=cathode.map &
        file { $(OBJS) $(LIB_OBJS) }

hw: CATHODE_HW.EXE .SYMBOLIC

CATHODE_HW.EXE: $(OBJS) $(LIB_HW_OBJS)
    $(LINK) system dos name CATHODE_HW option map=cathode_hw.map &
        file { $(OBJS) $(LIB_HW_OBJS) }

# Pattern rules
$(OBJ_DIR)\main.obj:        $(SRC_DIR)\main.c        ; $(CC) $(CFLAGS) $< -fo=$@
$(OBJ_DIR)\browser.obj:     $(SRC_DIR)\browser.c     ; $(CC) $(CFLAGS) $< -fo=$@
$(OBJ_DIR)\render.obj:      $(SRC_DIR)\render.c      ; $(CC) $(CFLAGS) $< -fo=$@
$(OBJ_DIR)\input.obj:       $(SRC_DIR)\input.c       ; $(CC) $(CFLAGS) $< -fo=$@
$(OBJ_DIR)\page.obj:        $(SRC_DIR)\page.c        ; $(CC) $(CFLAGS) $< -fo=$@
$(OBJ_DIR)\urlbar.obj:      $(SRC_DIR)\urlbar.c      ; $(CC) $(CFLAGS) $< -fo=$@
$(OBJ_DIR)\stub_pages.obj:  $(SRC_DIR)\stub_pages.c  ; $(CC) $(CFLAGS) $< -fo=$@
$(OBJ_DIR)\utf8.obj:        $(SRC_DIR)\utf8.c        ; $(CC) $(CFLAGS) $< -fo=$@
$(OBJ_DIR)\url.obj:         $(SRC_DIR)\url.c         ; $(CC) $(CFLAGS) $< -fo=$@
$(OBJ_DIR)\htmltok.obj:     $(SRC_DIR)\htmltok.c     ; $(CC) $(CFLAGS) $< -fo=$@
$(OBJ_DIR)\htmlout.obj:     $(SRC_DIR)\htmlout.c     ; $(CC) $(CFLAGS) $< -fo=$@
$(OBJ_DIR)\fetch.obj:       $(SRC_DIR)\fetch.c       ; $(CC) $(CFLAGS) $< -fo=$@
$(OBJ_DIR)\search.obj:      $(SRC_DIR)\search.c      ; $(CC) $(CFLAGS) $< -fo=$@
$(OBJ_DIR)\bookmark.obj:    $(SRC_DIR)\bookmark.c    ; $(CC) $(CFLAGS) $< -fo=$@

# Shared library objects from ../lib
$(OBJ_DIR)\screen.obj:       $(LIB_DIR)\screen.c       ; $(CC) $(CFLAGS) $< -fo=$@
$(OBJ_DIR)\netisa_stub.obj:  $(LIB_DIR)\netisa_stub.c  ; $(CC) $(CFLAGS) $< -fo=$@
$(OBJ_DIR)\netisa.obj:       $(LIB_DIR)\netisa.c       ; $(CC) $(CFLAGS) $< -fo=$@

clean: .SYMBOLIC
    @if exist $(OBJ_DIR)\*.obj del /q $(OBJ_DIR)\*.obj
    @if exist *.EXE del /q *.EXE
    @if exist *.exe del /q *.exe
    @if exist *.map del /q *.map
    @if exist *.err del /q *.err
