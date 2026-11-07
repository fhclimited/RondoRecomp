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
             Keyboard                  Controller
D-pad        arrow keys                d-pad / left stick
I (jump)     X                         B (East)
II (attack)  Z                         A (South)
RUN          Enter                     Start / Menu
SELECT       Backspace / Right Shift   Back / View
Rewind       hold R                    hold left trigger or left shoulder
Save state   F5
Load state   F8
State slot   F6 / F7 (slots 1-9)
Fullscreen   F11
Quit         Esc

Rewind plays the game backwards while held (faster after 3 seconds) and continues from
where you let go; up to 30 minutes are kept. Rewinding never changes your saved game.

In-game saves are kept in %APPDATA%\RondoRecomp\RondoRecomp\bram.bin, save states in
%APPDATA%\RondoRecomp\RondoRecomp\states\.

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
