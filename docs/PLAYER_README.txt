RondoRecomp
===========

Turns your own copy of Castlevania: Rondo of Blood (PC Engine Super CD-ROM2) into a
native Windows program. No game files are included: you provide your own disc image.

What you need
-------------
A zip of your disc image: the .cue file together with the track files it lists
(.bin, .img, .iso and/or .wav). The files may be inside a folder in the zip.
Either disc works: the original Japanese release (Akumajou Dracula X: Chi no Rondo) or
the English fan translation. With both, you can switch languages in the game.

How to use it
-------------
1. Unpack this folder anywhere you can write to (not Program Files).
2. Run "RondoRecomp Setup.exe" and click "Choose disc zip...".
3. Setup checks the disc, extracts it to the game folder, recompiles the game's code
   into native code and builds rondo.exe in this folder. This takes about a minute
   (the first build prepares the bundled C compiler's runtime once).
4. Click Play. From then on, start rondo.exe directly.
To add the other language, run Setup again and choose that disc's zip: it is installed
next to the first (game\en and game\ja) instead of replacing it.

Controls
--------
              Controller (Xbox)     Keyboard
Move          d-pad / left stick    arrow keys
Jump          A                     X
Attack        X                     Z
Sub-weapon    B                     C
Item Crash    Y                     V
Backflip      RB                    A
Run           hold LB               hold S
Pause         Start                 Enter
Menu          Back / View           F1 or Esc
Rewind        hold LT               hold R
Map           RT / right stick      M or Tab   (Remaster mode)
Save state                          F5
Load state                          F8
State slot                          F6 / F7 (slots 1-9)
Fullscreen                          F11

Sub-weapon presses Up + Attack for you, Item Crash is the original SELECT button, and
Backflip presses Jump twice quickly (as Maria it gives her double jump). The original
inputs (Up + Attack, Jump twice) work as well.

Run is Richter's run from Symphony of the Night: hold LB while walking to go about 1.8
times as fast (his run there is 2.3 pixels a frame against a 1.25 walk; Rondo's walk is the
same 1.25). Jumps started while running keep the speed. Maria runs too.

Start screen
------------
rondo.exe opens on a start screen; the game itself starts only when you choose Play
(START, Enter, A, or a click; it goes on to the game's title screen, past the logo and
intro, unless Skip cinematics is Never). The menu also has the language, loading a save
state, the option pages below, Setup (to add the other language's disc or rebuild;
rondo.exe closes so Setup can rebuild it) and Quit. Controller, keyboard and mouse all
work; the line at the bottom of the menu says what the selected item does. Menu, Start
screen comes back to it. Gameplay, Start screen at launch: Off goes straight into the
game (rondo.exe --play does that once).

Menu
----
F1, Esc or the controller's Back button pauses the game and opens the menu: save and load
states, choose the slot, reset, back to the start screen, quit, and:

Video      Fullscreen (uses the screen's own resolution, e.g. 4K)
           Mode: Classic or Remaster; see below (also on the start screen)
           Aspect ratio: 4:3 (as on a TV), square pixels, or stretched to fill the screen
           Size: Fit screen (as big as the screen allows), or Whole multiples (every game
           pixel the same size; leaves a border)
           Filter: Sharp (crisp, even pixels), Nearest, or Smooth
           Beside the picture: when the picture is narrower than the screen, a blurred
           copy of it, or black
Gameplay   Skip cinematics: press START during a story scene or the power-on intro (the
           story narration and opening before the title screen) to skip it, or skip them
           automatically, or never
           Rewind on/off, volume, Start screen at launch on/off
Cheats     Infinite health, infinite hearts (always 99), infinite lives
           Maria unlocked: the game offers Maria as it does after you rescue her in Stage 2
           (and your save keeps it)
           Play as Maria: every stage is played as Maria, the Prologue too (starts from the
           next stage you enter)
Language   English patch or Japanese original (each is its own disc; see below)

Options are kept in %APPDATA%\RondoRecomp\RondoRecomp\settings.ini.

Classic and Remaster
--------------------
Classic is the game as it was: the 4:3 picture, filling the screen's height (with a
blurred copy or black beside it), with square pixels, or stretched to fill the window
(Video > Aspect ratio).

Remaster fills a wide screen with the level:
- Where the camera scrolls, the game draws more of the level on both sides: the
  background, enemies (they appear and move further out, where they are in the level) and
  items, with the score display moved to the left edge. Up to 21:9 (about 112 extra game
  pixels each side) is the game itself.
- Around that, the rooms next door are drawn in: each stage's rooms are laid side by side
  the way the game takes you between them (the end of Stage 1's burning street joins the
  staircase house), so beyond a room's walls, or beside a room that does not scroll (a
  vertical shaft, a boss room), you see the room you came from or are going to. A room
  change glides across instead of cutting. These pictures fill in as you play: a room you
  have not been to yet is dark until you have, and everything you have seen is kept (with
  your saves), so on your next visit the stage is drawn in from the start. What is drawn
  this way is a picture of the room's background; the enemies in it come to life as the
  camera reaches them, as in the game.
- The map: RT (or M / Tab) pauses the game, blurs it softly and shows the stage's map:
  the rooms you have been through in grey with white outlines (the parts of the room you
  have seen lighter), and you as a blinking yellow dot. Press it again (or B / Esc / Start)
  to go on. The map is saved too.
Turning Remaster on takes effect from the next room. Where the game parks something just
off its 4:3 screen to bring it on later (the Prologue's carriage waits off the left edge
while the forest goes by), it stays out of sight until it comes in. Cinematics and menus
keep the original picture.

Your own icon
-------------
RondoRecomp includes no game artwork. To give rondo.exe an icon, put an image named
icon.png (up to 256 x 256 pixels) or icon.ico next to "RondoRecomp Setup.exe" and run
Setup (or Rebuild); it is built into rondo.exe and shows on its window and taskbar button.

Rewind plays the game backwards while held (faster after 3 seconds) and continues from
where you let go; up to 30 minutes are kept. Rewinding never changes your saved game.

In-game saves are kept in %APPDATA%\RondoRecomp\RondoRecomp\bram.bin (bram-ja.bin for
Japanese), save states in %APPDATA%\RondoRecomp\RondoRecomp\states\.
To keep a separate set (options, saves, states), set the environment variable
RONDORECOMP_PROFILE to a folder before starting Setup or rondo.exe; your usual ones are
left alone.

Japanese and English
--------------------
The game's text and subtitles come from the disc image: Japanese is the original disc
(Akumajou Dracula X: Chi no Rondo), English is the fan translation of it. rondo.exe starts
with whichever disc Setup installed (the language chosen in the menu, if both are). Menu,
Language switches and restarts the game; if that language's disc is not installed, it asks
for its .cue file once and remembers it. The Japanese story text uses the System Card's
kanji font, which RondoRecomp supplies (Shinonome, a public-domain bitmap font), so no
System Card image is needed. Both discs run the same recompiled code.

Rebuild
-------
rondo.exe notes any game code it had to run without recompiled code in misses.txt.
"Rebuild" in Setup recompiles again including that code. rondo.log shows how much ran
natively in the last session.

Command line
------------
rondo_setup.exe <disc.zip | disc.cue>     the same steps without a window
rondo_setup.exe --rebuild                 recompile again with misses.txt

Status
------
Work in progress. Music, sound effects and voices all play.

Third-party software: see THIRD_PARTY.txt.
