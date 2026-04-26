# The universal DOS text-mode UI/UX reference

This reference fills three gaps in the DOS text-mode UI canon: a widget catalog beyond benchmark-tool primitives, a per-adapter progressive-enhancement tier specification, and screen-layout templates for the major application archetypes. **Every widget, every adapter feature, and every archetype is tied to concrete CP437 code points, INT 10h register values, port I/O sequences, Turbo Vision class names, and named exemplar applications** so a developer can implement any DOS TUI with maximum visual quality and correct behavior on every supported adapter tier from MDA through SVGA/VESA.

The unifying thesis: the DOS TUI ecosystem standardized around three conventions (IBM CUA/SAA for keyboarding, Borland's blue/cyan/gray palette for visual rhetoric, and the orthodox Norton Commander bottom-F-key legend for command discovery), while the visual upper bound was set by which adapter the BIOS reported at INT 10h/AH=1Ah. Knowing the exact feature gate at each tier lets a program degrade gracefully from an 18-bit RGB gradient on VGA to a monochrome underline on MDA without forking the codebase.

---

## Part A — Universal widget catalog

### Foundations shared by every widget

**Attribute byte layout (all color adapters):** `(blink<<7) | (bg<<4) | fg`. Colors 0 Black, 1 Blue, 2 Green, 3 Cyan, 4 Red, 5 Magenta, 6 Brown, 7 LtGray, 8 DkGray, 9 LtBlue, A LtGreen, B LtCyan, C LtRed, D LtMagenta, E Yellow, F White. If INT 10h AX=1003h BL=00h is issued, bit 7 becomes background-intensity and 16 background colors become available.

**CP437 character vocabulary used across widgets (decimal):**

| Range | Chars | Role |
|---|---|---|
| 24–31 | 24 ↑ 25 ↓ 26 → 27 ← 30 ▲ 31 ▼ | Scrollbar arrows, menu markers |
| 176–178, 219–223 | 176 ░ 177 ▒ 178 ▓ 219 █ 220 ▄ 221 ▌ 222 ▐ 223 ▀ | Shading, fill, progress bars |
| 179–218 (single) | 179 │ 180 ┤ 191 ┐ 192 └ 193 ┴ 194 ┬ 195 ├ 196 ─ 197 ┼ 217 ┘ 218 ┌ | Single-line frames, trees |
| 185–188, 200–205 (double) | 186 ║ 187 ╗ 188 ╝ 200 ╚ 201 ╔ 205 ═ | Double-line frames (active window) |
| 249–254 | 249 ∙ 250 · 251 √ 254 ■ | Bullets, checkmarks, markers |
| 1–15 | 1 ☺ 2 ☻ 13 ♪ 15 ☼ | Repurposed as pictograms |

**Canonical Borland/Turbo Vision palette slots (hex attribute bytes):** 0x17 inactive-blue-frame; 0x1E yellow-on-blue (shortcut letter); 0x1F white-on-blue (active-blue-frame and normal text); 0x30 black-on-cyan (menu, help); 0x3F white-on-cyan (focused menu); 0x70 black-on-lt-gray (dialog body); 0x7F white-on-lt-gray (dialog active frame); 0x78 dim/disabled; 0x20 black-on-green (button normal); 0x2F white-on-green (focused default button); 0x2E yellow-on-green (button shortcut letter); 0x4F white-on-red (error); 0x71 blue-on-lt-gray (selection/inverse).

**CUA/SAA 1989/1991 global keys honored across every archetype:** F1 Help, F3 Exit, F10 Menu bar, Alt+letter menu access, Tab/Shift+Tab field traversal, Enter default, Esc cancel, Space toggle, Alt+F4 close window, Ctrl+F6 next window, Shift+F10 context menu, Alt+Space system menu.

### 1. Scrollable text viewers and TScrollBar

The canonical scrollbar is five glyphs stored in Turbo Vision's `TScrollChars` array: arrow[0]=CP437 24 ↑ vertical (27 ← horizontal), arrow[1]=25 ↓ (26 →), page=**177 ▒**, thumb=**178 ▓**, reserved=space. Borland's `TScrollBar` palette assigns page-area to slot 0x30 (dialog) or 0x13 (blue window), thumb/arrows to 0x3F/0x1F. Some implementations substitute 219 █ for the thumb or 30 ▲/31 ▼ for the arrows (Norton Commander convention).

**Vertical bar rendered, thumb at 30 %:**
```
┌─ FOO.PAS ─────┐
│ line 1      ▲ │   24
│ line 2      ▒ │   177 page area above
│ line 3      ▓ │   178 thumb
│ line 4      ▒ │
│ line 5      ▼ │   25
└───────────────┘
```

**Turbo Vision class hierarchy:** abstract `TScroller` (viewport with `delta.x/delta.y` offsets) → concrete `TEditor`, `TFileEditor`, `TMemo`, `THelpViewer`, `TTerminal`; `TScrollBar` (the bar itself); `TEditWindow` bundles TFileEditor + 2× TScrollBar + `TIndicator` (line:col display, bottom-left of frame).

**Keyboard (CUA + WordStar diamond, dual bindings inherited by Borland IDE, Brief, QEdit, TSE):** arrows for line/char; Ctrl+←/→ word jump; PgUp/PgDn page; Ctrl+PgUp/PgDn top/bottom; Home/End BOL/EOL; Ctrl+Home/End top/bottom of visible window; F3/Shift+F3 Find Next; Ctrl+QF find (WordStar legacy); Ctrl+Y delete line; Ctrl+K B/K/C/V/Y WordStar block ops. Word wrap is a `wordWrap` Boolean tracked by `TEditor`; it breaks at the last whitespace column and, when enabled, disables horizontal delta. **Search highlight is an XOR of the attribute byte's nibbles, not a repaint**, usually 0x70 over the blue base, which avoids snow on CGA and is O(1) per cell.

**Exemplars:** EDIT.COM (QBasic-derived, not TV), Borland IDE editor (TP 6/7, TC++ 3.1, BC++ 3.1/4/5), Brief 3.x (modeless line-marking), QEdit/TSE (SemWare), MultiEdit, Vernon Buerg's LIST.COM 9.x (asm-coded; identical 177/178/24/25 idiom; q/x quit, / search, N next, Alt-E video, Alt-W wrap, + goto), Norton Commander F3 viewer (white-on-blue, F7 search, F8 hex toggle).

### 2. Tree-view controls

The canonical expand-collapse tree uses **195 ├** (child with following siblings), **192 └** (last child), **179 │** (ancestor-rail for "has more siblings"), **196 ─** (horizontal connector), with `[+]` collapsed and `[-]` expanded markers. Turbo Vision's `TOutlineViewer`/`TOutline` (TV 2.0, 1992) is the stock class; override `getRoot`, `getChild(node,i)`, `getNumChildren`, `getText(node,buf,size)`.

```
C:\
├─[-] DOS
│  ├─ COMMAND.COM
│  └─ FORMAT.COM
├─[+] WINDOWS
└─[-] TURBOC
   ├─ BIN
   └─ INCLUDE
```

**Keyboard:** ↑/↓ cursor; ←/→ collapse/expand (Windows-95-ish, adopted c. 1992; earlier NC/XTree used + and −); \* expands an entire subtree in XTree ("log branch" semantics, distinct from NC's file-panel Gray \* which is "invert selection"); / expands to root (XTree); type-ahead jumps to matching sibling. XTree adds Alt+F1/F2 swap drives, T tag, Ctrl+T tag branch, \\ "Treespec" path jump. **Color:** normal 0x1F, focused 0x71 (inverted), [+]/[−] marker 0x1E, rails 0x17. **Implementation trick:** render depth-first keeping a per-depth "has-more-siblings" bitmask; bit set → draw 179 │ in that column, bit clear → draw space; the last child clears that depth's bit and emits 192 └. Visible-line count is recomputed lazily on expand/collapse.

**Exemplars:** Norton Commander tree panel (Ctrl+F8, or Alt+F10 for full-screen NCD-style navigator), Volkov Commander, DOS Navigator (Turbo Vision-based), Midnight Commander, MS-DOS Shell (DOSSHELL.EXE 4.0–6.22, upper-left pane), XTree/XTree Gold 3.01 (tree is the primary view), PC Tools PCSHELL, Borland IDE project tree (BC++ 4.5/5).

### 3. Split-pane and dual-pane layouts

The orthodox file-manager pattern pioneered by Norton Commander 1.0 (John Socha, 1986) has two equal panels abutting at a shared double-line frame—**no dedicated splitter character**, simply two frames that touch. Each panel has its own file collection and cursor; active/inactive is a TGroup focus state visible in the title-bar attribute (0x1F active, 0x17 inactive) and the presence of the selection bar. A shared `TInputLine`-style command line lives on row 23 and consumes untrapped keys so typing "dir" auto-focuses the prompt without Tab.

```
╔═══════ C:\DOS ═══════╗╔═══════ D:\WORK ══════╗
║ COMMAND  COM  54645  ║║ AUTOEXEC BAT    128  ║
║ FORMAT   COM  32911  ║║ CONFIG   SYS    256  ║
╚════ 3 files ═════════╝╚════ 3 files ═════════╝
 C:\DOS>_
 1Help 2Menu 3View 4Edit 5Copy 6RenMov 7Mkdir 8Delete 9PullDn 10Quit
```

**The orthodox keyboard contract** (Tab swap active, Ctrl+U swap panel contents, Ctrl+O hide panels to reveal raw DOS prompt, Ctrl+L info-view partner, Ctrl+Q quick-view partner, Alt+F1/F2 drive selector, Ins tag, Gray + tag by mask, Gray − untag, Gray \* invert, Ctrl+Enter pull filename onto command line) is the defining fingerprint—honored by Volkov, DN, Midnight Commander, FAR, and Total Commander. The `F1–F10` legend at row 25 (Help/Menu/View/Edit/Copy/RenMov/MkDir/Delete/PullDn/Quit) is instantly recognizable. Swapping (Ctrl+U) is a pointer exchange of TFileCollection pointers—O(1). Before spawning a shell, the program snapshots 4 000 bytes of B8000 into a heap buffer and restores on return (TV's `TProgram::suspend`/`resume`).

### 4. Tab strip and notebook controls

Stock Borland Turbo Vision 2.0 has **no TTab class**—the closest is Borland OWL `TNotebook` and later Delphi `TPageControl`/`TTabControl`. The window-list chooser (Alt+0) uses `TWindowList`/`TListViewer`. Third-party RHTVision/FreeVision added `TTabbedView`. **Quattro Pro for DOS 3.0+ (Borland, 1990) popularized the bottom-sheet notebook** with up to ~18 000 sheets. Quarterdeck Manifest (1990) used a distinctive left-column tab stack.

```
├─[Sales]─┬─┐                                      bottom-tab style
│  A  B C │
╞═════════╧═╧══════════════════════════════════════
 │ Sales │ Q1 │ Q2 │ Q3 │ Q4 │ Summary │     ◄ ►
```

Active tabs paint one row taller to break the divider line (the row above is drawn as 196 ─ except over the active label where it is space); junctions use 197 ┼. Active label uses 0x7F White on LtGray; inactive 0x70 Black on LtGray or 0x30 Black on Cyan; shortcut letter 0x7E Yellow on LtGray. **Keyboard:** Alt+digit 0–9 jumps to named window (Borland IDE), Ctrl+Tab/Ctrl+Shift+Tab next/prev (CUA 1991), **Ctrl+PgUp/Ctrl+PgDn next/prev sheet** (Quattro Pro DOS and Windows, Lotus 1-2-3 r3+), Shift+F6/F7 next/prev sheet (Quattro DOS), Alt+L via tilde-accelerator `~L~abel` in the tab string.

### 5. Multi-page form wizards

The canonical five-step layout uses a title bar with "Step N of M," a content body, a ■/○ step indicator near the bottom, and `[< Previous] [ Next >] [ Cancel ]` buttons. Step indicator glyphs are filled CP437 7 • or 254 ■ for the current step and rings/dots 250 · for remaining. Turbo Vision has no dedicated wizard class—idiom is a sequence of modal `TDialog`s driven by a shared state record exchanged via `setData`/`getData`; each view (TInputLine, TCheckBoxes) contributes bytes at fixed offsets in the struct. Validators: `TPXPictureValidator`, `TRangeValidator`, `TLookupValidator`, `TFilterValidator`.

**Keyboard:** Enter default (Next or Install), Esc Cancel with confirm, Alt+N Next, Alt+B or Alt+P Previous, Alt+F Finish, Tab/Shift+Tab traversal, Space toggle checkbox, arrows cycle radio group. Next is disabled (0x78 dim) until required fields validate. Back stack = array of `(pageIndex, struct_copy)` so Previous restores both UI and validated state; pages are hidden via `setState(sfVisible, False)` rather than destroyed, preserving focus.

**Exemplars:** Microsoft Setup (Word for DOS 5.5, Works), **Borland INSTALL.EXE** (TV-based with full validator chain), **DOOM SETUP.EXE** (id, 1993–96; EGA mode 3, "funky" darker blue in v1.5 reverted in v1.7+; built on the **Laughing Dog Screen Maker** by Yardbird Software inherited from Raptor's setup engine by Paul Radek), Stacker install, Windows 3.x text-mode SETUP, Novell NetWare INSTALL, MemMaker (MS-DOS 6 wizard, Helix Software for Microsoft). Early MS installers drove a flat `.INF` script via a small VM; a Cancel at any step rolled back by distinguishing committed vs staged changes, commit only on Finish.

### 6. Data grids and spreadsheet tables

No stock TV class exists; the pattern is a `TScroller` subclass whose `draw()` iterates a cumulative-width column array and clips at delta.x, with the cell cursor implemented as a per-cell attribute swap (0x70→0x07). Closest stock class is `TListViewer` with columns. Delphi later introduced `TStringGrid`/`TDBGrid`.

```
A1: [W9] 'Sales                                             READY
╔═════════╤═════════╤═════════╤═════════╤═════════╤════╗
║         │    A    │    B    │    C    │    D    │ E  ║
╠═════════╪═════════╪═════════╪═════════╪═════════╪════╣
║    1    │Sales    │   Q1    │   Q2    │   Q3    │Tot ║
║  ▓ 2 ▓  │▓North▓▓▓│▓ 12,500▓│▓ 13,200▓│▓ 11,800▓│51,6║  ← cell cursor
╚═════════╧═════════╧═════════╧═════════╧═════════╧════╝
                                   1-2-3   (F1=Help)  CAPS NUM
```

Row dividers 205 ═ (double), intersections 206 ╬ / 203 ╦ / 202 ╩ / 204 ╠ / 185 ╣; column separator 179 │ (single through body, double on outer frame—the "3-D" Lotus look); frame/header 0x70 (1-2-3) or 0x4F; body 0x07 (1-2-3) or 0x1F (Quattro); **-14,523** negative in Red 0x74; formula bar mode indicator ("READY/LABEL/VALUE/POINT/EDIT/ERROR/CIRC/WAIT/CMD") in yellow 0x7E; `*****` shown when a column is too narrow for the formatted number.

**Keyboard** (Lotus slash-menu, the defining signature): Arrows cell-move, End+arrow jumps to next non-blank (1-2-3 signature), Home→A1, F5 Goto, F2 Edit, F3 Name, F4 Abs/Rel, F9 Calc, F10 Graph. **`/WCS` sets column width** (Worksheet, Column, Set-width, interactive with →/←); **`/WT` titles-freeze** (H/V/B/C for horizontal/vertical/both/clear); `/WWH`/`/WWV` split; Quattro adds Ctrl+PgUp/PgDn sheet switch. dBASE BROWSE uses WordStar diamond (Ctrl+X/E next/prev field, Ctrl+D/S char right/left), plus `FIELDS`, `FREEZE <col>` (single editable column), `LOCK <n>` (n leftmost columns pinned on horizontal scroll—direct analog to Lotus /WT).

**Exemplars:** Lotus 1-2-3 r1–r5, Quattro Pro 1–5 DOS (Borland 1989+, Surpass codebase), SuperCalc 3/4/5, Twin, VP-Planner (Paperback Software look-and-feel suit), Multiplan, dBASE III/IV/V BROWSE, Paradox 3–4 table view, Clipper TBrowse(), FoxBase/FoxPro.

### 7. Combo boxes and dropdowns

Turbo Vision has no `TComboBox` class; the combo pattern is the **THistory + TInputLine + THistoryWindow** triad. `TInputLine` is the one-line editor, `THistory` is the ▼ dropdown trigger drawn one cell wide to the right, `THistoryViewer`/`THistoryWindow` is the modal TListBox popup. The trigger glyph is CP437 **25 ↓** or **31 ▼** on attribute 0x1E Yellow on Blue.

```
Filename: [ README.TXT___________________ ][▼]
          ╔══════════════════════════════╗
          ║ README.TXT                 ▒ ║
          ║ CHANGES.LOG                ▒ ║
          ║ MAKEFILE                   ▓ ║
          ╚══════════════════════════════╝
```

**Keyboard:** ↓ or Alt+↓ opens; Esc closes; Enter commits; type-ahead first-letter jump via `TListViewer::focusItemNum`. Autocomplete-while-typing subclasses TInputLine to intercept printable chars and rebuild a filtered collection every keystroke. History ring is global, keyed by integer `historyId`—multiple inputs with the same id share the same list, persisted via `StoreHistory`/`LoadHistory` on the TV stream. Popup placement is computed from the input line's absolute cursor rect, flipped upward if insufficient screen below. **Exemplars:** Word 5.5 for DOS (file-open combos, style dropdowns), Works for DOS mail-merge field picker, Borland IDE File-Open/Change-Dir (press ↓ for last 20 paths—the textbook TV combo), WordPerfect 5.1/6.0 printer-model chooser, dBASE IV Control Center field popup, Paradox 4.5 lookup, Stacker drive-letter combos.

### 8. Multi-line edit controls

Turbo Vision's `TEditor` is a 64 KB gap buffer (grows in 4 KB increments) with fields `char *buffer; int bufSize, bufLen, gapLen; int selStart, selEnd, curPtr; TPoint curPos, delta, limit; int drawLine, drawPtr; int overwrite, autoIndent`. Insertion moves the gap to curPtr and shrinks gapLen by one per char; deletion extends the gap. Undo is stored as delCount/insCount. Wrapped by `TFileEditor` (file I/O) and `TEditWindow` (TWindow + 2× TScrollBar + TIndicator). `TMemo` is the fixed-size edit control inside dialogs.

**Word wrap** is not intrinsic to TEditor—it horizontally scrolls via delta.x with lines clipped to `limit.x` (default 1024). WordPerfect 5.1 hard-wraps at margin with a soft-return code [SRt]; dBASE IV memo editor soft-wraps at window edge. **Selection** is stored as two byte offsets; drawing swaps fg/bg nibbles = reverse video (0x71 blue-on-gray in the TV blue palette). Shift+arrows extend; Ctrl+Ins copy, Shift+Del cut, Shift+Ins paste; WordStar Ctrl+K B/K/C/V accepted in parallel. **Insert/overwrite cursor shape** set via INT 10h AH=01h: insert = thin underline (CX=0607h on CGA/VGA 8-line cell, 0B0Ch on MDA 14-line); overwrite = full block (CX=0007h / 000Dh). INS key issues `cmInsMode` broadcast; `TIndicator` shows ▓ or 254 ■ when modified plus Ln:Col plus mode. WordPerfect 5.1/6.0 kept documents in EMS/XMS to escape the 64 KB segment limit.

### 9. Gauge, meter, and progress widgets

No stock TV class—subclass `TView`, draw via `TDrawBuffer` with `moveChar(b, 219, color, filledCells)` followed by `moveChar(b, 176, color, remaining)`, then writeLine. Borland TI1532 documented the continuously updated message-box pattern. FreeVision and RHIDE added `TProgressBar`.

**Percent bar:** `[████████████░░░░░░░░░░] 54 %` using 219 █ and 176 ░. **Smoothed (eighth-cell):** use 221 ▌ for the partial-fill cell: `[████████▌░░░░░░░░░░] 42 %`. `filled = (value * width * 8) / max` picks the glyph. **Dithered fill** uses the 176/177/178/219 shade progression. **Indeterminate spinner:** rotate `|` (124) → `/` (47) → `─` (196) → `\` (92) at ~8 Hz via `printf("%c\b", frames[i++ & 3])`—the one-byte backspace homes the column without full cursor reposition. Some apps pulsed ▲▼ (30/31) or ◄► (17/16).

**Cluster grids** (the DEFRAG/SCANDISK/NDD signature): one screen cell = N clusters; **DEFRAG packed two clusters per cell using 223 ▀ upper-half and 220 ▄ lower-half with independent fg/bg colors**, doubling vertical density. Color legend: green 0x1A used, yellow 0x1E reading, red 0x1C writing, blue 0x19 directory, bright white 0x1F unmovable. SCANDISK: green 254 good (0x0A), red 254 bad (0x0C), yellow being-checked (0x0E). Writes are double-buffered—redraw only cells whose state changed, not the whole grid—and on CGA waits for vertical retrace before each cluster-cell write to avoid snow.

**Exemplars:** FORMAT.COM (DOS 5+), DEFRAG.EXE (DOS 6, licensed from Symantec Speed Disk), Norton Speed Disk (SPEEDISK.EXE), NDD.EXE, McAfee SCAN/VSHIELD, PKZIP/PKUNZIP 2.04g (text "75 %" counter), SCANDISK (DOS 6.2+), Stacker/DoubleSpace setup, XTree Gold file copy, all installers (Microsoft SETUP, Borland INSTALL, WP Install).

### 10. Message boxes and alert levels

Turbo Vision's `messageBox(const char *msg, ushort aOptions)` and `messageBoxRect()` live in `msgbox.h`. Low 2 bits of aOptions select title/severity: `mfWarning=0x0000`, `mfError=0x0001`, `mfInformation=0x0002`, `mfConfirmation=0x0003`. Upper bits OR in buttons: `mfYesButton=0x0100`, `mfNoButton=0x0200`, `mfOKButton=0x0400`, `mfCancelButton=0x0800`; combinations `mfYesNoCancel`, `mfOKCancel`. Return codes: `cmOK=10`, `cmCancel=12`, `cmYes=13`, `cmNo=14`.

**Icon vocabulary (CP437 decimal):** 33 `!` (warning, often inside a 30/222/33/221 triangle), 19 `‼` (severe), 63 `?` (confirmation), 9 `○` or 10 `◙` (info, Norton/NDOS style), 254 `■` (generic), 15 `☼` (tip). **Severity colors:** info 0x1F blue-body with icon 0x1B bright-cyan; warning 0x1E yellow-on-blue (or 0x4E white-on-yellow body in NC/PC-Tools); error whole box 0x4F; confirmation standard gray 0x70. A drop-shadow is added by OR-ing `0x08` into two columns right of the box and one row below while preserving the underlying glyph (Borland's `xorAttribute` trick).

**INT 24h Abort/Retry/Ignore/Fail semantics** for the DOS critical-error handler: AL=0 Ignore, AL=1 Retry, AL=2 Abort (via INT 23h), AL=3 Fail (DOS 3.1+). Valid options are gated by AH bits: bit 3=Fail allowed, bit 4=Retry allowed, bit 5=Ignore allowed. TV and CUA-compliant apps omit buttons whose option bit is clear. COMMAND.COM showed "Abort, Retry, Fail?" in DOS ≥3.3 and "Abort, Retry, Ignore?" in ≤3.2; DOS 3.3 added `/F`; DR-DOS 7 and 4DOS added `CritFail=Yes`.

### 11. Context menus and popup menus

**Shift+F10** is the CUA-canonical context-menu invocation (1989/1991 spec; Microsoft Word 5.5/6 for DOS followed it). **Alt+Space** opens the window system menu (Move/Size/Zoom/Close, `cmSysMenu` in TV). **F10** activates the main menu bar. **F9** was Norton Commander's pulldown (chosen because Shift was reserved for file selection) and Midnight Commander inherited it. **Right mouse button** via Microsoft mouse INT 33h AX=0003h returns BX button state (bit 1 = right), CX/DX position in pixels (or chars ×8 in text mode); TV maps to `evMouseDown` with `buttons & mbRightButton`.

**TV classes:** `TMenuBar` (top), `TMenuBox` (drop-down), `TMenuPopup` (free-floating, TV 2.0+: `TMenuPopup(TRect, TMenu*)`). Items via `TMenuItem("~C~ut", cmCut, kbCtrlX, hcCut, "Ctrl-X", nextItem)`; separators via `newLine()`. Colors: normal 0x30, disabled 0x78, selected 0x1F, shortcut 0x3E normal / 0x1E selected (yellow). Checkmarks use 251 √ or 250 ·; disabled items dim. To avoid snow on CGA, menus are drawn with direct video writes gated on port 03DAh bit 3 (vertical retrace).

**Apps with true right-click context menus:** Borland IDE (Turbo Pascal 7, Turbo C++ 3.1, Borland C++ 3.1/4.x)—right-click in editor pops Topic Search/Go to cursor/Run to cursor/Open file at cursor/Breakpoint; Norton Commander 5.0 right-click = user menu (F2); DESQview 2.x Alt+Space window menu; 4DOS/NDOS directory history.

### 12. Toolbars and button bars

Two layout traditions coexisted: **top icon toolbar** (Microsoft Works 2/3, WordPerfect 6.0/6.2 Button Bar, Quattro Pro 4/5 SpeedBar, Lotus 1-2-3 r3.x SmartIcons, Paradox 4.5) versus **bottom F-key legend** (Norton Commander, Volkov, DN, Borland IDE via `TStatusLine`, MS-DOS Shell, WordPerfect's printed template). NC's legend shows digit 0x0F (bright white on black) alternating with label 0x30 (black on cyan); F-key modifier variants (Shift+Fn, Alt+Fn, Ctrl+Fn) live-swap the bar as the modifier is held (poll INT 16h AH=12h shift flags and redraw on change).

**Glyph rendering in text mode** used INT 10h AX=1100h to overload CP437 128–255 (or a dedicated 8 KB block) with custom 8×16 bitmaps depicting folder, floppy, printer pictographs. **WordPerfect 6.0 shipped a GRAPHICS.DRV with such fonts**; Quattro Pro's WYSIWYG mode switched to 640×480 graphics to paint true bitmap icons while retaining a text-like UX. Works for DOS overloaded CP437 1–31 with icon glyphs, and fake icons from existing box chars (`┌─┐│■│└─┘`) gave a serviceable pictograph. Stock TV has no toolbar class; the canonical substitute is `TStatusLine` at bottom; third-party TVX/TVFN added `TToolBar`.

### 13. Status bar patterns

Zones are separated by **CP437 179 │** (single) or 186 ║ (double). Standard indicators: **INS/OVR** (app-local flag, reflected on cursor shape), **CAPS/NUM/SCRL** (read directly from BIOS data area 0040:0017h—bit 6 Caps, bit 5 Num, bit 4 Scroll—on each timer tick for speed), **Ln/Col** (1-based; WP used inches: `Ln 2"/Pos 1.5"`), **modified \*** (char 42 or 254 ■, shown by TV `TIndicator`), **truncated doc name**, **clock** (HH:MM, update only when MM changes to avoid flicker, read via INT 21h AH=2Ch), **free memory** (INT 21h AH=48h BX=FFFFh returns largest-block in BX).

TV's `TStatusLine` builds from a linked list of `TStatusDef(minCtx, maxCtx)` each owning `TStatusItem("~Alt-X~ Exit", kbAltX, cmQuit)`. `TStatusLine::hint(helpCtx)` returns a string that overrides items when help context matches—the tooltip-analogue. Colors: normal 0x30, selected 0x1F, hotkey 0x3F, disabled 0x38. **Exemplar status-line vocabularies:** WordPerfect `Doc 1 Pg 3 Ln 2" Pos 1.5"` (POS flashes when NumLock); Word 5.5 `CHAPTER1.DOC │ Pg 3 Ln 12 Col 25 │ INS │ 14:32 │ 423K`; Lotus 1-2-3 top-right mode word cycling READY/LABEL/VALUE/POINT/EDIT/MENU/WAIT/ERROR/CMD/CIRC/END/OVR.

### 14. Popup notifications and toast messages

Five distinct DOS idioms: (1) **status-line transient** where WP writes "File saved" to the left zone for ~2 s via `{STATUS PROMPT}` that auto-clears only on keystroke/command-end; (2) **"Press any key to continue"** blocking halt via INT 21h AH=08h or INT 16h AH=00h, text at 0x0F with optional BEL char 7; (3) **modeless TDialog** inserted into deskTop without execView, updated via `setText`/TVarText per Borland TI1436; (4) **TStatusLine hints** keyed by helpCtx; (5) **flash boxes** (Norton Commander "Reading…") that save B8000 bytes to heap, overwrite, then restore on dismiss—fast and flicker-free. Auto-expiring toasts poll BIOS ticks at 0040:006Ch (18.2 Hz, 4 bytes) to measure elapsed time. Error toasts blink by setting attribute bit 7 on 0x1C (red). BEL emits via `putchar(7)` or INT 21h AH=02h DL=07h without blocking.

---

## Part B — Per-adapter progressive enhancement tiers

### Tier 0 — MDA (IBM, 1981)

**Hardware:** MC6845 CRTC + 4 KB SRAM + 9264 character ROM, 16.257 MHz pixel clock, 50 Hz vertical refresh, TTL Video+Intensity output, integrated LPT (IRQ 7). **Frame buffer B000:0000**, 4 KB, repeats every 4 KB up to B7FFFh. **Only usable mode: 7** (80×25, 9×14 cell, 720×350 theoretical). Font is fixed CP437 in ROM (bitmaps 8 px wide; for chars 0xC0–0xDF the 9th pixel duplicates the 8th to keep box-drawing joins unbroken; blank for all others).

**Attribute byte is NOT IRGB.** Bits 2–0 exactly equal `001b` → underline; any other value → no underline. Bit 3 foreground intensity. Bits 6–4 as background but only 000b (black) and 111b (green) produce distinct output. Bit 7 blink if 3B8h bit 5 is 1, else background intensity. **The only nine visually distinct attribute values** are: 00h/08h/80h/88h invisible; 01h underline; 07h normal; 09h bright underline; 0Fh bright/bold; 70h reverse; 78h reverse-halo; F0h blinking reverse; 87h blinking normal; 8Fh blinking bold.

**Ports:** 3B4h/3B5h CRTC address/data; 3B8h mode control (bit 0 must be 1—IBM warns CPU may hang if cleared; bit 3 video enable; bit 5 blink enable); 3BAh status (bit 0 hblank, bit 3 video-drawing, bits 7–4 always `1111` on real IBM MDA—a detection fingerprint); 3BC–3BEh parallel port. **BIOS default mode-7 value for 3B8h = 29h.** Cursor 0–13 scanlines via INT 10h AH=01h. 4 KB = exactly one page. **UI capability: only nine distinct looks. Use CP437 box-drawing for geometry, underline for emphasis, reverse video for selection.**

### Tier 1 — Hercules Graphics Card family

All three HGC variants are MDA-supersets with no BIOS extension—IBM BIOS treats them as MDA for text (INT 10h AL=7). Shared ports 3B0–3B7h CRTC, 3B8h mode, 3BAh status, plus **3BFh configuration** (bit 0 enable graphics; bit 1 enable "full" mode exposing B8000). Extended 3BAh status bits 6–4 provide a card ID: `000b` HGC, `001b` HGC+, `101b` InColor, `111b` unknown clone.

**Hercules HGC (1982):** text mode identical to MDA, adds 720×348 mono graphics. ROM font only, no redefinition. **HGC+ (1986):** adds **RamFont**—12 KB of RAM at B4000h holds up to 3 072 simultaneous characters (in 48 KB mode, 12 fonts × 256). Enable via `OUT 3BFh, 03h` then CRTC idx 14h ("xMode") with bit 0=1 RAM-font, bit 1=1 selects 90×25 columns, bit 2=1 selects 48 KB RAMfont. CRTC 15h programmable underline row, 16h programmable strikethrough. **InColor Card (GB222, April 1987):** 16 colors in text mode on an EGA monitor, 256 KB RAM as 4 planes × 64 KB. Attribute system switches between MDA-style and CGA-style via **CRTC reg 17h bit 5** (1 = MDA, 0 = CGA); bit 4 selects between 16-byte palette LUT (loaded via CRTC reg 1Ch after reading it to reset counter) and raw IRGB. In CGA-attribute mode, bits 3–0 are foreground, bits 6–4 background (if blink enabled), bit 7 blink or bg-intensity. RamFont with 16-color planes gives 3 072 colored 8×14 mini-sprites or up to 12 288 two-color characters across all four planes. **UI capability on InColor:** full 16-color CGA-equivalent text plus arbitrarily designed glyphs—but vanishingly few programs ever activated CGA-attribute mode.

**Detection:** poll port 3BAh in a loop of ~32 768 iterations; if bit 7 toggles, it's Hercules-class (vsync bit); if constant at 1, it's true IBM MDA. Mask bits 6–4 for variant. Recommended portable test: `(in(3BAh) >> 4) & 3`: 0 = plain Herc, 1 = HGC+ or InColor-compatible.

### Tier 2 — CGA (IBM, 1981)

**Frame buffer B800:0000, 16 KB.** Modes 0 (40×25 B/W), 1 (40×25 color), 2 (80×25 B/W), 3 (80×25 color). **Mode 7 is NOT CGA**—that's the monochrome adapter. 8×8 ROM font, no redefinition. **No 80×43 possible**—200 scanlines ÷ 8 rows = 25. Attribute byte is standard IRGB/IRGB.

**Ports:** 3D4h/3D5h CRTC; **3D8h mode control**—bit 0 80-col, bit 1 graphics, bit 2 disable NTSC burst (B/W), bit 3 video enable, bit 5 **blink-disable (1) → 16 backgrounds** (0 → 8 bg + blink). BIOS default mode-3 = 29h (blink on). To disable blink without BIOS: read BDA 0040:0065h, AND 0DFh, OUT 3D8h, write back to BDA. On EGA/VGA simply use INT 10h AX=1003h BL=00h. **3D9h** color select—bits 0–3 border IRGB in text modes, bit 5 selects the 320×200 palette. **3DAh status**—bit 0 "1 = CPU may safely access video RAM" (during hblank or vblank), bit 3 "1 = vertical retrace active" (~1.25 ms, ≈160 safe bytes at 4.77 MHz).

**CGA snow:** caused by single-ported DRAM in IBM's original discrete 80-column text design. Only 80×25 text (modes 2/3) is affected; 40-col and all graphics modes are properly arbitrated. Mitigation: for per-char write, `wait_no: in dx; test al,01h; jnz wait_no; wait_re: in dx; test al,01h; jz wait_re; stosw` synchronizes to the retrace edge; for bursts, wait for bit 3 (VR) and write up to ≈160 bytes. Alternative: blank screen via 3D8h bit 3=0, write freely, re-enable (visible flash). Clone CGAs with dual-ported VRAM (C&T 82c425/426, Amstrad PC1512, Commodore AGA) have no snow. **Composite monitor**: bit 2 of 3D8h off enables NTSC color burst—artifact-color 16-color palette in 640×200 mono mode. Undocumented 160×100×16 "text-trick": reprogram CRTC to 2-pixel-high cells, fill with char 0xDE, attribute nibbles become two pixels.

### Tier 3 — EGA (IBM, 1984)

**Frame buffer B800:0000** (text), A000:0000 (graphics). Text modes 0/1/2/3 plus 7 (monochrome). **16-of-64 palette with unusual rgbRGB bit layout**: bits 5/4/3 are secondary (1/3-intensity) R'/G'/B', bits 2/1/0 are primary (2/3) R/G/B; per-channel output = primary*2 + secondary gives four levels. 16 Attribute Controller palette registers at port 3C0h indices 00h–0Fh.

**INT 10h AH=10h palette calls:** AL=0 set-single `BL=reg, BH=value(0–3Fh)`; AL=1 overscan `BH=color`; AL=2 set-all-16 `ES:DX→17 bytes (16 regs + overscan)`; **AL=3 blink control `BL=00h enable bg-intensity (16 backgrounds), BL=01h enable blink`**; AL=7 read single palette reg (VGA). **Direct port I/O via ATC flip-flop:** read 3DAh (color) or 3BAh (mono) to reset the flip-flop; OUT 3C0h, index (00h–0Fh palette, 10h mode, 11h overscan, 12h plane-enable, 13h pel-pan, 14h color-select); flip-flop toggles; OUT 3C0h, data; finally OUT 3C0h, 20h to re-enable palette-address-source (unblank screen).

**Fonts via INT 10h AH=11h:** text-mode without mode-reset—AL=0 load-user (BH=bytes/char, BL=block, CX=count, DX=first, ES:BP→table); AL=1 ROM 8×14; AL=2 ROM 8×8 double-dot; AL=3 block-specifier (BL bits 0–1 primary, 2–3 secondary—enables 512-char via attribute bit 3). Text-mode with full CRTC reprogram—AL=10h load user + mode; AL=11h ROM 8×14 + mode (80×25 on EGA); **AL=12h ROM 8×8 + mode → 80×43 on EGA, 80×50 on VGA**. Font-info AX=1130h BH=spec returns ES:BP→table, CX=bytes/char, DL=rows-1. Up to eight 8 KB blocks exist in plane 2; only two live simultaneously. **512-char mode sacrifices bright foreground**—attribute bit 3 becomes bank-select. **UI capability:** reprogram the 16 slots for themed colors; design custom 8×14 glyphs for mini-icons (disk/CPU/memory symbols) and overload CP437 slots 128–255; enable 80×43 via AX=1112h for denser data display; deploy 512-char for bilingual interfaces.

### Tier 4 — VGA (IBM, 1987)

**Frame buffer B800:0000** (text). Mode 3 default is 80×25 with **8×16 ROM font on analog display (400 scanlines: 25 × 16)**. AX=1112h loads 8×8 → 80×50, AX=1114h loads 8×16 (VGA-specific). Scanline selector before mode set: AH=12h BL=30h AL=00h/01h/02h (200/350/400).

**The signature VGA capability is full DAC reprogramming.** 256 DAC entries, each 18-bit RGB (6+6+6). Port 3C6h pel mask (FFh normally), 3C7h read-index or state register (bits 0–1 read/write state), **3C8h write-index**, **3C9h data port (three sequential accesses R,G,B, 0–63 each)**. Example: `OUT 3C8h, n; OUT 3C9h, r; OUT 3C9h, g; OUT 3C9h, b`. BIOS equivalents: AX=1010h set one, AX=1012h set block (BX=first, CX=count, ES:DX→3×CX triplet), AX=1015h read one, AX=1017h read block, AX=1018h/1019h set/get pel mask, AX=101Bh gray-scale sum over a range.

**Text-mode UI tricks:** custom cursor via INT 10h AH=01h CH=start CL=end (bit 5 of CH = hide), or direct CRTC reg 0Ah/0Bh at 3D4h index. Blink-disable AX=1003h BL=00h (or ATC idx 10h bit 3 = 0). Overscan AX=1001h BH=DAC-index. **Palette tricks:** remap 16 text-attribute slots to any 16 DAC entries; write DAC entries 0x11–0x17 as a blue→cyan gradient then paint title bars with ATC→DAC indices 0x11–0x17 in sequence for a **smooth gradient title bar**; palette animation is pure DAC rewriting with zero redraw. Underline via CRTC idx 14h (bits 0–4 = scanline within cell; FFh disables). Font pages: up to 8 simultaneously loaded in plane 2; any two active via AX=1103h (or Sequencer 3C4h idx 03h Character Map Select).

**Text rows available:** 80×25 (default 8×16), 80×28 (approx 8×14 with 400 scanlines), **80×43** (8×8 at 350 scanlines—EGA-style), **80×50** (8×8 at 400 scanlines). CRTC protection: to write CRTC indices 0–7 for 80×50 tweaks, read 3D5h idx 11h, clear bit 7 (Protect 0–7), write back.

### Tier 5 — SVGA / VESA VBE

**Standard VBE text modes:** 108h 80×60; **109h 132×25**; 10Ah 132×43; 10Bh 132×50; 10Ch 132×60. Set via AX=4F02h BX=mode (bit 15 preserve memory). **Detection:** AX=4F00h with ES:DI→256-byte buffer; pre-fill first 4 bytes with ASCII "VBE2" to request VBE 2.0-extended data. Returns AL=4Fh, AH=00h success; verify **first 4 bytes of buffer equal "VESA"**—some broken UniVBE shims return AX=004Fh with garbage signature. VBE version word at offset 04h (0102h=VBE 1.2, 0200h=2.0, 0300h=3.0). Mode-info via AX=4F01h CX=mode → 256-byte ModeInfoBlock (attrs at 00h, bpp at 19h, bytes-per-scanline at 10h, width/height in chars at 12h/14h, cell width/height in pixels at 16h/17h, memory model at 1Bh where 0 = text). AX=4F03h current mode, AX=4F04h save/restore state, AX=4F06h logical scanline, AX=4F07h display start, AX=4F08h DAC palette width.

**Chipset-specific 132-column modes (pre-VBE, c. 1988–1990):** Tseng ET3000/ET4000: 22h/23h 132×44, 26h 132×25, 2Ah 80×60, 2Ch 132×28; Trident TVGA 8800/8900/9000: 50h/51h/52h 80×30/43/60, 53h/54h/55h/56h 132×25/30/43/60 (detection via AH=12h BL=11h); Paradise/WD PVGA1A/WD90C: 54h 132×43, 55h 132×25 (unlock port 3CEh idx 0Fh=05h); Cirrus Logic CL-GD5xxx: 14h/54h 132×25, 55h 132×43 (unlock SR06=12h); S3 86Cxxx: 54h 132×43, 55h 132×25, 56h 132×50, 57h 132×60 (unlock CRTC idx 38h=48h, 39h=A5h, read chip-ID at CRTC 30h); ATI VGA Wonder/Mach: 23h 132×25, 33h 132×44 (signature "761295520" at C000:0031h); Oak OTI-037/067/077: 50h/51h/52h. **UNIVBE** (SciTech) abstracts all as VBE 2.0. 132-column modes commonly require ≥512 KB VRAM; 256 KB cards fail on 10Ch 132×60. Custom fonts and DAC reprogramming continue to work in VESA text modes.

### Adapter detection waterfall

Probe most-capable first. **Step 1 (VGA/VESA) via INT 10h AH=1Ah (VGA-only; returns AL=1Ah only on VGA/MCGA BIOS).** If present, inspect DCC in BL (01 MDA, 02 CGA, 04/05 EGA color/mono, 07/08 VGA mono/color, 0A/0B/0C MCGA variants). Probe VESA with pre-filled "VBE2" buffer; require both AX=004Fh and literal "VESA" signature at offset 0. **Step 2 (EGA) via INT 10h AH=12h BL=10h with BH=FFh sentinel.** If BH changes, EGA is present (BH=0 color, BH=1 mono; BL=memory size 0–3 for 64/128/192/256 KB; CH feature bits; CL switch settings). **Step 3 (CGA vs mono-class) via INT 11h equipment word bits 5–4:** 01b 40×25 CGA, 10b 80×25 CGA, 11b mono (MDA or Herc). **Step 4 (Hercules vs MDA):** poll port 3BAh bit 7 for ~32 768 iterations; toggle ⇒ Hercules-class; constant ⇒ MDA. Read bits 6–4 for Herc variant. Supplementary: INT 10h AH=0Fh current mode/columns/page; AX=1200h BL=32h VGA-only enable CPU video access; INT 10h AH=1Bh VGA Return Functionality State. BDA 0040:0063h holds the active CRTC base (3B4h mono, 3D4h color).

### Enhancement tier capability matrix

| Feature | MDA | HGC | HGC+ | InColor | CGA | EGA | VGA | SVGA |
|---|---|---|---|---|---|---|---|---|
| Colors in text | 9 distinct | same | same | 16 | 16 fg / 8–16 bg | 16-of-64 | 16-of-262 144 | same as VGA |
| Custom palette | — | — | — | yes (CRTC 1Ch) | — | yes (ATC) | yes (18-bit DAC) | yes |
| Custom font | no | no | yes (3072 chars) | yes (3072 color) | no | yes (512 chars) | yes (512 chars, 8 banks) | yes |
| 80×43 | no | no | yes (90×25 via CRTC 14h) | yes | no | yes (AX=1112h) | yes | yes |
| 80×50 | no | no | no | no | no | no | yes (AX=1112h on 400-line) | yes |
| 132-column | no | no | no | no | no | no | no | yes (VBE 109h–10Ch) |
| Blink-disable ⇒ 16 bg | — | — | — | yes | yes (3D8h bit 5) | yes (AX=1003h) | yes | yes |
| Custom cursor shape | CRTC 0A/0B | yes | yes | yes | CRTC 0A/0B | yes | yes (bit-addressable) | yes |
| Snow-free writes | yes | yes | yes | yes | no—gate on 3DAh | yes | yes | yes |
| Gradient title bars | no | no | no | limited | no | limited (4 levels/channel) | yes (18-bit DAC) | yes |

### Concrete enhancement flow once tier is known

1. Detect tier via the waterfall above. 2. Save state (VGA: AX=1C00h query, then AX=1C01h save). 3. Set base text mode (03h, or VBE 109h+ on SVGA). 4. Load custom font (AX=1110h/1111h/1112h/1114h, or 1100h + 1103h for dual-bank 512 chars). 5. Disable blink (AX=1003h BL=00h) for 16 backgrounds. 6. On VGA, reprogram DAC entries 0–15 via 3C8h/3C9h for theme/gradient. 7. Set cursor shape via AH=01h matching cell height. 8. On exit, restore (AX=1C02h or reverse-DAC-then-reload-default-font via AX=1114h).

---

## Part C — Application archetype templates

### Terminal and communications apps

**Canonical layout:** 24-row terminal/scrollback area + single status line at row 25. **ProComm Plus 2.x** status line fields: `| Alt-Z Help | VT102 | FDX | 2400 N81 | LOG CLOSED | PRINT OFF | OFFLINE |`. **Telix 3.22** status line: `| Alt-Z HELP | VT102 | 2400 8N1 | FDX | CP LOG | PR OFF | Script: NONE | Off-Line |` showing time/date, connect, elapsed, comm params, capture/printer state, script, directory, macro file. Color: normal black-bg white text; status line bright white on cyan or blue; popups gray with double-line borders; error dialogs red background; dialing directory cyan panel with yellow selection bar.

**Alt-key chord convention (universal):** Alt-Z help, Alt-D dial, Alt-P params, Alt-S send/setup, Alt-R receive, Alt-G ASCII send, Alt-C clear, Alt-X exit, Alt-F file dir, Alt-K/L macros, Alt-T translate, Alt-Y host, Alt-8 toggle status line (Telix), PgUp/PgDn upload/download. **Dialing directory** is a full-screen tabular dialog with name/phone/baud/parity/terminal/script/last-called columns; Space marks for redial-ring queue; Enter on marked set opens a rotating redial popup showing current entry, attempt count, timer, skip key. **File transfer dialog** overlays an ~18×60 window: filename, size, bytes received, block size, CRC 32/16, CPS, efficiency, elapsed, ETA, errors, last message, progress bar drawn with 219 █/176 ░. Zmodem is auto-triggered by the `**\x18B00` sequence from the remote, opening the window without user input. Scrollback is a circular buffer in conventional/EMS/XMS memory holding rows of character+attribute pairs so ANSI colors survive review; Alt-F3/Alt-B enters scrollback viewer with PgUp/PgDn/Home/End and Alt-F find. Emulations typically include TTY, VT52, VT102, ANSI-BBS (X3.64), plus IBM 3101, TeleVideo 910/920/925/950/955, Wyse 50/100, Heath-Zenith 19, ADDS Viewpoint 60, ADM3/5, IBM 3270; Telix adds AVATAR. Scripts: Telix SALT (.SLT → .SLC), ProComm ASPECT (.ASP → .ASX), invoked via Alt-G, command line, or dialing-directory entry.

**Exemplars:** Telix 3.15/3.21/3.22/3.51, ProComm/ProComm Plus 1.x/2.0x/2.5, Qmodem/Qmodem Pro, **Telemate** (notable for using EMS/XMS and offering stacked-window multi-pane with dial/editor/terminal/scrollback), Boyan, TERM (Crosstalk), Kermit.

### File management

Two paradigms coexisted. **Norton-style dual-pane (the "orthodox file manager"):** two double-framed panels with the standard F-key legend at row 25 and a command line above it. Active panel signaled by reverse-video title and selection bar. Panel display modes via `Left`/`Right` pulldown: brief (three columns of filenames), full (name/size/date/time single column), info (drive stats plus DIRINFO), tree (hierarchical), quick-view (Ctrl+Q puts live preview in opposite panel with LIST.COM-style text viewer, hex for binary, embedded viewers for DBF/DOC in v5). Archive browsing: pressing Enter on a .ZIP/.ARJ/.LZH/.RAR/.TAR descends into the archive transparently, with F5 as implicit pack/unpack. Color: panels bright white/yellow on blue, borders cyan; menus black on cyan; dialogs black on light-gray; errors white on red; F-key legend alternates black-digit-on-cyan with white-label-on-black.

**XTree-style three-region:** rows 1–3 path/stats; rows 4–18 tree (left) + files (right); rows 19–22 statistics pane (logged/matching/tagged/bytes); rows 23–25 **single-letter command menu** whose capitalized first letter is the accelerator (`Available Copy Delete Filespec Invert Log Move Print Rename Tag Untag eXecute Batch`). Alt and Ctrl modifier-keys switch to secondary/tertiary command banks (Alt = operate on tagged across branches preserving path; Ctrl = operate on all tagged). Distinctive features: tag/untag across entire branches via Ctrl+T; Showall/Global mode lists every file on disk sorted; F3 viewer has text/hex-split/DBF/WK1 modes; XTreePro Gold's Split View shows two independent tree+file regions for different drives.

**DOS Shell (DOSSHELL.EXE, MS-DOS 4.0–6.22):** **first DOS file manager with a true menu bar on row 1** (File/Options/View/Tree/Help), drive-letter icons on row 2, tree + file-list on rows 3–12, Program List / Active Task List on rows 13–22, F-key hints on row 24. Task Swapper (enabled via Options) uses DOSSWAP.EXE, consumes ~35.4 KB conventional memory, supports up to 13 concurrent tasks swapped to %TEMP%; Shift+Enter adds without running; Alt+Tab/Ctrl+Esc cycle. Separate .GRB files per adapter (CGA.GR_/EGA.GR_/VGA.GR_/HERC.GR_) allow character-mode or 640×480 "text-in-graphics."

**Defining keys (NC):** Tab swap, Enter change dir/run, Ins tag, Gray +/−/\* select/unselect/invert, Ctrl+O hide panels, Ctrl+U swap, Ctrl+R refresh, Ctrl+L/Q info/quickview, Ctrl+F1/F2 hide panel, Alt+F1/F2 drive select, Alt+F7 find, Alt+F10 tree navigator, Ctrl+\\ root, Ctrl+Enter filename-to-prompt.

### Database and record browsers

Two canonical screen modes share the archetype. **Browse (tabular grid):** records as rows, fields as columns; `BROWSE` in xBase, Table view in Paradox. Highlighted row = current record pointer; Left/Right pans fields that don't fit in 80 cols; Ctrl+Left/Right jumps by field; **`LOCK n` freezes n leftmost columns as key-fields during horizontal pan**; `WIDTH n` truncates char fields; `FREEZE <field>` restricts editing to one column while showing context. **Edit (form view):** one field per line with labels, PgUp/PgDn moves records (not screens), Ctrl+Home opens memo-field popup (full-screen editor ≈70×20, WordStar diamond keys in dBASE; Ctrl+End/Ctrl+W save+exit, Esc cancel). **Custom Form (.FMT):** built with the screen painter, arbitrary labels and field boxes drawn in `[...]` or reverse-video cells, saved as .FMT source + .FMO compiled.

**Status bar universal convention (dBASE `SET STATUS ON`):** two-row panel showing work-area letter, open table, `Rec 4/1523`, file/edit state, lock, Caps/Ins/Num mirror, field name at cursor, field type/offset, optional message line (`SET MESSAGE TO`). Optional scoreboard row-1 (`SET SCOREBOARD ON`). Navigation: ↑/↓ record or field; PgUp/PgDn record (Edit) or page (Browse); Tab/Shift+Tab field; Ctrl+Home/Ctrl+End top/bottom or memo/save-exit; Ctrl+W save+exit; Esc abandon; Ctrl+U mark-delete; Ctrl+N insert; F10 menu (dBASE IV Control Center, FoxPro); Shift+F9 Quick Report.

**dBASE IV/V Control Center:** six vertical list-box "panels" side by side—Data / Queries / Forms / Reports / Labels / Applications—each with `<create>` marker atop. Tab/arrows move between panels; F2 opens selected table in Browse/Edit (toggles with F2 again); F10 activates menu bar across the top; Exit returns to the dot prompt (`.USE`, `.INDEX`, `.BROWSE`…); `ASSIST` reverses. **Paradox 3.5/4.5** is distinctive for its **true windowing workspace**: F10 main menu (View/Ask/Report/Create/Modify/Image/Forms/Tools/Scripts/Help/Exit); each opened table is a floating movable/resizable window; F7 toggles form↔table; F9 toggles view↔edit. **Query By Example (QBE)** is Paradox's signature: Ask + table opens an empty table skeleton; F6 places checkmarks in columns (include in ANSWER); literals or operators in cells filter; **example elements** (F5-prefixed identifier) in two tables form inner joins on the repeated token; `CALC`/`AVERAGE`/`GROUPBY` cells aggregate; `CHANGETO`/`INSERT`/`DELETE` keywords at row-left make update-queries; F2 runs, producing a fully materialized ANSWER table window.

**Color:** workspace blue (dBASE IV) or cyan (Paradox); menu bar black-on-white; input fields reverse-video; navigation line yellow on blue; delete-marked records dim; errors white on red.

### Text editors and word processors

Three UI schools coexisted throughout the decade. **Blank-screen (WordPerfect 5.1):** the screen is almost entirely document; one visible widget—right-justified `Doc 1 Pg 1 Ln 1" Pos 1"` at row 25 (inches notation, verbatim). Commands strictly function-key driven (F1 Cancel, F3 Help, F7 Exit, F10 Save). **Reveal Codes (Alt+F3, or F11 on enhanced keyboards)** splits the screen with normal document view on rows 1–11, a CP437 ═ divider on row 12, and a codes pane on rows 13–24 showing `[HRt][Tab]The quick [BOLD]brown[bold] fox.[HRt]` with cursor mirrored in both panes—codes are double-clickable to edit. **Control-band (WordStar 4–7):** top help-band (toggleable through levels 0–3 via Ctrl+J) plus ruler bar on row 9 (`L----!----!----R`) plus status row 24 (`B:FILE.TXT PAGE 1 LINE 12 COL 8 INSERT ON`). The **WordStar diamond** (^E/X/S/D cursor + outer ring ^A/F/R/C) prefixed by menu letters: **^K block** (^KB begin, ^KK end, ^KC copy, ^KV move, ^KS save), **^Q quick** (^QR top, ^QC end), **^O onscreen**, **^P print** (^PB bold, ^PS underline, ^PY italic), **^J help-level toggle**. Dot commands at column 1 (`.cp 10`, `.pa`, `.mt 5`, `.lh 8`, `.op`, `.fi FILE`, `.AV`). **CUA pulldown school (Word 5.5 DOS, 1991):** File/Edit/View/Insert/Format/Tools/Utilities/Macro/Window/Help menu bar; scrollbars drawn with 177 ▒/178 ▓ thumbs; up to 8 document windows cycled via Ctrl+F6; outline mode collapses headings; `{REC}` indicator on status shows macro recording.

**Borland IDE editor** (Turbo Pascal 5.5–7, Turbo C++, BC++, also copied by QuickBASIC/QuickC): row 1 menu, tiled/overlapping windows with double-line border when active (╔═╗) vs single when inactive (┌─┐), `[■]` close gadget top-left, `[↑]` zoom top-right, Ctrl+F5 size/move, message window for compile errors, row 25 F-key legend. Defining keys: **F1 Help, F2 Save, F3 Open**, F4 Run-to-cursor, F5 Zoom, F6 next window, F7 step-into, F8 step-over, F9 Make, F10 Menu, Alt+F3 close, Alt+F9 Compile, Ctrl+F9 Run, Alt+0 Window list, Alt+F5 User screen, Alt+X Exit. Block operations still use WordStar keys (^K B/K/C/V/Y). Syntax highlighting (TP 7 / TC++ 3.0 +): keywords bright white, identifiers yellow, comments gray, strings light-cyan, errors red on black, all over the hatched blue desktop.

**Exemplars per school:** blank-screen—WordPerfect 4.2/5.0/5.1/5.1+/6.0; control-diamond—WordStar 3.0/4/5/6/7.0d (last DOS 1992), VDE, jstar/Joe, WordTsar; CUA-pulldown—Word 5.0/5.5/6.0 DOS, Sprint, DeScribe; programmer editors—Borland IDE, Brief (1985), QEdit/TSE, MultiEdit (10-window tiling with column-block select), EDIT.COM (MS-DOS 5+, QBasic-derived); markup/typesetter—XyWrite III/III+/IV, TeX frontends.

### System utilities and configuration tools

Three sub-patterns. **Tabbed/paged category browser** (BIOS setup, DOOM SETUP, MSD, Manifest): Award-style vertical list of category tiles (Standard CMOS / BIOS Features / Chipset / Power / PnP-PCI / Integrated Peripherals / Load Defaults / Save & Exit) vs AMI-style horizontal top-tab bar (Main / Advanced / Chipset / Power / PnP-PCI / Peripherals / Boot / Exit). Within a page, left column lists settings, right column shows context help ("Menu Level ►"). **Value-stepping with immediate feedback**—no text entry, values cycle through a fixed list via **PgUp/PgDn or +/−**; commit only on F10 Save & Exit. Award color: saturated blue bg, cyan labels, white values, yellow selection bar, red warnings. Phoenix: gray-on-gray with inverse-video blue selection bar. **F1 Help, F10 Save & Exit, Esc back** are universal; Award also has F2 "Change Color," F7 "Load Optimized Defaults."

**DOOM SETUP.EXE** (Laughing Dog Screen Maker engine, inherited from Raptor's setup by Paul Radek, rewritten by John Romero): main menu with Sound Card / Music Card / Controls / Keyboard / Mouse / Joystick (calibrate) / Network / Save-and-launch / Save-and-exit / Exit. Arrow keys, Enter, Esc. **v1.5 used a "funky" darker blue palette**; v1.7+ reverted to EGA default blue. Each sub-page shows card brand list (SB, SB Pro, SB16, Gravis UltraSound, AdLib, PAS, PC Speaker), IRQ dropdown (5/7/10), DMA dropdown (1/5), I/O Port (220h/240h), plus a Test button that plays a sample. **Sound Blaster DIAGNOSE.EXE** runs three-phase detection ("Detecting Sound Blaster at I/O 220h… OK; IRQ… 5 OK; DMA… 1 OK") with bright green OK / bright red FAIL, concluding with a `SET BLASTER=...` line for AUTOEXEC.BAT.

**MSD.EXE** (Microsoft Diagnostics) v2+: top menu bar File/Utilities/Help; nine "category tiles" (Computer, Memory, Video, Network, OS Version, Mouse, Disk Drives, LPT Ports, COM Ports, IRQ Status, TSR Programs, Device Drivers)—press the highlighted letter or mouse-click to open a draggable pop-up with `[■]` close corner. **MemMaker (DOS 6.0/6.22):** wizard cards with Space to toggle choices in-place (Express / Custom underlined and cycling), Enter accept and advance, F3 abort; after analysis reboots to test new CONFIG.SYS/AUTOEXEC.BAT, returns with a before/after conventional-memory table; `/UNDO` restores. **ScanDisk/DEFRAG** share the cluster grid with ▓ used / ░ free / B bad / r reading / W writing plus legend and percent bar. Unified color scheme across MSD, ScanDisk, DEFRAG, EDIT.COM, QBASIC, MemMaker, Norton family: stippled blue desktop, gray menu bar with black text and highlighted hotkey letters, cyan dialogs, yellow selection bar, white-on-red errors. `/B` switch forces monochrome everywhere.

### Email and messaging

**Three-pane layout (GoldED/GoldED+, the Fidonet editor that anticipated Outlook):** area list left, message list top right, message body bottom right, with synced cursors. Keys: R reply, E enter new, C change area, Del delete, / search, Alt+J jump, Space/PgDn next page, B back, Shift+F10 tagline menu, Alt+Q re-wrap quote, Alt+H edit headers. **Two-pane master-detail (Pegasus Mail for DOS, v3.50 last 1999):** folder list + main area with messages as `# U  J.Smith  Budget update  12 May 95  2,104` rows; flags U unread, R replied, F forwarded, A attachment, \* marked. Compose is modal full-screen with To/CC/Subject fields and body. **Modal single-pane (1stReader—the original QWK reader by Sparky Herring, April 1988; SLMR by Greg Hewgill; OLX; Blue Wave):** chained full-screens pushed/popped via Esc—packet list → area/conference list → message list → one-message view → reply editor. Five universal keys: Space/PgDn next-page-then-next-message, B/PgUp back, N/P next/prev, R reply, F forward, D delete (toggle), S save, E enter new, A address book, / or F7 search, Tab next field in compose, Ctrl+Z / F10 send, Esc back, F1 help, Alt+X/Q quit.

**Color convention (near universal):** blue/cyan desktop; bright white unread; gray/dim read; yellow/bright for personal; green replied; red private/deleted; selection bar yellow-on-blue or white-on-magenta. **Packet-oriented workflow unique to DOS mail:** download .QWK/.BW via a terminal → disconnect → read/reply offline → save reply packet → reconnect → upload. **Blue Wave popularized rotating taglines** inserted at end of each reply, persisted in GoldED and SLMR. **Pegasus Mail was the first DOS mailer with content-based filter rules**—a feature missing from nearly every QWK reader.

---

## Conclusion — a unified design grammar across adapters and decades

The DOS text-mode UI universe reduces to three orthogonal axes. On the **visual axis**, the adapter tier gates which attribute tricks you can deploy: monochrome underline on MDA, custom fonts and 512-char banks on EGA, 18-bit DAC gradient title bars on VGA, 132-column data tables on SVGA. On the **interaction axis**, IBM CUA/SAA 1989/1991 standardized the keyboarding (F1/F3/F10/Shift+F10/Alt+Space/Alt+letter/Tab), WordStar's control diamond survived as a second layer in virtually every Borland IDE, and Norton Commander's F1–F10 bottom legend became the orthodox file-manager signature. On the **structural axis**, six archetypes (terminal, file manager, database, editor/word processor, system utility, mail client) each have canonical band layouts—top menu or hidden, main area pattern, bottom status or F-key—assembled from a compact widget vocabulary (scrollbars, trees, splits, tabs, wizards, grids, combos, memos, progress meters, alerts, context menus, toolbars, status bars, toasts) whose glyphs are drawn from a fixed CP437 palette.

**The single highest-leverage insight for a modern DOS-TUI author:** detect the adapter tier once at startup via the `1Ah → 1210h → INT 11h → 3BAh-toggle` waterfall, then fork only the *styling* code—never the structural/interaction code. A well-written app using the widget catalog above will look identical in structure on MDA and VGA; only the palette depth, font fidelity, and grid density shift. Second-highest: **respect the three conventions your users already know**—Borland blue/gray visual rhetoric, CUA/SAA keys, Norton F1–F10 bottom legend—because forty years of DOS users formed habits around them and a fourth convention is a usability tax you cannot afford to levy. Third: the gap between a good and a great DOS TUI is almost entirely in the details of **adapter-specific polish**—INT 10h AX=1003h BL=00h for 16 background colors, AX=1112h for 80×50 on VGA, DAC reprogramming for themed gradients, VBE 109h for 132-column data grids, and snow-avoidance on CGA—none of which require framework changes, all of which dramatically raise perceived quality. The reference above gives the exact register values, glyph codes, and class names to deploy each one.