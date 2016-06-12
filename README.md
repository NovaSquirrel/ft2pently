# ft2pently
Famitracker to Pently music converter

ft2pently takes Famitracker's text exports and converts it to the Pently music engine's MML-like format.

Usage: `ft2p -i song.txt -o song.pently`

-----

As with the other music engines that offer conversions from Famitracker, composers have to limit the Famitracker effects they use.
Most effects are unsupported, as is the volume column and "pitch" and "hi-pitch" envelopes.

Supported effects:
* 0xx - arpeggio
* bxx - set loop point
* cxx - stop song
* dxx - pattern cut (xx ignored, always zero)

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
