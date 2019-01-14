ft2pently
=========
Famitracker to Pently music converter

ft2pently takes Famitracker's text exports and converts it to the Pently music engine's MML-like format.

Windows binaries are available [on the release page](https://github.com/NovaSquirrel/ft2pently/releases).

Limitations
-----------------------
As with the other music engines that offer conversions from Famitracker, composers have to limit the Famitracker effects they use, though Pently is much less limiting than say, Famitone2.
Most effects are unsupported, as are the "pitch" and "hi-pitch" envelopes.
If using 0CC Famitracker, you can get a better approximation of how vibrato sounds in Pently by setting it to linear pitch.

See also [https://wiki.nesdev.com/w/index.php/Audio_drivers#Pently this list].

Supported effects:
* 0xy - Arpeggio
* Bxx - Set loop point (loop to frame xx)
* Cxx - Stop song
* Dxx - Pattern cut (xx ignored, always zero)
* Fxx - Tempo/speed change
* Gxx - Delay note start
* Sxx - Delay note cut
* 300 - Disable slur
* 3xx - Enable slur (if x is nonzero)
* 4xy - Vibrato of depth Y. X (speed) is ignored; use 5 to approximate Pently's vibrato speed in Famitracker. Valid depths are 0 through 4, where 4 is very strong and 0 is disabled.
* Qxy - Play note for one row then slur up Y semitones
* Rxy - Play note for one row then slur down Y semitones

Things to keep in mind:
* Always define a duty envelope for square instruments, even if it's 12.5%. In Pently, an unspecified duty cycle is 50%, so a duty envelope needs to be defined.
* An instrument's envelope will only last as long as the volume envelope. If your instrument's arpeggio or duty envelopes are longer than the volume envelope and you want the whole arpeggio/duty envelope to play, extend the volume envelope to match.
* Triangle channel volume is still used for determining whether a note should be interrupted by a sound effect or not.
* Triangle instrument duty must be 50% (or unspecified, which defaults to 50%) or the note will cut prematurely.
* The volume column is supported, but can only do 25%, 50%, 75% and 100% volume rather than the range Famitracker has. 0-6 maps to 25%, 6-9 maps to 50%, A-C maps to 75% and D-F maps to 100%.

Attack channel
--------------

Pently supports sharing one channel between two different patterns. To use it in ft2pently, set the module to MMC5 and puts notes on the first square channel. To specify what channel is being interrupted by the attack, put the Jxx effect in the track, with 0 for the first square channel, 1 for the second square channel, or 2 for the triangle channel.

The channel being interrupted must be at the end of its volume envelope by the time the attack happens, because it won't resume the earlier attack.

An instrument WILL return to a decay if it was interrupted during a decay, however. See the "Auto decay" section in this manual for information on how to use decay.

Drums
-----

Drums are probably the area where Pently differs from Famitracker's model the most, so ft2pently needs some help to convert them. Instead of a generic noise channel, Pently has a "drum" channel that is based around sound effects, allowing the drum patterns to be stored very efficiently. There is a hard limit on 25 drum types, so keep this in mind!

Alongside the space savings, Pently's drum system allows for drums that use two channels at once, most commonly using noise and triangle together. This allows for very good sounding drums even without DPCM, and unlike the usual case with this strategy you don't need to mess with the triangle channel in Famitracker to "bake" drums into the triangle patterns.

There are a few different choices for how you specify drums in your FTM file:
* Auto noise: Insert `auto noise` in your FTM file's comments. Compose a noise channel track normally in Famitracker, and each pitch a noise instrument is played at becomes a new Pently drum automatically. Recommended if you just want to quickly get things to work.
* Auto dual drums: Insert `auto dual drums` in your FTM file's comments. Use `fixed` arpeggio envelopes on your noise and triangle instruments you want to use for drums, create the noise track as normal and insert Jxx effects, where the xx selects a corresponding triangle instrument. Look at Nova the Squirrel's repository for an example of this style.
* Assigning drums to specific DPCM channel pitches (see next section)

ft2pently can handle drums three different ways. If your game's drums are just noise instruments and you're happy with them, just put `auto noise` in the .ftm's comments and you're done!

Keep in mind that Pently has a hard limit of 25 drums (because drums map to notes, and Pently can only see about two octaves at a time), and each frequency a noise instrument gets used at counts as another drum, so be careful not to use too many. Also, keep in mind that if `auto noise` mode is on, the DPCM channel in the .ftm is ignored. The noise channel and DPCM channels don't mix together.

Using native Pently drums
-------------------------

Set up the DPCM instrument as you usually would, with samples assigned to different notes. The samples aren't actually used in the conversion, but will let you approximate what the drum section of the song will sound like as you're composing it.

You need to make a file containing drum definitions for pentlyas. [The pentlyas manual](https://github.com/Qix-/pently/blob/master/docs/pentlyas.md) covers how to define drums. Drums that already sound nice can be found in [the sample songs](https://github.com/Qix-/pently/blob/master/src/musicseq.pently).

Now that the sound of the drums are defined, ft2pently needs to know what DPCM channel notes correspond to which drums. This is done by putting commands for ft2pently in the song's comments section, reached with `Modules -> Comments` from the menu. (Make sure to have a blank line at the end of the comments)

A sample drum configuration is as follows (with explanation):

```
include drums.pently
drum c3 tkick
drum c#3 tsnare
drum d3 clhat
```

`include` reads another file and dumps it right into the output file along with the conversion, for sound effects and drums and such. Here it is including the drum definitions.

`drum` specifies that a given DPCM channel note and octave corresponds to a given drum in Pently. Here, C, C# and D in octave 3 are used.

Converting instruments to drums
-------------------------------

Instead of importing drums, you can define drums using instruments. The DPCM channel must still be used as described in the previous section. As stated above, drums consist of one or two sound effects. For the arpeggio envelope, use the `fixed` type.

```
sfx 01 t tri_kick
sfx 02 n noise_kick
drumsfx tkick tri_kick noise_kick
drum c3 tkick
```

`sfx` defines a new sound effect. It takes an instrument number (hexadecimal), a channel (s, t or n for square/pulse, triangle, or noise respectively), and a name to give the new sound effect (alphanumeric and underscores only).

`drumsfx` defines a new drum, using one or two sound effects. It takes the drum name, and then the names of the sound effect(s) used. Same naming restrictions.

`drum` works as before.

Auto decay
----------
Pently can split an instrument into "attack" and "decay" sections, with the attack being a Famitracker-ish volume envelope and the decay being a linear slope down towards silence. This saves space, as a long fadeout at the end of an instrument does not need to be in the ROM.

To use this feature, add a line containing "auto decay" to the .ftm's comments.
Use my [decay envelope generator page](http://t.novasquirrel.com/test/decay.html) to create a decay envelope, and then paste the generated envelope onto the end of a volume envelope.
A volume envelope may contain a decay and nothing else, if you don't want to use an attack.

Important note: Auto decay will not activate for a given instrument if it would interfere with the duty and/or arpeggio envelopes. At the point in the volume envelope where the decay envelope starts, the duty and arpeggio envelopes must have already completed. This also means that those envelopes cannot be looped.

Command line arguments
----------------------
By default, ft2pently will only warn about unsupported effects. To make it give an error and stop instead, add the `-strict` flag.

Errors will display row numbers in decimal by default, but you can choose hex row numbers with `-hexrow`.

Auto noise and auto decay can be turned on from the command line with `-autonoise` and `-autodecay` if you prefer that over comments.

`-dotted` will make ft2pently use '.'s when writing durations.

Converting the song
-------------------
In Famitracker, either use `File -> Export text` from the menu, or `famitracker.exe song.ftm -export song.txt` from a terminal to make a text export of the song.

Now, to run ft2pently: `ft2p -i song.txt -o song.pently`

This output will need to be run through `pentlyas` to result in something the Pently engine can use.
