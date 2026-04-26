# HEARO Design Document: Novel Arithmetic Additions

Additions for `hearo-design.md` covering techniques from modern arithmetic research (2015-2024) adapted to run on period FPUs.

## Section 5.2.1 Deep-Dive: What the FPU Actually Unlocks

HEARO implements several techniques from modern arithmetic research, adapted to run on the specific FPUs the user has installed. If you have a 287, HEARO uses it to run a 2022 Posit Standard quire in software across XMS, giving you exact tracker mixing. If you have a 387 or later, HEARO additionally runs adaptive-precision CORDIC visualizers. If you have a Pentium-class FPU, HEARO adds log-domain effects chains derived from Takum arithmetic research.

## Section 22.5: Post-Moore Arithmetic Techniques

### 22.5.1 The Premise

Modern arithmetic research has produced techniques that rethink how computers represent and manipulate numbers. Posits (Gustafson 2015, Posit Standard 2022). Takum logarithmic tapered precision (2024). Stochastic computing. Interval arithmetic. Adaptive-precision iterative methods.

None run natively on a 287/387/487. These chips speak IEEE 754. But the techniques, stripped of native hardware contexts, can be adapted. Software quires run on IEEE FPUs. Adaptive CORDIC replaces hardware FSIN/FCOS. Stochastic computing runs on any CPU with bitwise AND.

The bar for inclusion: 80%+ chance of working in production.

### 22.5.2 Adaptive-Precision CORDIC (95%+ confidence)

CORDIC produces one bit of precision per iteration and can be stopped at any iteration. HEARO implements software CORDIC instead of hardware FSIN/FCOS, letting it dynamically trade visualizer precision for CPU budget. A plasma effect drops iterations from 24 to 12 when the tracker mixer gets busy, then restores when load drops. The visualizer never stutters.

### 22.5.3 Software Quire (85-90% confidence)

The 2022 Posit Standard quire is a 256-bit fixed-point accumulator for exact dot products. HEARO implements it in XMS memory, used for tracker mixer channel summation. Quiet channels in dense IT modules remain audible where every other DOS mixer loses them to quantization noise. The FPU converts samples; XMS holds the quire; rounding happens only at frame end.

### 22.5.4 Stochastic Computing for FPU-less Systems (75-80% confidence)

Numbers as bit-stream probabilities. Multiplication becomes AND. A 286 with no FPU can run a particle visualizer using stochastic bit streams. Particles jitter (SC is noisy), which reads as organic motion. Available only on FPU-less systems: the inversion where constrained hardware gets an exclusive feature.

### 22.5.5 Interval Arithmetic for Fingerprinting (85% confidence)

Unum Type I interval bounds applied to audio fingerprint matching. Interval pre-filter eliminates 95%+ of MusicBrainz candidates without running expensive full comparison. Library scans drop from hours to minutes.

### 22.5.6 Bipartite Tables (99% confidence)

Two small table lookups plus an addition for 8-10 bit transcendental precision. Enables 64-bin FFT on FPU-less 286 where fixed-point was stuck at 16 bins. ~4KB tables, ~20 cycles per evaluation.

### 22.5.7 Log-Domain Effects (80% confidence)

Takum-inspired logarithmic arithmetic for reverb, EQ, dynamics. Multiplication becomes addition. Better dynamic range at signal extremes. Available on FPU-equipped systems in full, limited subset on FPU-less.

### 22.5.8 8087 Silicon Fingerprinting (70% confidence)

Detect the 8087's substrate bias generator oscillation via timing loops. Distinguish genuine Intel 8087s from later chips and emulators. Pure easter egg. Hall of Recognition entry: "Intel 8087 (genuine silicon, substrate bias at 1.3MHz)."

### 22.5.9 Cross-Tier Pattern

| Technique | Target | Effect |
|-----------|--------|--------|
| Adaptive CORDIC | FPU-equipped | Visualizers never stutter |
| Software quire | FPU-equipped | Exact tracker mixing |
| Stochastic vis | FPU-less | Exclusive visualizer |
| Interval fingerprint | All tiers | Fast library scans |
| Bipartite tables | FPU-less | Unlocks FFT on 286 |
| Log-domain effects | Both tiers | Better effects |
| 8087 fingerprint | 8087 only | Easter egg |

### 22.5.10 Implementation Roadmap

- v1.0: Adaptive CORDIC, bipartite tables
- v1.1: Interval arithmetic
- v1.3: Software quire, stochastic particle
- v1.4: Log-domain effects
- v1.5: 8087 silicon fingerprint

### 22.5.11 The Marketing Line

"HEARO is the DOS music player whose arithmetic was designed by reading 2024 research papers and asking how to make it run on a 1985 chip."

## Section 22.6: Speculative (Below 80%)

- LNS AdLib sample playback (60-70%): log-domain tracker on OPL2. Sounds terrible. That is the point.
- Neuromorphic particle interactions (50-60%): particles with neural neighbor interactions.
- Approximate computing for vintage laptops (65%): trade precision for battery savings.
