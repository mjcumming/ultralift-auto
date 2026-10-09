# Ready / Guide review — 2026-10-09

Recorder review of the lowered-to-ready part of the stroke. No new motion was commanded for this note. Live device `boat-lift`. The visits below were taken at Zone Tolerance **3.5°** (the flash-persisted dock setting from 2026-09-10). The firmware initial is **2.0°**. On 2026-10-09 that persisted value was set back to **2.0°**. The supervised Guide lower at the new band is still open.

Purpose: see whether Ready and Guide can be told apart more tightly than ±3.5°, and write the next goals before anyone nudges a capture.

Captures left as they were:

| | Starboard | Port |
|---|---|---|
| Lowered | 53.77° | 53.80° |
| Guide | 52.89° | 53.02° |
| Ready | 45.95° | 46.20° |

Arm length 51 in. Near Ready a degree is about 0.62 in of bunk; near Guide about 0.54 in. The two captures are 6.9° apart, about 4.0 in of bunk. Bunk height is waterline-relative (Lowered datum −13 in).

Sources: Home Assistant `sensor.cobalt_boat_lift_arm_angle_starboard` for the visits below, plus the July lowers in `field-data/` for descent speed. Position history on this controller starts 2026-09-30. The angle sensor publishes only when it moves at least 0.1°, so the wave numbers are a floor.

---

## What the visits show

### Ready stops on the near edge, then barely coasts

A lowering arrival sets the zone as soon as the angle enters `capture − tol`. With tol 3.5° that edge is **42.45°**. Four Ready flags:

| Visit (UTC) | Flag | Then |
|---|---|---|
| Oct 5 22:21 | 42.71° | short stay |
| Oct 5 22:27 | 42.56° | left for Guide within a minute |
| Oct 6 21:57 | 42.52° | sat ~73 min, mean **43.08°** |
| Oct 7 22:56 | 42.48° | sat ~12 min, mean **43.06°** |

Spread of the four flags is **0.23°** (about 0.15 in). The coast after the flag is about **0.6°**. The long Oct 6 sit held 43.05–43.12° on the 5-minute median for 70 minutes. Sixty-second peak-to-peak at that park was **0.42° p90** (about 0.27 in).

So the parked Ready the boat actually uses is about **1.8 in above** the 45.95° capture. The capture is not where it stops. The edge is.

### Guide coasts 2–3° after the flag

Guide’s 3.5° edge is **49.39°**. Three Guide flags, then the rest after the coast:

| Visit (UTC) | Flag | Settled | Coast |
|---|---|---|---|
| Oct 5 22:28 | 50.35° | 53.39° for ~55 min | 3.0°, through the capture |
| Oct 6 23:12 | 49.59° | 52.20° for ~98 min | 2.6° |
| Oct 7 23:10 | 50.36° | 52.31° for ~45 min | 2.0° |

Oct 6 and Oct 7 finished within about 0.7° of the 52.89° capture (a few tenths of an inch). Oct 5 kept going to 53.4°, almost at the Lowered capture. After the coast, the Oct 6 Guide sit drifted under 0.2° across 90 minutes. Sixty-second wave there was about **0.35° p90**.

The Guide guard still matters. A rest near 52.2° is inside the one-sided Lowered band (`angle > 53.77 − 3.5`, so above 50.3°). Vents stay shut only because Guide is checked first and HOLD does not re-enter `LOWERED_VENT` while either side is still in the Guide band.

### The 3.5° bands almost touch

Ready’s band is 42.45–49.45°. Guide’s is 49.39–56.39°. The gap between them is 0.06°. Raising the Guide capture about 2 in (about −3.6°, to ~49.3° starboard / ~49.45° port) moves Guide’s approach edge onto the Ready capture. That is the wrong knob for “a little higher.”

### The loop can resolve a tighter band

Through 41–47° the October samples descended at **0.11°/s** median and **0.20°/s** at the 90th percentile (502 intervals). July field lowers through the same band were 0.14–0.18°/s. One degree is about 9 seconds, roughly 36 supervisor ticks. A half inch near Ready is about 0.8°, larger than the 0.4° wave. A tenth of an inch is inside the wave.

---

## Goals

1. **Zone Tolerance is 2.0°** as of this deploy (`number.cobalt_boat_lift_zone_tolerance_deg`). It had been persisted at 3.5°. At 2° the Ready edge moves from 42.45° to 43.95° and Guide’s from 49.39° to 50.89°. The two bands then have about 3° of clear gap (Ready 43.95–47.95°, Guide 50.89–54.89°). Ready should park about 0.9 in closer to its capture if the 0.6° coast stays the same. OTA does not move this number; the deploy sets it.
2. **Watch one Guide lower at 2° before going any tighter.** Record the flag angle, the angle 30 s later, and the settled angle. Pass: the settled angle is still inside `capture ± 2°`, Status stays Guide, and the valves stay closed (not `LOWERED_VENT`). The Oct 5 coast of 3° is why 1.0° is off the table: a coast that size can leave a ±1° band on the far side, fall out of the Guide guard, and reopen the vents. 1.5° waits on that same supervised lower.
3. **Leave both Guide captures where they are** until that lower is in. A 2 in raise of the saved angle, at today’s 3.5°, makes Guide arrive where Ready already is. If Guide still rests too low after the 2° test, move **both** sides by the same bunk inches (the 51 in sine), on the 0.05° step, and only while sitting still. Home Assistant shows these calibration numbers as sliders (`mode: auto`); `number.set_value` on them often does not refresh the displayed value. The local page’s number field rejects a live value such as 52.89° because it is not on the 0.05° grid.
4. **Treat half an inch as the useful height step** in this part of the stroke. Target Height may still accept 0.1 in, but a destination claim finer than the ~0.4° wave is not something these visits support.
5. **Keep one shared tolerance** for this pass. Ready’s coast would allow ±1° on its own. Guide’s coast would not. Splitting the knob is a later change, after the 2° lower says whether Guide’s coast shrinks when the flag moves later.

---

## Not claimed

- That a 2° zone has been run through a Guide or Ready move. The knob was set to 2.0° on 2026-10-09; the first lower at that band is still the supervised check in goal 2.
- That Guide’s coast is a fixed angle. It may shrink or grow when the flag moves closer to the capture. Goal 2 is how we find out.
- Port angle for these same sits. The table above is starboard, which was the height source. Any capture nudge still has to be applied to both sides so leveling does not invent a list.
