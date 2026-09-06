# Changelog

## 0.10.4 — 2026-09-07

- Discover the loader NVS partition at runtime: prefer M5Apps `apps_nvs`, then
  any DATA/NVS slot. Host-channel settings and BLE bonds now persist on
  loaders that label the store `nvs` instead of `apps_nvs`.
- Surface the concrete ESP-IDF NVS error on device and over USB diagnostics.
- Add a double-confirmed Opt+Tab `RESET BLE BONDS` action that erases only this
  app's security keys and restarts advertising, leaving M5Apps data and local
  preferences intact.
- Ignore USB diagnostic DECK/TASK traffic while a native Codex Micro session is
  alive. The leftover serial companion polls every two seconds and was calling
  wake() on each line, so Auto-dim never held: the backlight bounced full/dim.

## 0.10.3 — 2026-08-24

- The stick page has sound: right and up rise, left and down fall, on the same
  cues the dial's detents use, and the page opens on the same chime the dial's
  does. Silence on an instrument page reads as a press that did not register.
- The cap springs out to its throw and springs back instead of being teleported
  to full deflection and released. The jump was visible on every press, and on
  a held arrow the repeats stacked into a stutter.
- Rounded the cap's position instead of truncating it, so the last pixel of the
  return does not stick.

## 0.10.2 — 2026-08-24

- Fixed the stick's vertical throw, which ran backwards: screen y and the
  direction table both count down as +1, and negating one of them made the cap
  answer a Down key by hopping away from the chevron that lit.
- Lengthened the vertical travel to match how far the horizontal throw reads.

## 0.10.1 — 2026-08-24

- Brought the stick's chevrons in close. Set out at the screen edges they read
  as four separate marks rather than as the throws of one control.
- Stood the cap on the plate instead of down inside the recess, and widened the
  recess so a full throw still lands within it. It was a nub sunk in a hole.
- Made the throw legible: further travel, and a looser spring that takes its
  time coming back. The old one was a twitch that was over before it was seen.

## 0.10.0 — 2026-08-24

- The four arrow keys get the dial's page. Same flood, same family of drawing:
  a chevron on each side lighting on the impulse, the instrument in the middle,
  Esc top-right.
- Micro's stick is planar -- a low rubber cap sliding across a recess in the
  plate -- so that is what is drawn. The first pass was a ball on a shaft that
  pivoted, which is a different object entirely.
- The stick page always closes itself three seconds after the last press. An
  arrow is an impulse: the host consumes it and reports nothing back, so there
  is no state for the page to wait on. It never opens over the dial page, which
  does have state to lose.
- Screen checks moved to their own list under DEBUG. Each renders a real path
  with fake data, which is not the same kind of thing as a switch that changes
  how the device behaves. The stick page joins them.

## 0.9.7 — 2026-08-24

- The confirm hint is the return arrow itself, not the word, and it sits in the
  bottom-right corner. At this size the mark is shorter than its name and it is
  the same mark printed on the key. Diagonally opposite Esc, the two ways out
  of the page bracket it instead of stacking into a list.

## 0.9.6 — 2026-08-24

- Took the keycap off the control page. Its whole subject is the dial, and a
  second drawn object competed with it for the eye.
- Unbound row-1 backslash. It was the encoder click and Enter is the click now;
  two keys for one confirm is one too many. The release stays physical -- it
  moved onto Enter -- so Codex can still time a long press on it.
- Centred the dial and its chevrons on the screen, with TURN under them.
- Named ENTER above ESC in the top-right corner, in the same hint style.

Clicking the encoder without turning it first is no longer possible from the
Cardputer: rotation is what opens the surface, and Enter confirms once it is
open. Enter still submits the composer everywhere else.

## 0.9.5 — 2026-08-24

- The control page closes itself three seconds after the last detent, but only
  while the session is still nothing: no click made, no lamp lit in reply. A
  dial nudged and abandoned is a stray turn, not a session, and it should not
  leave a page on the screen.
- A click, or any lit lamp from the host, engages the session and stops the
  clock for good. From there the page still waits for the host's own close
  frame or for Esc, however long the user takes -- which is the part every
  earlier timer got wrong.

## 0.9.4 — 2026-08-24

- Put the chevrons back beside the dial. As keycaps the two detent directions
  turned a page with one control on it into a page with three, and the dial
  stopped being the subject of it.
- The confirm cap now carries a return arrow instead of `\`. `\` is the
  physical encoder switch and stays wired to it, but as a legend it named the
  key rather than the action.
- Enter confirms the host's control surface while it is open, instead of
  submitting the composer. Nothing on that page is a message.

## 0.9.3 — 2026-08-24

- Replaced the chevrons flanking the dial with two small keycaps carrying `[`
  and `]`, drawn in the same profile as the select cap and pressing down on the
  detent that matches them. A chevron says "there is more this way", which is a
  scroll hint; the brackets say what the host actually receives.
- Brought the dish back on the cap top, as a filled tone with a lit far rim
  rather than an outline. As a stroke it was one more edge parallel to the
  keyline -- the pleating that made the cap a concertina -- and as a large area
  it turned the cap into a tray.
- Took the position index off the dial, both the notch through the wall and the
  bar on the cap. The knurling already carries the angle; two more marks only
  competed to be read as a value this page never knows.
- Cut the legends into the plastic: the stroke in ink with a lit pixel trailing
  it down and to the right. The backslash's fall was being read against the
  cap's own left edge, which runs the other way.

## 0.9.2 — 2026-08-22

- Took the pyramid out of the keycap. The taper was doing the work of a roof:
  the base was eight pixels wider than the top over a seventeen-pixel wall. It
  is now four over fifteen, on a slightly larger top -- a cap, not a spire.
- Cut the corner chamfers back to a single pixel. Two rows off each tip plus a
  two-pixel side flat gave the silhouette eight visible corners, all of them
  large; at this size that is enough to lose the rhombus.
- Seated the cap lower than the knob. It is the wider, heavier shape, and set
  on the same line it reads as riding high.

## 0.9.1 — 2026-08-22

- The keycap was a concertina. Stacked inside the keyline it had a moulding
  line under the far edges and, before that, a filled dish; three parallel
  edges around a barely tapered slab read as pleats. There is now one line on
  the top face -- where it meets the walls -- and nothing else.
- Given the profile of the thing it is: a tall Cherry-like cap, small top, wide
  base, strongly tapered walls. It was a flat slab pretending.
- The lamp row is gone. In this mode the host's pattern is its own UI state,
  not six agent statuses, so a row of them under the instrument said nothing a
  reader could use. The frame is still mirrored -- for its colour, which goes
  into the knob's base ring, the object actually being turned. The instrument
  now has the whole height and centres in it.

## 0.9.0 — 2026-08-22

- Escape now asks the host to drop its surface instead of only hiding the page.
  Micro has no escape key, so it does what a hand would do on the hardware:
  presses the agent key that is lit. That returns focus to the chat, Codex
  drops the picker, and the host's own close frame takes the page down -- the
  same path as dismissing the surface on the desktop. Hiding it locally is
  kept only as the fallback for when there is no host to answer.

- Weighted the keycap against the knob: a thicker wall and a lower seat, so the
  two objects sit as a pair rather than a tall one beside a flat one.

- The control page now closes on the host's own signal and nothing else. Codex
  blanks all six lamps, or flashes them the same white, when it closes a
  control surface -- so that frame takes the page down. Every timer tried for
  this was the device guessing at a state only Codex knows, and each one cut a
  selection short: a fixed wait after the click, then a test of the host's
  silence, then an eight-second idle. All three are gone.
- Nothing local closes the page any more except Esc. It stays for as long as
  the user takes. A uniform lit frame is read as a close rather than a state
  because a picker preview is never uniform -- it is showing a value.
- Both outcomes are on the wire: `thstatus|picker_closed|uniform` and
  `CCP_UI|composer|closed_by_host`.

## 0.8.8 — 2026-08-22

- The keycap travels the right way. It was moving its top face up while the
  wall shrank, so the cap flinched upwards and tucked itself in. The plate does
  not move: the top now goes down by the travel and the wall loses exactly that
  much, which leaves the base and its shadow where they were.
- A detent calls off any pending exit outright. After a click the page was
  counting down while the user carried on turning, so it closed out from under
  a selection in progress. Whatever the last click meant, a detent means the
  user is still choosing, and that wins.

## 0.8.7 — 2026-08-22

- The wait after a click is now a test of silence, not a stopwatch. The fixed
  0.45 s could not be the host's answer: a click has to cross the radio, be
  drawn by the host and come back, and the all-off frame the host sends while
  it rebuilds its lights never reached the cancel at all. The page now leaves
  only after the host has said nothing for 1.2 s, and every frame restarts
  that count -- so it stays for as long as the host keeps its lamps lit. Both
  outcomes are logged, so which one happened is visible over the wire.
- The keycap is held down for 90 ms before it springs back. Releasing on the
  same frame it went down was a flinch, not a keystroke.

## 0.8.6 — 2026-08-22

- The page no longer leaves while the host is still lit. A click on Micro's
  dial does not always close its picker -- it can step into a submenu or cycle
  a value, and the device cannot tell which from the click alone. It can tell
  from what happens next: after a click the page now waits out both the keycap
  travel and a short window for the host, and a lamp frame arriving in that
  window calls off the exit. Silence still means the picker closed, and the
  page follows it out.

## 0.8.5 — 2026-08-22

- Rebuilt the keycap after looking at it magnified. Three things were wrong.
  The dish was most of the top face, so what read was a tray with a bright rim
  around it rather than a solid key; it is gone, replaced by a single line
  under the two far edges, which is enough to break the plane and leaves the
  face whole. The legend was two pixels of near-black -- a crowbar lying on the
  cap -- and is now one pixel in a softer ink. And the tips were cut with a
  curve fitted to four pixels, which lands differently on each of the four and
  made the silhouette lumpy; they are cut with a fixed chamfer instead.
- Gave the cap the proportions of the hardware. Codex Micro has low-profile
  keys: a broad top and short, nearly straight walls. The flare no longer
  applies above the top face either -- unioning it over the whole silhouette
  let the base's far corners peek out past the top and gave the cap a
  hexagonal brim, where on a real key the top overhangs and hides them.

- Fixed the startup score sagging on boot. The speaker's mixer task was left on
  the defaults: floating across both cores at priority 2 with 32 ms of DMA
  cushion. One late wake-up -- easy to get in the second the radio connects and
  the panel is pushing whole frames -- repeats or drops a block, and that is
  heard as the music slowing rather than as a click. It now has a quarter
  second of cushion and runs on the core that is not drawing, above the synth
  worker that shares it.
- The startup score is started before the radio, so its first blocks are queued
  outside the window where a bonded host reconnects and floods the device.
- Took libm off the boot score's sample path, the way the status scores already
  had it: it was evaluating an exp() per note per sample. Measured on the
  device, rendering it fell from 259 ms to 197 ms, with the score's peak
  unchanged to two decimals -- the table is transparent.
- The output rate is deliberately left at the driver default. Running it at
  16 kHz to match the material saves a 4x resample, but that resample is also
  the only low-pass between the scores and the amplifier, and without it the
  top of every cue turns hard and rings.

## 0.8.4 — 2026-08-22

- Fixed the legend. It was the projection of a horizontal rule laid along one
  isometric axis, which is why it read as a slot milled into the cap rather
  than a letter. A glyph printed on the top face is sheared by the projection:
  its own vertical runs down one axis and its own horizontal down the other,
  and backslash falls from the far corner to the near one -- on screen, a steep
  stroke down and to the right, two rows per step so the diagonal is clean.
- The cap is tapered. It is wider at the plate than at the top, which is most
  of what tells the eye it is a key and not a box; the silhouette is built as
  the top outline swept down and outwards, and the keyline is dilated from that
  one shape so its weight stays even all the way round.

## 0.8.3 — 2026-08-22

- The keycap's rounding is visible because it is no longer a superellipse. A
  smooth exponent turned the cap into a coin: in this projection the edges have
  to stay dead straight or the object stops sitting on the ground plane. The
  edges are exact again and only the four tips are flattened, which is what
  rounding means at this pixel size.
- The keyline is now laid down as one dilated silhouette of the whole cap
  instead of outlining the top face and the walls separately, which used to
  leave tabs sticking out at the tips.
- The dish is a real recess -- a shade back from the face with its far edge
  catching the light -- rather than a one-pixel inset outline that could not be
  seen at all.
- Broke the knob's top edge with a chamfer. Machined aluminium always has one,
  and without it the cap read as a lid dropped onto a tube.
- The lamp row is drawn only when the host has actually sent a frame. Before
  the first one it was six empty wells: a strip that showed nothing and could
  not be read as anything. When it is absent the instrument takes the whole
  height and centres.

## 0.8.2 — 2026-08-22

- The keycap is a keycap now: rounded corners instead of four rhombus spikes,
  a dished inset on the top face, and the `\` set in the plane of that face at
  one pixel of fall per two of run, so it reads as printed on the cap rather
  than laid over it.
- Both objects carry a two-pixel keyline. They are drawn as oversized
  silhouettes with the faces sunk into them, which keeps the weight even all
  the way round instead of thinning wherever a diagonal meets the pixel grid.
- The knob is taller and narrower, closer to the machined encoder's real
  proportions, and its base ring reads clearly as the host's lamp colour
  because it now sits outside the keyline.
- Escape leaves the control page. It is the way out of every other surface on
  the device and did nothing here. Micro's HID vocabulary has no escape, so
  this is deliberately local: the page leaves with the same quarantine as a
  timeout and nothing is claimed to the host. The page says ESC in its corner.

## 0.8.1 — 2026-08-22

- Redrew the dial to the shape of the real one. Codex Micro is Work Louder
  hardware: the encoder is a straight machined cylinder with a flat top, fine
  knurling around the wall and a light ring at its base. The first attempt had
  the knurling radiating across the cap like spokes and almost no wall, which
  is a different object. The knurling is now 24 flutes on the wall so a
  30-degree detent walks it by exactly two, the silhouette has no taper, and
  the index is a notch through the wall plus a bar on the cap.
- The base light ring carries the host's own lamp colour and brightness. It is
  the one place on the page where the device is allowed to be expressive,
  because the colour is not invented -- it is what Micro is showing.
- Dropped the COMPOSER wordmark. The page is the instrument; naming it added a
  label to a surface whose meaning the device deliberately does not claim.
- Rebuilt the keycap as a real cube with three distinct faces, a contact shadow
  that tightens as it travels, and the legend lying along the isometric axis.
  One flat band with a diagonal on it read as a sticker, not an object.
- Confirming no longer makes the page vanish under the finger. The host does
  close its picker on click, so leaving is correct, but the page now waits out
  the 0.22 s of keycap travel first.
- The six agent lamps moved to the columns the six task cells occupy, each in a
  recessed well so a host lamp that happens to be this surface's own blue still
  reads.

## 0.8.0 — 2026-08-22

- The dial now opens its own surface. Turning the knob used to show nothing
  until you had pressed `\`, so the first detent of every adjustment was made
  blind. Any detent the host accepts opens the page; a detent with no host
  still only toasts.
- The host's lamp frame is mirrored instead of discarded. While the control
  page is open, `v.oai.thstatus` is still kept out of the task status reducer —
  it is presentation, not task state — but it is the only truthful thing the
  device knows about a surface whose semantics it deliberately does not guess.
  The six lamps are drawn in the host's own colours and levels, with a marker
  that springs to the brightest. Before the first frame they are empty wells.
- Drew the instrument. The page is now a pixel isometric knob and an isometric
  `\` keycap: the knob is knurled with twelve flutes, one per detent, so a
  click visibly walks the pattern by exactly one flute and the pointer carries
  absolute angle rather than just motion; the keycap depresses on select. Both
  are two flat faces with no shading, matching the deck's own language.
- The numeral retreats to the header at resting scale and the copy is now
  TURN / SELECT under each object, because the instrument is what the page is
  about.

## 0.7.2 — 2026-08-22

- Rebuilt the weak-link annunciator as an instrument panel instead of a
  warning label. It was a dark plate with a word set in reverse — a sticker
  applied over the deck. It is now an inset of the same paper the interface is
  printed on, delimited by a one-pixel rule and bleeding off the screen edge,
  with the code BLE at the left and the level at the right.
- The level is three equal dots with the first lit warm, replacing the
  ascending staircase: equal marks read as a scale, a staircase reads as a
  picture of a signal. The spent positions stay drawn in spent chrome.

## 0.7.1 — 2026-08-22

- Put the build stamp back in the splash's top-right trim, with the
  registration marks rather than with the wordmark: right-aligned to the inner
  edge of the corner mark, on the same rule, tracked tight, no prefix, and
  faded most of the way back to the paper. It is reference, not identity — it
  should be findable, not read. The pixel face has one size, so this is as
  small as the type gets; the reduction is in tracking and contrast.

## 0.7.0 — 2026-08-22

- Corrected every sound for the register it plays in. This speaker radiates
  almost nothing below a few hundred hertz and the ear is least sensitive
  exactly where it gives up, so cues written low arrived quiet however
  carefully their levels were balanced — the input request after it moved down
  a fifth, the key press before that, the startup chime's pad two octaves under
  its melody. Every voice is now scaled by where it sits: flat above 760 Hz,
  rising about 4.5 dB per octave below it, and holding below 280 Hz where more
  level buys only cone excursion. The gains are worked out before each sample
  loop, so no libm reaches the samples.
- Trimmed every score to a target peak instead of a fixed output scale. With
  voices corrected, what a cue peaks at depends on which notes it is playing,
  so the peak is measured during the render and the buffer scaled once it is
  known. Cues are now equally loud rather than equally scaled, and the
  hierarchy between them lives in one place: request and result own the room,
  fault matches them, work in progress sits under them, a release is quietest.
  The input request roughly doubled in level as a result.
- `CCP_AUDIO` and `CCP_CHIME_READY` report the pre-trim peak, so headroom can
  be watched on hardware rather than guessed.
- Rebuilt the weak-link annunciator around a meter: three ascending bars with
  the first lit warm and the other two drawn as outlines, then the single word
  SIGNAL. It reads as a state before it reads as text, and the plate keeps its
  two-column grid alignment.
- Moved the build stamp out of the splash's top-right corner onto the centre
  line under the product name. It was touching the registration mark but
  aligned to nothing; it is now the third line of the wordmark and the corners
  are corners again.

## 0.6.1 — 2026-08-22

- Stopped the input cue sounding like completion. It had been built from the
  completion bell's spectrum, register and roll, so the two read as the same
  event with different notes. Three things separate them now and none is the
  melody: the ask carries its third partial and no octave — odd harmonics
  only, hollow and reedy where the bell is glassy; it speaks a fifth above its
  chord, around 260-400 Hz, a register completion never uses; and its rhythm
  is speech rather than a roll, two notes close together and then a gap two
  and a half times as long before the one it settles on.
- Gave each step of the progression an intonation contour instead of a climb.
  The phrase lifts to a peak and settles a step below it, coming to rest above
  where it began on a chord tone that is never the root — the completion bell
  climbs and lands on top, and doing the same thing an octave down would still
  have been the same gesture. The glint on the arc's peak step now lights the
  top of the phrase rather than its end, a fifth above rather than an octave,
  which would have put the bell's partial back.
- The bed under the wait is an open fourth rather than a fifth: with the ask
  now speaking a fifth above the chord, a bed fifth landed exactly on the
  phrase's first note. Renders 85.3 ms against the 100 ms debounce, 90.5 ms on
  the lit step.

## 0.6.0 — 2026-08-22

- Made a series of input requests a piece of music rather than a repeated cue.
  Each ask now takes the next step of a four-chord progression held in one
  key: the bed moves under it, the three notes are drawn from whichever chord
  is standing, the arc peaks on the third step — where a quiet octave above
  the last note lights it, once a cycle — and leans back on the fourth, which
  wants the first again. Answering four things in a row is heard as a line
  arriving somewhere instead of the same request four times, and because the
  cycle closes without resolving, a long run keeps going round. The key is now
  held for a whole series and re-rolled only after half a minute of quiet:
  transposing every ask, as every other cue does, is what stopped the asks
  from relating to each other at all.
- Paid for the progression by dropping the input bed's middle voice. The chord
  under the wait is a bare open fifth now; what keeps it alive is that it
  moves. Renders are 85.6 ms against the 100 ms debounce, 91.2 ms on the lit
  step, down from 91.5 ms flat.
- `CCP_AUDIO` reports the progression step for input requests, replacing the
  figure index: a series has to be watched over several asks to be judged.

## 0.5.2 — 2026-08-22

- Took the sting out of the input cue's last note. It was the highest note in
  the figure, struck as hard as the two before it and pulled a semitone upward
  through its decay — a rising leading tone on a pure sine, which is a whine
  rather than a question. No input note bends its pitch now, none climbs more
  than a fifth above its call (the old figures topped out near 1.1 kHz, where
  this transducer is harshest), and the last note is the quietest and longest
  of the three with nearly twice the onset of the others: it is reached rather
  than struck. The figure asks by where it stops instead of by pulling upward.
  Voices end where they fall below one output unit rather than where the decay
  table runs out, and the decay is shifted to reach exactly zero there, which
  is what pays for a third note inside the debounce: 91 ms of 100.

## 0.5.1 — 2026-08-22

- Made the input cue three notes instead of two, and took the edge off them.
  Each note now opens on twenty milliseconds of squared attack rather than at
  full level, carries a second partial at a sixteenth of its fundamental
  instead of a quarter, and rings longer, so the ask reads as a phrase leaning
  forward rather than as strikes. The figures rise through a second onto an
  open fifth — or, once in three, climb to a major seventh and stop there —
  and each note arrives quieter than the one before. Voices now end where they
  fall under one output unit instead of where the decay table runs out, which
  pays for the third note.

## 0.5.0 — 2026-08-22

- Rebuilt the input cue in the same grammar as the other four status scores.
  Every one of them is a struck gesture over a quiet sustained bed with a soft
  tail; the ask alone was blown resonators over a loud chord that pulsed,
  leaned between chord tones and receded on its own envelope, which is why it
  never sounded like the same instrument. It is now two notes struck with the
  completion bell's voice — a fundamental, a light second partial, one
  exponential decay each — staggered so they ring together and rising into an
  answer that lifts about a semitone through its decay, over one quiet open
  chord on the shared hold envelope. It still asks rather than resolves: the
  pair never settles, and the three figures (a fourth rising to a fifth, the
  same rise a register lower, and a just major seventh that stops there)
  survive unchanged. The call moved up an octave, into the register this
  transducer actually radiates and the one completion already speaks from.

## 0.4.9 — 2026-08-22

- Rebuilt the input cue as three figures instead of one shape transposed
  through twelve interval combinations. Two of the three resolve onto open
  consonances — a fourth rising to the octave, a unison rising to the fifth —
  and the third leaps a just major seventh and stops there: a rising leading
  tone is the shape of a spoken question, so one request in three reads as an
  open question rather than as an alert. The question never descends or
  roughens, which is what keeps it distinguishable from the fault cue.
- Put the attention pad in the register opposite its notes. Where the call
  sits high the bed now drops an octave underneath it; where the call sits low
  the bed rises an octave above it, so top against bottom holds every time.
  Both layers remain ratios of one 220 Hz anchor, so they cannot drift out of
  tune with each other, and the return tail now lifts back to that figure's
  answering note rather than to a ratio of whatever root the pad happens to
  sit on.

## 0.4.8 — 2026-08-21

- Rebuilt the local screens as one instrument language: numbered rows, a single
  sprung selection plate per list, and inverted content clipped to the plate so
  the highlight no longer jumps a whole row ahead of the animation.
- Replaced the volume and host-channel readouts with segment meters that count
  their own steps, with the volume meter following the stored value on a spring.
- Gave the chime grid a two-axis sprung plate and set its pad captions solid so
  the longest name stops running under the neighbouring pad's border.
- Made task-key press feedback attack on the first frame and ease out, which
  reads as a switch rather than as a slow glow arriving late.
- Stamped the running build in the splash's top-right registration mark, read
  through the single `firmware::version()` accessor rather than a second copy.
- Rebuilt the weak-BLE notice as a corner annunciator flush with the top and
  right edges and exactly two task columns wide, instead of a badge floating
  across the middle of two towers at an arbitrary offset. A critical battery
  readout now steps aside for it and takes its contrast from the tower it
  actually lands on.
- Rebuilt the attention cue as a rising two-note call. It previously shared the
  error cue's unresolved intervals — its variation set was led by a tritone —
  and its low beating bed, so a request for input was heard as a fault. The
  call now leaps an open fifth, major sixth or octave and bends its second note
  upward, and a small ungated pad holds an open triad under the whole wait
  instead of leaving silence between the pulses. The ask no longer repeats
  itself: each recurrence steps to another tone of the chord and arrives
  quieter than the last, so the cue states itself and recedes rather than
  nagging at one pitch. Documented the interval contract for all five status
  cues. Dropped both notes of the call a full octave and gave it a short swell
  instead of an instant onset: at the old register the widest call put real
  energy where this transducer is harshest. The call is now derived from the
  pad as a suspended fourth above its root rather than carrying its own
  register, so it cannot drift out of the chord it asks over, and it leans on
  its octave partial to be heard at all down there.
- Made the attention pad the cue rather than a floor under it. It is now the
  loudest sustained element in any status score and it pulses 13 dB deep at
  twice the ask's rate, cresting on it, so every second pulse is the one that
  carries a note. Its inner voice leans between the fifth and the sixth and
  rests in between rather than sounding both forever, and changes every second
  ask. Pulse, repeat and chord change are now powers of two off one phase
  instead of three unrelated rates drifting past each other.
- Stopped the pad rasping. Its shaping values were held flat between updates,
  which is a staircase — inaudible while the modulation was shallow, and not
  once it was deepened, because the steps repeat at a kilohertz where this
  speaker is most efficient and a low chord has nothing to mask them. They are
  now slewed to each new value instead of stepped to it.
- Made the attention pad recede far earlier. It used to hold full level for the
  whole 1.85 s hold and fall only as the panel left; it now begins its decay at
  the middle of the hold — twice as early — and the fall is squared, so it is
  already down 8 dB a third of a second later and silent by the moment the old
  fade would only have started. The return tail takes over from there, so the
  chord hands off instead of being cut. Ending the pad with the hold rather than
  with the panel also removed the render-time spread it was causing: attention
  now renders in 89 ms every time, against 90-101 ms before.
- Stopped the pad breaking up on its peaks. Locking the pad's crest to the
  ask's made them sum, and the chord's weight was on a 220 Hz root, which this
  speaker answers with cone excursion rather than output. The weight moved to
  the octave: peak level fell by a third and energy at 220 Hz by more than half,
  with the pulse depth unchanged.
- Rebuilt the attention notes as bottles rather than beeps. They are blown into
  over tens of milliseconds instead of starting at full level, carry one
  resonant mode plus a second that dies well before the fundamental, and are
  excited by a breath of dulled noise. Both notes now ring together: the first
  used to be cut off mid-decay to make room for the second, which put a step of
  several hundred output units exactly where the question turns and was most of
  what made the cue feel hard. The answer's upward lift was reduced from two
  semitones to about one — a wide bend on a pure tone is a squeal — and the
  brightest remaining voices, the pad's twelfth and the ask's fifth, were
  removed. The first note of the two was then inaudible: at 293 Hz the
  fundamental barely leaves the case, and its octave had been cut to a brief
  chiff, so it is now carried by a sustained octave weighted above the
  fundamental. The higher answering note, which carries itself, keeps the short
  chiff.
- Brought every status score back inside its debounce window, so the sound is
  ready before the takeover it accompanies rather than delaying it. Envelopes
  now come from an interpolated decay table instead of `exp()`, each opening
  gesture states how long it is actually alive rather than computing silence
  for the rest of a 3.3 s score, the sustained beds are skipped outside the
  hold, and the attention pad's two sub-hertz shaping voices are held for a
  millisecond at a time. Attention 122 ms and Running 192 ms are now 86 ms and
  62 ms, with the per-play variation left untouched. A source contract keeps
  library maths out of the per-sample path.
- Added `chime`, `status` and `signal` screenshot scenes so those screens, and
  the one arrangement where the deck's two chrome elements can collide, are
  capturable.

## 0.4.7 — 2026-08-21

- Made the firmware version single-sourced from the build: `PROJECT_VER` in
  the root CMakeLists now feeds `CCP_HELLO`, `CCP_PONG`, and the OTA app
  descriptor, which had drifted apart.
- Discarded any pending diagnostic task batch when a new host session opens
  (`HOST|`), so a stale partial batch can no longer merge into the next deck,
  and made mid-batch slot overwrites observable without dropping the batch.
- Flushed pending settings before returning to M5Apps and verified the
  rollback flush when disabling USB HID fails, so neither path can lose the
  last change or resurrect a disabled mode after reboot.
- Stopped rewriting settings NVS on every host `CFG|`/`OPT|` exchange; those
  values are host-owned and were never persisted anyway.
- Replaced scattered host lamp colour literals with the byte-exact named
  constants in `main/lamp.h`.
- Removed dead BLE text-channel entry points that never had callers.

## 0.4.6 — 2026-08-21

- Treat the first Codex lighting snapshot as restored state, so reconnecting or
  rebooting cannot turn every previously completed task unread green.
- Preserve green unread feedback for real active-to-completed transitions.

## 0.4.5 — 2026-08-21

- Moved long status scores and short interface cues onto separate hardware
  mixer channels so navigation cannot cut off notifications.
- Retained and retried an armed status buffer when the speaker temporarily
  cannot accept it instead of silently advancing the animation.

## 0.4.4 — 2026-08-21

- Made selected-completion read state self-healing on every native status
  snapshot and added an animation-independent settlement fail-safe.

## 0.4.3 — 2026-08-21

- Fixed selected tasks remaining unread green when they complete during the
  short local-selection guard window.

## 0.4.2 — 2026-08-21

- Hardened native USB/BLE session ownership so one gesture cannot cross hosts
  or survive a transport reset.
- Moved HID input processing out of transport callbacks and protected release
  edges from queue pressure.
- Made settings and BLE bond persistence transactional without erasing the
  shared M5Apps NVS partition.
- Added reliable M5Apps partition discovery, retrying flash writes, read-back
  verification, and OTA selection to the development installer.
- Corrected completed-task read state: an active completion plays green and
  settles to viewed grey, while a background completion remains green until
  selected.
- Added sanitizer regressions, installer tests, handshake tests, public-tree
  auditing, and a clean ESP-IDF firmware build to CI.
- Added project formatting rules and restored readable release sources.

The release remains an independent community implementation of the native
Codex Micro protocol. No bridge, daemon, API key, or Wi-Fi connection is
required.

## 0.1.0 — 2026-08-18

- Initial public release.
