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
rondo.exe opens on a start screen, with the game's own attract mode running behind it:
Play (START, Enter, A, or a click; it goes on to the game's title screen, past the logo and
intro, which play behind the start screen), the language, loading a save state, the option pages
below, Setup (to add the other language's disc or rebuild; rondo.exe closes so Setup can
rebuild it) and Quit. Controller, keyboard and mouse all work. Menu, Start screen comes
back to it. Gameplay, Start screen: Off goes straight into the game (rondo.exe --play
does that once).

Menu
----
F1, Esc or the controller's Back button pauses the game and opens the menu: save and load
states, choose the slot, reset, back to the start screen, quit, and:

Video      Fullscreen (uses the screen's own resolution, e.g. 4K)
           Widescreen: More of the level, or Off (4:3); see below
           Aspect ratio: 4:3 (as on a TV), square pixels, or stretched to fill the screen
           Size: Fit screen (as big as the screen allows), or Whole multiples (every game
           pixel the same size; leaves a border)
           Filter: Sharp (crisp, even pixels), Nearest, or Smooth
           Beside the picture: when the picture is narrower than the screen, a blurred
           copy of it, or black
Gameplay   Skip cinematics: press START during a story scene or the power-on intro (the
           story narration and opening before the title screen) to skip it, or skip them
           automatically, or never
           Rewind on/off, volume, Start screen on/off
Cheats     Infinite health, infinite hearts (always 99), infinite lives
           Maria unlocked: the game offers Maria as it does after you rescue her in Stage 2
           (and your save keeps it)
           Play as Maria: every stage is played as Maria, the Prologue too (starts from the
           next stage you enter)
Language   English or Japanese (each is its own disc; see below)

Options are kept in %APPDATA%\RondoRecomp\RondoRecomp\settings.ini.

Widescreen
----------
On 16:9 and ultrawide screens the game draws more of the level on both sides: the
background, enemies (they appear and move further out, where they are in the level) and
items, with the score display moved to the left edge. Up to 21:9 (about 112 extra game
pixels each side) is covered; wider screens get the rest beside the picture.
Rooms the camera does not scroll through sideways (vertical shafts, boss rooms) keep the
original 4:3 picture, as do cinematics and menus. Beyond the walls of a room is black.
Turning widescreen on takes effect from the next room.

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
