ft2pently
=========
Famitracker to Pently music converter

ft2pently takes Famitracker's text exports and converts it to the Pently music engine's MML-like format.

Limitations
-----------------------
As with the other music engines that offer conversions from Famitracker, composers have to limit the Famitracker effects they use.
Most effects are unsupported, as is the volume column and "pitch" and "hi-pitch" envelopes.

Supported effects:
* 0xy - arpeggio
* Bxx - set loop point (loop to frame xx)
* Cxx - stop song
* Dxx - pattern cut (xx ignored, always zero)
* Fxx - tempo/speed change
* Gxx - delay note start
* Sxx - delay note cut (not usable on empty rows yet)
* 300 - disable slur
* 3xx - enable slur (if x is nonzero)
* Qxy - play note for one row then slur up Y semitones
* Rxy - play note for one row then slur down Y semitones

Things to keep in mind:
* Always define a duty envelope for square instruments, even if it's 12.5%. In Pently, an unspecified duty cycle is 50%, so a duty envelope needs to be defined.
* An instrument's envelope will only last as long as the volume envelope. If your instrument's arpeggio or duty envelopes are longer than the volume envelope and you want the whole arpeggio/duty envelope to play, extend the volume envelope to match.
* Triangle channel volume is still used for determining whether a note should be interrupted by a sound effect or not.
* Triangle instrument duty should NOT be 50% or the note will cut prematurely.

Attack channel
--------------

Pently supports sharing one channel between two different patterns. To use it in ft2pently, set the module to MMC5 and puts notes on the first square channel. To specify what channel is being interrupted by the attack, put the Jxx effect in the track, with 0 for the first square channel, 1 for the second square channel, or 2 for the triangle channel.

The channel being interrupted must be at the end of its volume envelope by the time the attack happens, because it won't resume the earlier attack.

Drums
-----
Pently's drum implementation plays a sound effect on up to two channels, which has no direct representation in Famitracker. In a later version of ft2pently you will be able to choose to automatically convert the noise track into drums, but currently you must use the DPCM channel.

Set up the DPCM instrument as you usually would, with samples assigned to different notes. The samples aren't actually used in the conversion, but will let you hear what the drum section of the song sounds like while composing it.

You need to make a file containing drum definitions for pentlyas. [the pentlyas manual](https://github.com/Qix-/pently/blob/master/docs/pentlyas.md) covers how to define drums. Drums that already sound nice can be found in [the sample songs](https://github.com/Qix-/pently/blob/master/src/musicseq.pently).

Now that the sound of the drums are defined, ft2pently needs to know what DPCM channel notes correspond to which drums. This is done by putting commands for ft2pently in the song's comments section, reached with `Modules -> Comments` from the menu.

A sample drum configuration is as follows (with explanation):

```
include drums.pently
drum c3 tkick
drum C3 tsnare
drum d3 clhat
```

`include` reads another file and dumps it right into the output file along with the conversion, for sound effects and drums and such. Here it is including the drum definitions.

`drum` specifies that a given DPCM channel note and octave corresponds to a given drum in Pently. Sharps are denoted with a capital letter. Here, C, C# and D in octave 3 are used.

Converting the song
-------------------
In Famitracker, either use `File -> Export text` from the menu, or `famitracker.exe song.ftm -export song.txt` from a terminal to make a text export of the song.

Now, to run ft2pently: `ft2p -i song.txt -o song.pently`

This output will need to be run through `pentlyas` to result in something the Pently engine can use.
