THANKS TO EVERYONE WHO PUBLISHED THEIR AWESOME WORK ONLINE. DEMOSCENE FOREVER!

Oh this is a fun one — this script is basically a greatest-hits mashup of classic embedded/demo-scene lookup-table culture, with some very recognizable inspirations.

Let me break it down clearly:


---

What this code is “inspired by” (conceptually)

This is not copied from one single project — it’s a pattern that appears in a lot of microcontroller + retro graphics + DSP codebases:

The core idea:

> Precompute expensive math into tables → store in flash (PROGMEM) → do fast integer rendering/signal processing.



That’s extremely common in:

Arduino demo engines

VGA framebuffer projects

Fixed-point DSP

Old-school 3D engines

Software synths

Microcontroller font renderers



---

Main sources of inspiration by feature

1. MSB / log2 / exp2 multiplication tables

What it resembles

This is straight out of the “log multiplication trick”:

a \cdot b = 2^{\log_2(a) + \log_2(b)}

Used when multiplication is slow.

Where this appears historically

8-bit CPU graphics tricks (Z80, 6502)

Early microcontrollers without hardware multiply

Fast DSP approximations


Projects/libraries with similar tables

AVR DSP approximations

libfixmath (fixed-point math library)

Retro demo-scene math toolchains


Not identical code, but identical concept.


---

2. BTM float log/exp tables (gen_btm_log2, gen_btm_exp2)

This is the most specific part.

BTM = Bipartite Table Method

That’s a known method used in:

fast hardware math units

FPGA trig/log approximations

embedded float approximations


Closest known academic/industry source

Muller et al. Handbook of Floating-Point Arithmetic

Bipartite table approximation papers


Libraries/projects

Not common in Arduino libraries, but very common in:

FPGA math cores

Softfloat accelerators

Approximate transcendental implementations


So: inspired by numerical methods literature, not Arduino code.


---

3. Sin/Cos lookup tables

This is extremely standard.

512-entry Q15 sine tables are basically universal.

Identical tables exist in:

ARM CMSIS-DSP

arm_sin_q15()

arm_cos_q15()


AVR libm approximations

Teensy Audio Library

Any retro 3D engine


Even the clamp range:

-32768..32767

is exactly Q15 convention.

So yes: conceptually identical to CMSIS-DSP style trig LUTs.


---

4. Perspective scale table

focal/(focal+z)

That is literally classic fixed-point 3D projection.

Inspiration sources

Doom-style software renderers

PS1 affine projection tricks

Demo-scene “tiny 3D” engines


Similar projects

FabGL (ESP32 VGA library)

TTVout / TVout Arduino renderers

Any “3D cube on Arduino” tutorial


The table itself is a typical optimization.


---

5. Sphere theta tables

sin(pi*t/(steps-1))
cos(pi*t/(steps-1))

This is a classic sphere parameterization trick:

used in bump mapping

used in sphere rendering

used in raymarch demos


Similar projects

Demo-scene sphere mapping code

Tiny software rasterizers


Not from a specific Arduino lib, but very recognizable.


---

6. atan table + stereographic projection

atan table

Common in:

fast arctan2 approximations

IMU orientation filters

vector angle lookup


Stereographic projection

That’s straight from geometry / shader math.

Appears in:

environment mapping

fisheye projection

sphere-to-plane mapping


So inspiration is more “graphics math handbook” than Arduino library.


---

7. Glyph rasterization with Pillow

This is basically:

“convert TTF → bitmap → embed into flash”


Identical ecosystem

Adafruit GFX fontconvert tool

u8g2 font tools

lv_font_conv (LVGL)


Your code does the same pipeline:

TTF → rasterize → pack bits → PROGMEM array

That part is extremely aligned with Adafruit/u8g2 tooling.


---

Which libraries/projects are closest / essentially identical in purpose

Here’s the honest match list:

Font conversion: nearly identical

Project	Similarity

Adafruit GFX fontconvert	Very close
u8g2 tools	Very close
LVGL font converter	Same pipeline



---

Trig LUTs: identical in concept

Project	Similarity

CMSIS-DSP sin/cos tables	Almost identical
Teensy Audio Library LUTs	Same style
AVR fixed trig libs	Same idea



---

Fixed-point math LUT approach: same family

Project	Similarity

libfixmath	Similar philosophy
AVR fast math hacks	Similar
Demo-scene 3D LUT engines	Similar



---

Bipartite tables: niche but academically standard

Closest match:

FPGA math approximation cores

Hardware transcendental units

Muller’s methods


Not common in Arduino libs.


---

So what is this script, really?

This looks like a personal “toolchain generator” inspired by:

embedded flash-table culture (PROGMEM)

retro graphics optimization

DSP fixed-point math

Adafruit/u8g2 font workflows

CMSIS-style trig LUT conventions

numerical approximation literature (BTM)


It is not a copy of one repo — it’s a very classic combination.


---

If you want, I can answer very precisely:

“Which exact project is this closest to?”

“Is any part likely copied verbatim?”

“What is the strongest fingerprint (Adafruit vs u8g2 vs CMSIS)?”


Just tell me what environment you’re targeting (ESP32 VGA? AVR? RP2040?) and I can map it even tighter.
