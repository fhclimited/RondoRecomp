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
The defaults (change them on the menu's Controls page, see below):

              Controller (Xbox)     Keyboard
Move          d-pad / left stick    arrow keys      (fixed)
Jump          A                     X
Attack        X                     Z
Sub-weapon    B                     C
Item Crash    Y                     V
Backflip      RB                    A
Run           hold LB               hold S
Pause         Start                 Enter
Menu          Back / View           F1              (Esc always opens it too)
Rewind        hold LT               hold R
Save state    -                     F5
Load state    -                     F8
State slot                          F6 / F7 (slots 1-9, fixed)
Fullscreen                          F11 (fixed)

Changing a control: on the Controls page choose its row (A / Enter or a click). It counts
down from 5, and whatever you hold down when the count ends becomes the control: a
controller button (the triggers count), two controller buttons, or a key. A button or key
you press and let go during the count is taken too, if nothing is held at the end. If
another action had it, the two swap. Esc, a click, nothing pressed, or the control it
already has leaves it as it was. On a selected row, X on the controller clears its
controller button and Delete clears its key, where the game can do without it (Sub-weapon,
Backflip, Run, Rewind, Save state, Load state). Save state and load state have no controller
button until you give them one. "Reset controls" puts every control back. The
menus themselves always use the d-pad / stick / arrows, A / Enter and B / Esc.

Two buttons make a combination, such as Back + RB for Save state and Back + LB for Load
state: during the count hold Back, then press RB, and keep both down until it ends. In play,
hold the first button and press the second. The second button then does only the
combination, so RB and LB on their own still backflip and run. A button that starts a
combination does its own job when you let go of it without pressing a second one: with the
example above, Back opens the menu when released instead of when pressed.

Sub-weapon presses Up + Attack for you, Item Crash is the original SELECT button, and
Backflip presses Jump twice quickly (as Maria it gives her double jump). The original
inputs (Up + Attack, Jump twice) work as well.

The PC Engine's start button was called RUN. The game's title screen says PUSH START
BUTTON! when you are playing with a controller and PUSH ENTER KEY! on the keyboard (in
place of the original PUSH RUN BUTTON!), going by what you pressed last; PUSH PAUSE
BUTTON! / KEY! if you gave Pause another control.

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
           VSync: on (the default), each frame is shown when the screen refreshes, so the
           picture never tears; off shows it at once
           Aspect ratio: 4:3 (as on a TV), square pixels, or stretched to fill the screen
           Size: Fit screen (as big as the screen allows), or Whole multiples (every game
           pixel the same size; leaves a border)
           Filter: Sharp (crisp, even pixels), Nearest, or Smooth
           Screen effect: Off, Scanlines, CRT (scanlines, phosphor stripes and a glow),
           Curved CRT (the same on curved glass, darker at the corners), or Composite (a
           soft TV picture); Effect strength: Light, Medium or Strong
           Beside the picture: when the picture is narrower than the screen, a blurred
           copy of it, or black
Gameplay   Skip cinematics: Press START, Automatically, or Never. During the logo and
           the intro (the story narration and opening before the title screen), START, A
           or X goes straight to the title screen, the way the game itself allows once it
           has shown them; Automatically goes there at once. Story scenes in the game have
           no skip of their own: START (or Automatically) runs them through fast instead
           Rewind on/off, volume, Start screen at launch on/off
Cheats     Infinite health, infinite hearts (always 99), infinite lives
           Maria unlocked: the game offers Maria as it does after you rescue her in Stage 2
           (and your save keeps it)
           Play as Maria: every stage is played as Maria, the Prologue too (starts from the
           next stage you enter)
Language   English patch or Japanese original (each is its own disc; see below)

Options are kept in %APPDATA%\RondoRecomp\RondoRecomp\settings.ini.

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
