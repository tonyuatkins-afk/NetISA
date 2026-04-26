# HEARO vs Mpxplay: targeted comparison

Notes from a side by side read of HEARO (`hearo/src/audio/sb.c`,
`hearo/src/audio/dma.c`) against Mpxplay v3.25 (`AU_CARDS/SC_SB16.C`,
`AU_CARDS/SC_SBPRO.C`, `AU_CARDS/DMAIRQ.C`, `MPXPLAY.C`). Citations are
file paths and line numbers; the line numbers are accurate as of
2026-04-26 in both trees.

The intent is not "who is right". The two players solve different
problems on different platforms (HEARO is real-mode 16-bit Watcom
targeting 286+ ISA hardware; Mpxplay is a DOS/4GW or DOS/32A protected
mode app on 386+, with PCI codecs and modern stacks in scope). The
question is: where can one borrow patterns from the other.

## 1. Close sequence: `sb_close` vs `SB16_stop` / `SB_stop`

**HEARO** `hearo/src/audio/sb.c:695-860` (sb_close, 165 lines):

| Step | What happens | Why |
|------|--------------|-----|
| 1   | `S.running = 0; S.cb = 0;` (line 723-724) | Kill the ISR fast path before any teardown I/O. ISR re-checks `running` after the gate; without writing it first, an in-flight ISR could still call cb on freed state. |
| 1b  | Spin until `isr_in_progress` clears, bounded ~0x1000 iters (line 737-742) | Mpxplay does not do this. Catches the case where an ISR was already past the running gate when foreground hit step 1; without it, foreground `dsp_reset` can preempt the ISR's `dsp_write` triple and latch half a command into the DSP. |
| 2   | `dma_disable(active_dma)` if armed (line 747-751) | Stop DMA cold so the chip cannot pull more bytes from the buffer about to be freed. Snapshot channel for step 5b. |
| 3   | Mask SB IRQ at PIC (line 755-758) | Close the window before `dsp_reset`. `uninstall_isr` will mask too, but doing it now means a pending edge cannot fire into a half-reset DSP. |
| 4   | DSP halt + reset, branched on high-speed (line 770-786) | High-speed mode: skip 0xD0/0xDA halts (they are ignored, and a single `dsp_write` burns ~64K port reads on a wedged chip), issue two `dsp_reset` calls. Normal mode: 0xD0/0xD5 halt, 0xDA exit auto-init, 0xD3 speaker off, then reset. All halt commands use `dsp_write_to(..., 256)` not `dsp_write` so a stuck chip cannot stall close. |
| 4b  | Restore SB Pro mixer reg 0x0E (line 794-803) | If sb_open touched the stereo bit, restore the original byte so a mono Adlib title launched after HEARO does not inherit stereo. Port-0x80 settle delay between index and data writes. |
| 5   | `uninstall_isr()` if hooked (line 806) | Unhook vector + restore PIC mask state. |
| 5b  | Defeat lingering DREQ before MCB walk (line 847-853) | Under `cli`: dma_disable, dma_wait_quiescent (count register stable across two reads), dma_disable again. Belt and suspenders; the second mask write covers the case where step 1's mask was racing with a DACK landing. **Crucial detail**: this used to call `dma_master_clear()` which masks all 8 DMA channels including DMA 2 (floppy) and DMA 0 (refresh on XT class boards). On a floppy boot box, that hung COMMAND.COM at exit. Per-channel `dma_disable` is sufficient because the chip's DREQ generator is already stopped by step 4's reset. |
| 6   | `dma_free(buffer)` last (line 859) | Release the DMA buffer only after every path that could write to it is shut down. The MCB walk inside INT 21h AH=49h cannot collide with a residual DMA cycle by this point. |

**Mpxplay** `AU_CARDS/SC_SB16.C:232-244` (SB16_stop, 13 lines):

```c
static void SB16_stop(struct mpxplay_audioout_info_s *aui)
{
 unsigned int baseport=aui->card_port;
 SB16_writeDSP(baseport,SB_DSP_DMA16_OFF);   // 0xD5
 SB16_writeDSP(baseport,SB_DSP_DMA16_EXIT);  // 0xD9
 SB16_writeDSP(baseport,SB_DSP_DMA16_OFF);   // 0xD5 again
 SB16_resetDSP(baseport);
 SB16_resetDSP(baseport);
 SB16_resetDSP(baseport);
 MDma_ISA_Stop(aui);
 MIrq_Stop(aui->card_irq,&aui->card_infobits);
 //inp(SB16_DATA_AVAIL_PORT);
}
```

`SB_stop` for SB Pro / 1.x at `SC_SBPRO.C:231-240` is even shorter: three
`SB_resetDSP` calls in a row, then `MIrq_Stop`, then `MDma_ISA_Stop`.

### Side by side observations

| Concern | HEARO | Mpxplay |
|---------|-------|---------|
| ISR gate flag set before teardown | yes (running + isr_in_progress, line 723-742) | no, relies on `MIrq_Stop` |
| Wait for in-flight ISR to drain | yes (line 737-742) | no |
| DSP write timeout on halt commands | yes, tight 256 (line 778-785) | no, full 65535 timeout (`SB16_writeDSP` body) |
| Skip halt path in high-speed | yes (line 770-775) | no, blindly writes 0xD5/0xD9/0xD5 even in high-speed |
| DMA stop ordered before DSP reset | yes (step 2 then 4) | reverse: DSP reset first, then `MDma_ISA_Stop` |
| Verify DMA quiescence with double count read | yes (line 850, `dma_wait_quiescent`) | no |
| PIC mask before DSP teardown | yes (step 3) | implicit through `MIrq_Stop` order |
| SB Pro mixer reg restore | yes (line 794-803) | no, leaves stereo bit set after stop |
| Port-0x80 settle delays | yes around mixer index/data | no, back-to-back outp |
| Triple `dsp_reset` | only in high-speed branch | always (3 in a row) |

### Takeaways

- HEARO's order (DMA stop, then PIC mask, then DSP reset, then unhook,
  then verify quiescence, then free) prevents a class of late-DREQ
  bugs that the Mpxplay sequence cannot catch on a wedged chip. The
  `dma_wait_quiescent` step is HEARO's unique contribution; nothing
  similar exists in Mpxplay.
- Mpxplay's three-reset hammer is reasonable for a player that runs
  on known-good hardware; on the YMF715 in MS-DOS mode without vendor
  init, it would have hung the DSP between the writes.
- The SB Pro mixer reg 0x0E restore in HEARO is a real bug Mpxplay
  has had since 1998. Launching a mono OPL game after Mpxplay leaves
  the stereo bit set; the next program inherits it. HEARO restores.
- Mpxplay's brevity is partly because it can: in a protected mode
  player with DPMI handling allocator state, much of HEARO's
  complexity around MCB walks and stuck DREQs simply does not apply.

---

## 2. DMA allocation: `dma_alloc` vs `MDma_ISA_AllocMem`

**HEARO** `hearo/src/audio/dma.c:43-191` (dma_alloc, 149 lines):

Behaviors:

1. **Hard cap at boundary** (line 61). 8-bit caps at 0x10000, 16-bit
   at 0x20000. Caller bug if larger.
2. **Slot table for tracking** (line 65-68). 4 simultaneous DMA
   allocations max; refuse rather than leak.
3. **Round up to whole paragraphs with slack** (line 71-73). Allocates
   `size + boundary` worst case so the bumped pointer always fits.
4. **Force allocation strategy 00h** (line 97-109). INT 21h AH=58h
   AL=01h BX=0 sets first-fit, low-memory-only. Saves the prior
   strategy in line 100-102 so it can be restored. Without this,
   under DOS=HIGH,UMB with EMM386, the kernel may legally hand out a
   UMB region (above A0000h). The page register can address those
   bytes, but EMM386 maps them as paged windows backed by extended
   memory shared with EMS handles owned by other programs; a DMA
   write into a UMB scrambles whatever EMS handle currently maps that
   page frame.
5. **Allocate via INT 21h AH=48h direct** (line 111-126). Bypasses
   Watcom's `_dos_allocmem` because its prototype is overloaded and
   the wrong overload selection writes only 2 bytes. Direct int86
   sidesteps the ambiguity.
6. **Restore prior strategy** after the alloc (line 131-136), success
   or failure, so subsequent malloc calls get the strategy they
   expected.
7. **Boundary cross check + bump** (line 138-158). Computes whether
   `(phys + size - 1) & ~(boundary-1)` matches `phys & ~(boundary-1)`.
   If it crosses, bumps to next boundary. **Computes bumped segment
   in 32 bits** (line 148) and rejects overflow past 1 MiB segment
   range; a near-1 MiB allocation could otherwise wrap a u16 segment
   add and hand back a UMB-region pointer.
8. **Belt-and-suspenders A0000 reject** (line 177-183). Even with
   strategy forced, on broken UMB stacks (old QEMM, weird drivers)
   the kernel can still honor from a high block. Reject any alloc
   whose end falls at or above A0000h, free it, fail.
9. **dma_free maps pointer back to slot** (line 193-212). Walks the
   slot table to find the original `raw_seg` and frees via INT 21h
   AH=49h.

**Mpxplay** `AU_CARDS/DMAIRQ.C:64-85, 214-231` (MDma_alloc_dosmem +
MDma_ISA_AllocMem, ~30 lines):

```c
struct dosmem_t *MDma_alloc_dosmem(unsigned int buffsize)
{
 struct dosmem_t *dm;
 dm=calloc(1,sizeof(dosmem_t));
 if(!dm) mpxplay_close_program(MPXERROR_XMS_MEM);
 if(!pds_dpmi_dos_allocmem(dm,buffsize)){
  free(dm);
  mpxplay_close_program(MPXERROR_CONVENTIONAL_MEM);
 }
 memset(dm->linearptr,0,buffsize);
 return dm;
}

static void MDma_ISA_AllocMem(struct mpxplay_audioout_info_s *aui)
{
 dosmem_t *dm;
 unsigned long p;

 aui->card_dma_buffer_size = MDma_get_max_pcmoutbufsize(aui,
   MDMA_ISA_DMABUFSIZE_MAX, MDMA_ISA_BLOCKSIZE, 2, 0);

 dm = MDma_alloc_dosmem(aui->card_dma_buffer_size * 2);

 p = (unsigned long)dm->linearptr;
 if(((p&0xffff) + aui->card_dma_buffer_size) > 65535)
  p += (65536 - (p&0xffff));
 dm->linearptr = (char *)p;

 aui->card_DMABUFF = (char *)p;
}
```

Behaviors:

1. **Allocate 2x the needed buffer** (line 221), then bump within the
   block if the first half would cross 64K. The unused tail acts as
   alignment slack.
2. **Boundary check is `(p&0xFFFF) + size > 65535`** (line 224). Only
   checks 64K page; relies on the fact that even 16-bit DMA, which
   needs 128K alignment, trivially fits inside 128K when the buffer
   is 64K-aligned and 64K or smaller.
3. **Delegates to `pds_dpmi_dos_allocmem`** (line 70). This is a DPMI
   wrapper, INT 31h function 0100h "Allocate DOS Memory Block". The
   DOS extender (DOS/4GW, DOS/32A) handles the segment selector setup.
4. **No allocation strategy manipulation**. Relies on DOS extender
   defaults.
5. **No A0000 reject**. Trusts the extender to return conventional
   memory.
6. **Unconditional fatal error on alloc failure** (line 71, 73). The
   wrapper kills the program rather than returning failure to caller.

### Side by side

| Concern | HEARO | Mpxplay |
|---------|-------|---------|
| Boundary check formula | works for any boundary (8-bit 64K, 16-bit 128K, future 256K) | hardcoded 64K, equivalent in practice but not generalizable |
| Forces alloc strategy to first-fit-low | yes (line 97-109) | no, trusts extender |
| Restores prior strategy | yes (line 131-136) | n/a |
| A0000 explicit reject | yes (line 177-183) | no |
| Segment overflow check on bump | yes (line 148-149) | not relevant in protected mode |
| Failure mode | returns 0, caller decides | calls `mpxplay_close_program(MPXERROR_*)`, kills program |
| DPMI vs raw INT 21h | raw INT 21h AH=48h via int86 | DPMI INT 31h function 0100h |
| Tracks allocations for free | yes (slot table) | yes (in `dosmem_t` struct) |

### Takeaways

- HEARO's UMB rejection is one of the few places where real-mode
  Watcom code has to be more paranoid than protected mode. Under
  UMB-aware DOS, the kernel will hand out high blocks unless told
  not to. The strategy 00h force is mandatory; the A0000 belt is
  cheap insurance.
- Mpxplay can be sloppy here because DPMI's allocate-DOS-memory
  call is bounded by the extender, which on DOS/4GW and DOS/32A
  defaults to conventional memory. HEARO has no such floor.
- Mpxplay's "allocate 2x and bump" trick is simpler than HEARO's
  "compute exact slack and round up" logic, at the cost of wasting
  up to one buffer-size of conventional memory. On a 286 with
  640K conventional, that matters; in DOS/4GW with 32 MB virtual,
  it does not.
- HEARO's overflow check on the bumped segment (line 148-149) is
  defensive against an attacker controlling buffer size; Mpxplay
  does not need it.

---

## 3. ISR structure: `sb_isr` vs `SB16_irq_routine` / `SB1_irq_routine`

**HEARO** `hearo/src/audio/sb.c:185-290` (sb_isr, 106 lines):

```c
static void __interrupt __far sb_isr(void)
{
    /* 1. Snapshot teardown gate. */
    if (!S.running) {
        if (S.irq >= 8) outp(0xA0, 0x20);
        outp(0x20, 0x20);
        return;
    }

    /* 2. Mark in-progress so foreground sb_close can wait. */
    S.isr_in_progress = 1;

    /* 3. Mask own IRQ at PIC for ISR body duration. */
    mask_bit  = irq_pic_mask_bit(S.irq);
    mask_port = (S.irq < 8) ? 0x21 : 0xA1;
    outp(mask_port, inp(mask_port) | mask_bit);

    /* 4. Acknowledge SB at +0xE (8-bit) or +0xF (16-bit). */
    if (S.format == AFMT_S16_MONO || S.format == AFMT_S16_STEREO)
        (void)inp(S.base + DSP_ACK16);
    else
        (void)inp(S.base + DSP_RSTATUS);

    /* 5. EOI to PIC immediately so lower-priority IRQs unblock
     *    while audio callback runs. */
    if (S.irq >= 8) outp(0xA0, 0x20);
    outp(0x20, 0x20);

    /* 6. Re-check cb after enabling interrupts conceptually. */
    cb = S.cb;
    if (!cb) { ... unmask, clear in_progress, return; }

    /* 7. Refill the half DSP just released, BEFORE toggling. */
    idle = S.active_half;
    halfp = (u8 far *)S.buffer + ((u32)idle * S.half_bytes);
    cb(halfp, S.half_frames, S.format);
    S.active_half ^= 1;

    /* 8. Re-check teardown after callback. */
    if (!S.running) { ... unmask, clear in_progress, return; }

    /* 9. SB 1.x / single-cycle: re-arm 0x14 or 0x91 with TIGHT
     *    timeout. On failure, abort-reset the DSP via raw outp
     *    to DSP_RESET so the foreground reset can re-handshake. */
    if (S.dsp_major < 2 || S.force_single_cycle) {
        u8 sc_cmd = S.in_high_speed ? 0x91 : 0x14;
        if (!dsp_write_to(S.base, sc_cmd, 256) ||
            !dsp_write_to(S.base, lo, 256) ||
            !dsp_write_to(S.base, hi, 256)) {
            outp(S.base + DSP_RESET, 1);
            S.running = 0;
        }
    }

    /* 10. Unmask before IRET. */
    outp(mask_port, inp(mask_port) & ~mask_bit);
    S.isr_in_progress = 0;
}
```

The ISR is doing the audio callback inline. `cb` is the mixer's
render-half function, which fills `S.half_frames` worth of audio into
`halfp`. Cost: whatever the mixer does, run from interrupt context.
HEARO's pcspeaker driver moved away from this model precisely because
the mixer takes tens of ms on a 386 and was starving foreground; SB
keeps it because the SB16 ISR fires only twice per buffer wrap (once
per half) so the budget is generous.

**Mpxplay** `AU_CARDS/SC_SB16.C:51-55` (SB16_irq_routine, 4 lines):

```c
static void SB16_irq_routine(struct mpxplay_audioout_info_s *aui)
{
 unsigned int baseport=aui->card_port;
 inp(SB16_DATA_AVAIL_PORT);
}
```

Mpxplay's SB16 ISR is one port read. That is the entire body. SB Pro /
2.0 ISR (`SC_SBPRO.C:101-106`) is the same plus a comment. SB 1.x
single-cycle ISR (`SC_SBPRO.C:92-99`) adds three more `SB_writeDSP`
calls to re-arm 0x14 because the chip stops at terminal count.

The actual decode + mix + buffer-fill happens in the **foreground
main loop**, not in the ISR. The ISR's only job is to acknowledge the
chip so it stops asserting IRQ. The DMA is auto-init, so the chip
keeps reading the buffer regardless; the foreground races to keep the
buffer ahead of the playhead. Mpxplay's INT 08h hook (every timer
tick, 18.2 Hz default but reprogrammable) calls
`MDma_interrupt_monitor` (`DMAIRQ.C:194-205`) which:

```c
if(aui->card_dmafilled<(aui->card_dmaout_under_int08*2)){
  if(!(aui->card_infobits&AUINFOS_CARDINFOBIT_DMAUNDERRUN)){
   MDma_clearbuf(aui);
   funcbit_smp_enable(aui->card_infobits,AUINFOS_CARDINFOBIT_DMAUNDERRUN);
  }
}else{
  aui->card_dmafilled-=aui->card_dmaout_under_int08;
  funcbit_smp_disable(aui->card_infobits,AUINFOS_CARDINFOBIT_DMAUNDERRUN);
}
```

Buffer underrun -> zero-fill and set the underrun flag. Foreground
sees the flag and works harder. No ISR-side mixing.

The DMAIRQ ISR wrappers `newhandler_sc_normal` and
`newhandler_sc_special` (`DMAIRQ.C:441-486`) are also worth noting:

- `newhandler_sc_special` for cards with `SNDCARD_SPECIAL_IRQ` (GUS):
  switches to a private 16K stack via inline asm `stackcall_sc`
  (line 431-439), wraps with `MPXPLAY_INTSOUNDDECODER_DISALLOW`
  bracket so re-entry from INT 08h decoder cannot collide. Also has
  `savefpu`/`restorefpu` slots commented out (line 422-429).
- `newhandler_sc_normal` for everything else: just calls the per-card
  ISR, EOIs, and returns.

### Side by side

| Concern | HEARO | Mpxplay |
|---------|-------|---------|
| ISR body workload | full mixer render pass | one port read (auto-init) or four (single-cycle) |
| Ack ordering | mask own IRQ, ack chip, EOI, then callback | ack chip, return, EOI in wrapper |
| EOI source | inline (line 198-199, 225-226, 251, 288) | wrapper (`DMAIRQ.C:460-462`) |
| Re-entrancy guard | yes (`isr_in_progress` flag, line 207, 289) | yes (`AUINFOS_CARDINFOBIT_IRQSTACKBUSY`) |
| Self-IRQ mask during body | yes (line 215, unmask at 288) | no |
| Stack swap to private stack | no | yes for SPECIAL_IRQ (GUS), via `stackcall_sc` |
| Teardown gate check | yes (line 197, 250) | implicit via `MIrq_Stop` unhook |
| FPU save/restore | no | scaffolded but commented out |
| Buffer fill | callback runs in ISR | callback runs in foreground main loop |
| Underrun handling | mixer's job, ISR keeps refilling | INT 08h timer monitor zero-fills |

### Takeaways

- The two architectures differ on where the work lives. HEARO has a
  callback-driven model: the chip's IRQ wakes the mixer, the mixer
  fills the freed half. Mpxplay has a polled model: the chip's IRQ
  is just an ack, the foreground main loop fills ahead of the
  playhead, INT 08h zero-fills if the foreground falls behind.
- HEARO's model is simpler and works fine for a fixed-platform
  player on known hardware with one decoder. Mpxplay's model is
  necessary for a player that supports many decoders, video, UI,
  network streams, and crossfading without one of them stalling
  audio.
- HEARO's self-IRQ mask + isr_in_progress flag is unique. Neither
  appears in Mpxplay because Mpxplay's ISR never does anything that
  could be preempted by another IRQ on the same line. Once HEARO
  decided to do mixer work in the ISR, the mask became necessary.
- Mpxplay's private-stack pattern for GUS (`SNDCARD_SPECIAL_IRQ`) is
  worth knowing if HEARO's GUS hw-mix path is ever wired up. The
  GUS ISR is long enough that the foreground stack running on a
  386 with deeply-nested decoder calls can blow if the ISR lands at
  the wrong moment; switching to a 16K private stack eliminates the
  class of bug.
- The `MPXPLAY_INTSOUNDDECODER_DISALLOW` bracket inside Mpxplay's
  special ISR (`DMAIRQ.C:446`) prevents re-entry from the INT 08h
  decoder hook. HEARO does not have an INT 08h decoder hook, so it
  does not need the equivalent.

---

## 4. Mpxplay's main loop: how UI, decoding, and buffer refill interleave

`MPXPLAY.C:71-89` (main), then `mpxplay_init_run` (line 207-218):

```c
static void mpxplay_init_run(void)
{
 if(funcbit_test(intsoundcontrol,INTSOUND_TSR)){
  do{ mpxplay_empty(); }while(mvps.partselect!=0);
 }else{
  do{ mpxplay_main_cycle(); }while(mvps.partselect!=0);
 }
}
```

Two loop modes:

- **Normal mode** (the typical case): `mpxplay_main_cycle()` is called
  in a tight `do-while` loop. Each iteration is one tick of work.
- **TSR mode** (`-xr`): main thread sleeps in `mpxplay_empty()`
  (`pds_threads_sleep(10)`); all work happens in INT 08h hooks
  (`mpxplay_tsr_cycle_1` and `mpxplay_tsr_cycle_2`).

`mpxplay_main_cycle` (line 221-228) dispatches on `partselect`:

```c
static void mpxplay_main_cycle(void)
{
 struct mainvars *mvp=&mvps;
 switch(mvp->partselect){
  case 1: mtc1_running=1; main_part1(mvp); mtc1_running=0; break;
  case 2: main_part2(mvp); break;
 }
}
```

- **part1** (line 251-308): "between songs". Skips, opens new file,
  sets up decoder, transitions to part2.
- **part2** (line 769-859): "playing". This is the per-tick playback
  loop body.

`main_part2` does in this order, every tick:

1. `mpxplay_mpxinbuf_buffer_fillness_check(mvp, frp0)` (line 780).
   Reads from the input file into the mpxinbuf prebuffer; only does
   work if the prebuffer is below the high-water mark.
2. Look-ahead next file load if `-bpn` (line 792-815). Reads ahead
   into the next file's frp1 prebuffer for gapless transitions.
3. `mpxplay_infile_decode(mvp->aui)` (line 824) **only if the
   INTSOUND_DECODER bit is NOT set**. This is the call that decodes
   one chunk from the prebuffer, runs the mixer chain, and calls
   `MDma_writedata` to enqueue PCM into the DMA ring.
4. `mpxplay_timer_execute_maincycle_funcs()` (line 852) runs all
   registered foreground tick handlers: keyboard input, screen
   refresh, ID3 tag scrolling, seek handling, crossfade controller,
   etc.
5. `mpxplay_newsong_check(mvp)` (line 856) and pause processing.

When `INTSOUND_DECODER` is set (`-x` flag), step 3 moves into the
INT 08h handler and the main loop just does steps 1, 2, 4, 5.
This is the optimization for slow systems where foreground ticks
are inconsistent.

The INT 08h chain (configured at line 162-163 for TSR mode, or via
`newfunc_newhandler08_maincycles_init` at line 183-186 for normal
mode) runs at 100 Hz default (`INT08_DIVISOR_NEW`), reprogrammed up
from BIOS 18.2 Hz. Each tick:

- Calls registered tick handlers (timers, seek check, song advance).
- Decrements `card_dmafilled` by `card_dmaout_under_int08` to track
  how much of the DMA ring the chip has consumed since last tick
  (`DMAIRQ.C:194-205`).
- If `card_dmafilled` falls below `2 * card_dmaout_under_int08` and
  underrun flag was clear, set the underrun flag and zero-fill the
  ring (`MDma_clearbuf`).

The card's IRQ handler (`SB16_irq_routine`, etc.) runs at chip
firing rate (typically 1-4 Hz for a 32K buffer at 44.1 kHz) and only
acks the chip.

So the data flow is:

```
file -> mpxinbuf -> infile_decode -> mixer chain -> MDma_writedata
                       ^                              |
                       |                              v
                     foreground tick              card_DMABUFF
                                                       |
                                                       v
                                                    8237 DMA
                                                       |
                                                       v
                                                  DSP / DAC
                                                       |
                                                  IRQ (ack only)
                                                       |
                                                  underrun detect
                                                  via INT 08h
```

The main loop's responsibility is to keep `card_DMABUFF` ahead of the
DMA playhead. The chip's IRQ is informational only.

### Why this works on DOS

In real-mode DOS without preemptive scheduling, this is the cleanest
way to interleave UI, file IO, decode, and audio without a
multitasking kernel. The key insight is that **the audio buffer is
the synchronization primitive**. Foreground keeps it full;
underrun detection runs on a timer; the chip pulls from it
asynchronously. UI ticks happen between decoder ticks.

The price: foreground latency budget must beat
`buffer_size / sample_rate`. At 32K @ 44.1 kHz stereo 16-bit, that
is 32K / (44100 * 4) = 186 ms. Anything that blocks foreground for
more than 186 ms underruns. Mpxplay's prebuffer (mpxinbuf) absorbs
most of the file IO variance; the decoder is the only foreground
work that can blow the budget, which is why INTSOUND_DECODER exists
as a fallback (decode in INT 08h every 10 ms).

### Comparison to HEARO

HEARO is callback-driven from the chip's IRQ. The mixer fills the
released half synchronously inside the ISR. There is no foreground
audio loop; all foreground does is run the UI and respond to input.

This works because HEARO's mixer is fixed-cost per render call; the
worst case is bounded and well below the half-buffer interval at
22 kHz. Mpxplay cannot make that assumption because its mixer chain
is variable (resampling, channel conversion, effects, format
conversion) and its decoders are variable (MOD, MP3, OGG, Vorbis,
AAC, FLAC, WAV, MIDI...).

### Takeaways

- For HEARO's scope (fixed mixer, fixed decoder shape), the
  callback-driven model is the right call. Mpxplay's polled model
  is overkill.
- If HEARO ever adds a long-running decoder (lossy formats with
  variable per-frame cost), the polled approach with INT 08h
  underrun detection is the proven fallback. The pieces are all in
  `DMAIRQ.C` and `MPXPLAY.C` if needed.
- Mpxplay's `mpxinbuf` is the file-side prebuffer (decoder input)
  vs `card_DMABUFF` is the audio-side prebuffer (decoder output).
  Two separate ring buffers with two separate fill criteria.
  HEARO conflates these into one buffer because the decoder runs
  in the ISR; if HEARO ever splits decode from render, this is
  the model.

---

## 5. DSP copyright string (cmd 0xE3) for clone gating

**Short answer**: Mpxplay does NOT use 0xE3. It gates clone behavior
purely on the version returned by 0xE1.

**Evidence**:

- `grep -n "0xe3\|0xE3" AU_CARDS/SC_*.C`: only matches in
  `SC_INTHD.C:1022` (a PCI device ID `0xe328` for Panther Lake-H, an
  HD Audio chip) and `SC_SBLIV.C:570` (a code address inside an EMU
  microcode dump). Neither is a DSP command.
- `SC_SB16.C:103-120` (SB16_testport): only DSP command issued is
  0xE1.
- `SC_SBPRO.C:108-134` (SB_testport): only DSP command issued is
  0xE1.

The version-only gating logic, transcribed:

`SC_SB16.C:115-117`:
```c
if(ct<0x0400 || ct>=0x0500)
 return 0;
aui->card_type=6;  // SB16
```
Anything in [0x0400, 0x0500) is treated as SB16. A clone with a
fabricated version byte would pass.

`SC_SBPRO.C:120-129`:
```c
if(ct<0x200)        aui->card_type=SB_CARDTYPE_10;
else if(ct==0x200)  aui->card_type=SB_CARDTYPE_200;
else if(ct<0x300)   aui->card_type=SB_CARDTYPE_201;
else                aui->card_type=SB_CARDTYPE_PRO;
```
Branched ladder on the version word. Anything 0x300 or above is
classified SB Pro.

There is no DSP 0xE3 (copyright string), no 0xE4 (DSP write test),
no clone-fingerprint logic anywhere in the SB drivers. Mpxplay
trusts the version byte completely.

### Why HEARO might want to do better

The YMF715 OPL3-SAx without vendor init returns version bytes that
encode SB16 (0x04xx) but its auto-init mode is broken. HEARO's
`force_single_cycle` flag (`sb.c:66`, env var `SB_SINGLECYCLE`)
exists precisely because version probing alone could not distinguish
this clone from a working SB16.

The DSP 0xE3 command, on a Creative-branded SB16 or AWE32, returns a
NUL-terminated copyright string like "COPYRIGHT (C) CREATIVE
TECHNOLOGY LTD, 1992." A clone that fakes 0xE1 may or may not also
fake 0xE3; chips that do not implement 0xE3 typically time out (no
data ready). This gives at least a probabilistic clone signal.

### Status in HEARO

HEARO does not currently issue 0xE3 either. The `SB_SINGLECYCLE`
override is the workaround. For a future detect-layer hardening pass,
0xE3 would be a useful additional probe:

- If the version says SB16 but 0xE3 times out, flag as "clone, may
  need single-cycle."
- If the version says SB16 and 0xE3 returns "Creative", trust the
  auto-init path.
- Either way, do not gate playback on the result. The user override
  via env var must still win.

### Takeaways

- Mpxplay's "trust the version word" approach is fine on Creative
  hardware and most clones; it fails silently on edge cases like
  YMF715 in MS-DOS mode.
- HEARO's escape hatch (`SB_SINGLECYCLE` env var + `force_single_cycle`
  in driver) is the practical answer in the absence of a clone
  fingerprint database.
- Adding a 0xE3 probe to HEARO's detect layer (not the driver) would
  let the auto-detect default to single-cycle on chips that fail
  the copyright check, eliminating the need for the user to set
  the env var manually.

---

## Summary table: where to steal what from where

| Pattern | Source | Target | Effort |
|---------|--------|--------|--------|
| Frequency-scaled DMA buffer size | Mpxplay `MDma_get_max_pcmoutbufsize` | HEARO `dma_alloc` callers | Low |
| Private stack for long ISR | Mpxplay `stackcall_sc` | HEARO `gus.c` if hw-mix lands | Medium |
| INT 08h underrun monitor | Mpxplay `MDma_interrupt_monitor` | HEARO if it ever adds variable-cost decoders | High |
| 0xE3 copyright probe for clone detection | Neither has it | HEARO detect layer | Low (one DSP write + read with timeout) |
| `dma_wait_quiescent` before free | HEARO unique | Mpxplay (not relevant; they have DPMI) | n/a |
| UMB rejection via strategy 00h | HEARO unique | Mpxplay (not relevant; DPMI) | n/a |
| SB Pro mixer reg 0x0E restore | HEARO unique | Mpxplay would benefit | Low (they would have to add it) |
| ISR self-mask + in_progress flag | HEARO unique to its callback model | n/a |

The asymmetry here is informative: HEARO's hardness lives in close
and DMA, where Mpxplay can lean on DPMI; Mpxplay's hardness lives in
the main loop and ISR architecture, where HEARO has narrower scope
and does not need it.
