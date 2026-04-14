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

CATHODE_OBJS = cathode\main.obj cathode\browser.obj cathode\render.obj &
	cathode\input.obj cathode\page.obj cathode\urlbar.obj cathode\stub_pages.obj &
	cathode\utf8.obj cathode\url.obj cathode\htmltok.obj cathode\htmlout.obj &
	cathode\fetch.obj cathode\search.obj cathode\bookmark.obj

CATHODE_CFLAGS = -0 -fpi -ms -s -ox -w4 -zq -bt=dos -i=lib -i=cathode

DISCORD_OBJS = discord\main.obj discord\discord.obj discord\render_dc.obj &
	discord\input_dc.obj discord\stub_discord.obj

DISCORD_CFLAGS = -0 -fpi -ms -s -ox -w4 -zq -bt=dos -i=lib -i=discord

CLAUDE_OBJS = claude\main.obj claude\claude.obj claude\chat.obj &
	claude\compose.obj claude\agent.obj claude\splash.obj claude\stub_claude.obj

CLAUDE_CFLAGS = -0 -fpi -ms -s -ox -w4 -zq -bt=dos -i=lib -i=claude

all: tsr launcher cathode discord claude .SYMBOLIC

tsr: netisa.com .SYMBOLIC

launcher: netisa.exe .SYMBOLIC

cathode: cathode.exe .SYMBOLIC

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

# Cathode browser
cathode.exe: $(CATHODE_OBJS) $(LIB_SCREEN) $(LIB_STUB)
	$(LINKER) format dos libpath C:\WATCOM\lib286 libpath C:\WATCOM\lib286\dos name cathode.exe &
		file cathode\main.obj &
		file cathode\browser.obj &
		file cathode\render.obj &
		file cathode\input.obj &
		file cathode\page.obj &
		file cathode\urlbar.obj &
		file cathode\stub_pages.obj &
		file cathode\utf8.obj &
		file cathode\url.obj &
		file cathode\htmltok.obj &
		file cathode\htmlout.obj &
		file cathode\fetch.obj &
		file cathode\search.obj &
		file cathode\bookmark.obj &
		file lib\screen.obj &
		file lib\netisa_stub.obj &
		option quiet

cathode\main.obj: cathode\main.c cathode\browser.h cathode\render.h cathode\input.h lib\screen.h
	$(CC) $(CATHODE_CFLAGS) -fo=cathode\main.obj cathode\main.c

cathode\browser.obj: cathode\browser.c cathode\browser.h cathode\stub_pages.h cathode\htmltok.h cathode\fetch.h cathode\bookmark.h lib\screen.h
	$(CC) $(CATHODE_CFLAGS) -fo=cathode\browser.obj cathode\browser.c

cathode\render.obj: cathode\render.c cathode\render.h cathode\search.h cathode\page.h lib\screen.h
	$(CC) $(CATHODE_CFLAGS) -fo=cathode\render.obj cathode\render.c

cathode\input.obj: cathode\input.c cathode\input.h cathode\browser.h cathode\bookmark.h lib\screen.h
	$(CC) $(CATHODE_CFLAGS) -fo=cathode\input.obj cathode\input.c

cathode\page.obj: cathode\page.c cathode\page.h lib\screen.h
	$(CC) $(CATHODE_CFLAGS) -fo=cathode\page.obj cathode\page.c

cathode\urlbar.obj: cathode\urlbar.c cathode\urlbar.h cathode\input.h lib\screen.h
	$(CC) $(CATHODE_CFLAGS) -fo=cathode\urlbar.obj cathode\urlbar.c

cathode\stub_pages.obj: cathode\stub_pages.c cathode\stub_pages.h cathode\render.h cathode\page.h lib\screen.h
	$(CC) $(CATHODE_CFLAGS) -fo=cathode\stub_pages.obj cathode\stub_pages.c

cathode\utf8.obj: cathode\utf8.c cathode\utf8.h
	$(CC) $(CATHODE_CFLAGS) -fo=cathode\utf8.obj cathode\utf8.c

cathode\url.obj: cathode\url.c cathode\url.h
	$(CC) $(CATHODE_CFLAGS) -fo=cathode\url.obj cathode\url.c

cathode\htmltok.obj: cathode\htmltok.c cathode\htmltok.h cathode\htmlout.h cathode\utf8.h cathode\page.h lib\screen.h
	$(CC) $(CATHODE_CFLAGS) -fo=cathode\htmltok.obj cathode\htmltok.c

cathode\htmlout.obj: cathode\htmlout.c cathode\htmlout.h cathode\htmltok.h cathode\render.h cathode\url.h cathode\page.h lib\screen.h
	$(CC) $(CATHODE_CFLAGS) -fo=cathode\htmlout.obj cathode\htmlout.c

cathode\fetch.obj: cathode\fetch.c cathode\fetch.h cathode\stub_pages.h cathode\url.h lib\screen.h lib\netisa.h
	$(CC) $(CATHODE_CFLAGS) -fo=cathode\fetch.obj cathode\fetch.c

cathode\search.obj: cathode\search.c cathode\search.h cathode\page.h lib\screen.h
	$(CC) $(CATHODE_CFLAGS) -fo=cathode\search.obj cathode\search.c

cathode\bookmark.obj: cathode\bookmark.c cathode\bookmark.h cathode\url.h
	$(CC) $(CATHODE_CFLAGS) -fo=cathode\bookmark.obj cathode\bookmark.c

# Discord client
discord.exe: $(DISCORD_OBJS) $(LIB_SCREEN)
	$(LINKER) system dos name discord.exe &
		file discord\main.obj &
		file discord\discord.obj &
		file discord\render_dc.obj &
		file discord\input_dc.obj &
		file discord\stub_discord.obj &
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
	-del cathode\*.obj
	-del discord\*.obj
	-del claude\*.obj
	-del netisa.com
	-del netisa.exe
	-del netisa_hw.exe
	-del cathode.exe
	-del discord.exe
	-del claude.exe
	-del *.err
