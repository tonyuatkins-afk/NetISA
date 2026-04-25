# dos/Makefile - Build the NetISA TSR + launcher + remaining DOS apps.
#
# CATHODE has migrated to ../cathode/ (suite-app pattern). This Makefile
# now only covers tsr, launcher, discord, and claude. Run wmake from
# ../cathode/ to build the browser.
#
# Requires: OpenWatcom 2.0 (C:\WATCOM), NASM
# Usage:    wmake -f Makefile [target]
#
# Targets:
#   all       - Build everything (tsr + launcher + discord + claude)
#   tsr       - Build NETISA.COM (TSR)
#   launcher  - Build NETISA.EXE (launcher with stubs)
#   launcher_hw - Build NETISA_HW.EXE (launcher with real INT 63h)
#   discord   - Build DISCORD.EXE
#   claude    - Build CLAUDE.EXE
#   clean     - Remove build artifacts

# Tools (full paths for build reliability)
CC = C:\WATCOM\binnt64\wcc
LINKER = C:\WATCOM\binnt64\wlink
LIB = C:\WATCOM\binnt64\wlib
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

DISCORD_OBJS = discord\main.obj discord\discord.obj discord\render_dc.obj &
	discord\input_dc.obj discord\stub_discord.obj discord\audio_dc.obj &
	discord\config_dc.obj discord\search_dc.obj

DISCORD_CFLAGS = -0 -fpi -ms -s -ox -w4 -zq -bt=dos -i=lib -i=discord

CLAUDE_OBJS = claude\main.obj claude\claude.obj claude\chat.obj &
	claude\compose.obj claude\agent.obj claude\splash.obj claude\stub_claude.obj

CLAUDE_CFLAGS = -0 -fpi -ms -s -ox -w4 -zq -bt=dos -i=lib -i=claude

all: tsr launcher discord claude .SYMBOLIC

tsr: netisa.com .SYMBOLIC

launcher: netisa.exe .SYMBOLIC

discord: discord.exe .SYMBOLIC

claude: claude.exe .SYMBOLIC

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

# CATHODE has migrated to ../cathode/. Run wmake from there.

# Discord client
discord.exe: $(DISCORD_OBJS) $(LIB_SCREEN)
	$(LINKER) system dos name discord.exe &
		file discord\main.obj &
		file discord\discord.obj &
		file discord\render_dc.obj &
		file discord\input_dc.obj &
		file discord\stub_discord.obj &
		file discord\audio_dc.obj &
		file discord\config_dc.obj &
		file discord\search_dc.obj &
		file lib\screen.obj &
		option quiet

discord\main.obj: discord\main.c discord\discord.h lib\screen.h
	$(CC) $(DISCORD_CFLAGS) -fo=discord\main.obj discord\main.c

discord\discord.obj: discord\discord.c discord\discord.h lib\screen.h
	$(CC) $(DISCORD_CFLAGS) -fo=discord\discord.obj discord\discord.c

discord\render_dc.obj: discord\render_dc.c discord\discord.h lib\screen.h
	$(CC) $(DISCORD_CFLAGS) -fo=discord\render_dc.obj discord\render_dc.c

discord\input_dc.obj: discord\input_dc.c discord\discord.h lib\screen.h
	$(CC) $(DISCORD_CFLAGS) -fo=discord\input_dc.obj discord\input_dc.c

discord\stub_discord.obj: discord\stub_discord.c discord\discord.h lib\screen.h
	$(CC) $(DISCORD_CFLAGS) -fo=discord\stub_discord.obj discord\stub_discord.c

discord\audio_dc.obj: discord\audio_dc.c discord\discord.h
	$(CC) $(DISCORD_CFLAGS) -fo=discord\audio_dc.obj discord\audio_dc.c

discord\config_dc.obj: discord\config_dc.c discord\discord.h
	$(CC) $(DISCORD_CFLAGS) -fo=discord\config_dc.obj discord\config_dc.c

discord\search_dc.obj: discord\search_dc.c discord\discord.h
	$(CC) $(DISCORD_CFLAGS) -fo=discord\search_dc.obj discord\search_dc.c

# Claude client
claude.exe: $(CLAUDE_OBJS) $(LIB_SCREEN)
	$(LINKER) system dos name claude.exe &
		file claude\main.obj &
		file claude\claude.obj &
		file claude\chat.obj &
		file claude\compose.obj &
		file claude\agent.obj &
		file claude\splash.obj &
		file claude\stub_claude.obj &
		file lib\screen.obj &
		option quiet

claude\main.obj: claude\main.c claude\claude.h lib\screen.h
	$(CC) $(CLAUDE_CFLAGS) -fo=claude\main.obj claude\main.c

claude\claude.obj: claude\claude.c claude\claude.h lib\screen.h
	$(CC) $(CLAUDE_CFLAGS) -fo=claude\claude.obj claude\claude.c

claude\chat.obj: claude\chat.c claude\claude.h lib\screen.h
	$(CC) $(CLAUDE_CFLAGS) -fo=claude\chat.obj claude\chat.c

claude\compose.obj: claude\compose.c claude\claude.h lib\screen.h
	$(CC) $(CLAUDE_CFLAGS) -fo=claude\compose.obj claude\compose.c

claude\agent.obj: claude\agent.c claude\claude.h lib\screen.h
	$(CC) $(CLAUDE_CFLAGS) -fo=claude\agent.obj claude\agent.c

claude\splash.obj: claude\splash.c claude\claude.h lib\screen.h
	$(CC) $(CLAUDE_CFLAGS) -fo=claude\splash.obj claude\splash.c

claude\stub_claude.obj: claude\stub_claude.c claude\claude.h lib\screen.h
	$(CC) $(CLAUDE_CFLAGS) -fo=claude\stub_claude.obj claude\stub_claude.c

clean: .SYMBOLIC
	-del lib\*.obj
	-del launcher\*.obj
	-del discord\*.obj
	-del claude\*.obj
	-del netisa.com
	-del netisa.exe
	-del netisa_hw.exe
	-del discord.exe
	-del claude.exe
	-del *.err
