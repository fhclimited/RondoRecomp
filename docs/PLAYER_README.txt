RondoRecomp
===========

Turns your own copy of Castlevania: Rondo of Blood (PC Engine Super CD-ROM2) into a
native Windows program. No game files are included: you provide your own disc image.

What you need
-------------
A zip of your disc image: the .cue file together with the track files it lists
(.bin, .iso and/or .wav). The files may be inside a folder in the zip.

How to use it
-------------
1. Unpack this folder anywhere you can write to (not Program Files).
2. Run "RondoRecomp Setup.exe" and click "Choose disc zip...".
3. Setup checks the disc, extracts it to the game folder, recompiles the game's code
   into native code and builds rondo.exe in this folder. This takes about a minute
   (the first build prepares the bundled C compiler's runtime once).
4. Click Play. From then on, start rondo.exe directly.

Controls
--------
              Controller (Xbox)     Keyboard
Move          d-pad / left stick    arrow keys
Jump          A                     X
Attack        X                     Z
Sub-weapon    B                     C
Item Crash    Y                     V
Backflip      RB                    A
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

Menu
----
F1, Esc or the controller's Back button pauses the game and opens the menu: save and load
states, choose the slot, reset, quit, and:

Video      Fullscreen (uses the screen's own resolution, e.g. 4K)
           Aspect ratio: 4:3 (as on a TV), square pixels, or stretched to fill the screen
           Integer scaling: whole-number zoom only
           Filter: Sharp (crisp, even pixels), Nearest, or Smooth
           Wide-screen sides: on 16:9 and ultrawide screens the space beside the 4:3 picture
           shows a blurred copy of the picture, or black
Gameplay   Skip cinematics: press START during a story scene to skip it, or skip them
           automatically, or never
           Rewind on/off, volume
Cheats     Infinite health, infinite hearts (always 99), infinite lives
Language   English or Japanese (needs your Japanese disc image too; see below)

Options are kept in %APPDATA%\RondoRecomp\RondoRecomp\settings.ini.

Rewind plays the game backwards while held (faster after 3 seconds) and continues from
where you let go; up to 30 minutes are kept. Rewinding never changes your saved game.

In-game saves are kept in %APPDATA%\RondoRecomp\RondoRecomp\bram.bin (bram-ja.bin for
Japanese), save states in %APPDATA%\RondoRecomp\RondoRecomp\states\.

Japanese
--------
The game's text and subtitles come from the disc image, so Japanese needs the original
Japanese disc (Akumajou Dracula X: Chi no Rondo). Choose Language: Japanese in the menu and
select its .cue file once; switching restarts the game in that language. Code that differs
from the disc Setup recompiled runs on the built-in interpreter (still full speed).

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
