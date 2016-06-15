# ft2pently
Famitracker to Pently music converter

ft2pently takes Famitracker's text exports and converts it to the Pently music engine's MML-like format.

Usage: `ft2p -i song.txt -o song.pently`

-----

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
* The default duty for square channels is 50%, not 12.5%
* An instrument's envelope will only last as long as the volume envelope

-----

Attack channel:

Pently supports sharing one channel between two different patterns. To use it in ft2pently, set the module to MMC5 and puts notes on the first square channel. To specify what channel is being interrupted by the attack, use the Jxx effect, with 0 for the first square channel, 1 for the second square channel, or 2 for the triangle channel.

The channel being interrupted should be on a sustain by the time the the attack happens, because it won't resume the previous attack.

-----

Meta information is sent to the converter via comments:
```
include drums.pently
drum c3 tkick
drum C3 tsnare
drum d3 clhat
```

`drum` specifies that a given DPCM channel note and octave corresponds to a given drum in Pently. Sharps are denoted with a capital letter.

`include` reads another file and dumps it right into the output file along with the conversion, for sound effects and drums and such.
