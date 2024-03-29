/*
 * ft2pently
 *
 * Copyright (C) 2016-2018 NovaSquirrel
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
// https://github.com/Qix-/pently/issues/4
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include <stdarg.h>

// maximum values, used for array sizes
#define MAX_EFFECTS     4
#define MAX_ROWS        256
#define MAX_FRAMES      128  // real max is 128
#define MAX_PATTERNS    128  // real max is 128
#define MAX_INSTRUMENTS 64   // pently limit is 256/5
#define MAX_MACRO_LEN   255  // real max is like 254?
#define NUM_OCTAVES     7
#define NUM_SEMITONES   12
#define MAX_SFX         64
#define MAX_DECAY_START 15   // starting volume
#define MAX_DECAY_RATE  16   // decay rate
#define MAX_DECAY_LEN   256  // actually goes up to 224 or something but this is to be safe
#define MAX_SONGS       64
#define SONG_NAME_LEN   32
#define MAX_DRUMS       25

//////////////////// constants ////////////////////
const char *scale = "cCdDefFgGaAb";
const char *supported_effects = ".034BCDFGQRSJ";
const char *chan_name[] = {"pulse1", "pulse2", "triangle", "noise", "drum", "attack"};
const char *envelope_types[] = {"volume", "arpeggio", "pitch", "hipitch", "duty"};

//////////////////// enums and structs ////////////////////
// sound channels
enum {
  CH_SQUARE1,
  CH_SQUARE2,
  CH_TRIANGLE,
  CH_NOISE,
  CH_DPCM,      // not supported, but used for Pently drums
  CH_ATTACK,    // MMC5's first expansion square channel
  CHANNEL_COUNT
};

// types of envelopes
enum {
  MS_VOLUME,
  MS_ARPEGGIO,
  MS_PITCH,
  MS_HIPITCH,
  MS_DUTY,
  MACRO_SET_COUNT
};

// arpeggio types
enum {
  ARP_ABSOLUTE,
  ARP_RELATIVE,
  ARP_FIXED
};

// supported note effects
enum {
  FX_NONE     = 0, 
  FX_ARP      = '0', // arpeggio
  FX_SLUR     = '3', // enable slur if nonzero
  FX_VIBRATO  = '4', // vibrato change
  FX_LOOP     = 'B', // jump to frame X
  FX_FINE     = 'C', // stop song
  FX_PAT_CUT  = 'D', // skip to next pattern
  FX_TEMPO    = 'F', // change tempo or speed
  FX_DELAY    = 'G', // delay for X frames
  FX_SLUR_UP  = 'Q', // note for one row, slur into pitch X semitones up
  FX_SLUR_DN  = 'R', // note for one row, slur into pitch X semitones down
  FX_DELAYCUT = 'S', // grace note for X frames then rest
  FX_ATTACK_ON= 'J'  // repurposed to specify attack target
};

// volumes
enum {
  VOL_SAME, // no change
  VOL_FF,   // 100%
  VOL_MF,   // 75%
  VOL_MP,   // 50%
  VOL_PP,   // 25%
};

// a sound effect definition, held onto until end of export
// used for drums and auto-generated noise drums
typedef struct soundeffect {
  uint8_t instrument, channel;
  uint8_t pitch; // pitch offset for auto noise, or 0
  char name[64];
} soundeffect;

// a note on a pattern
typedef struct ftnote {
  uint8_t octave;             // octave number
  char note;                  // note name, capitalized if sharp
  int8_t instrument;          // instrument number
  uint8_t volume;             // note volume, uses VOL_* values
  char effect[MAX_EFFECTS];   // effect letter
  uint8_t param[MAX_EFFECTS]; // effect parameter
  uint8_t slur;               // nonzero if note has slur
} ftnote;

// a song and its patterns
typedef struct ftsong {
  // Explicitly stated song information
  char real_name[SONG_NAME_LEN]; // name to display for errors
  char name[SONG_NAME_LEN];      // sanitized name for the actual file
  int rows, speed, tempo;

  // Buffers to hold song information
  int frame[MAX_FRAMES][CHANNEL_COUNT];
  ftnote pattern[MAX_PATTERNS][CHANNEL_COUNT][MAX_ROWS];
  uint8_t pattern_used[MAX_PATTERNS][CHANNEL_COUNT];
  int pattern_length[MAX_PATTERNS][CHANNEL_COUNT];
  int effect_columns[CHANNEL_COUNT]; // number of effect columns
  int loop_to;                       // frame to insert the segno at, or -1 for no looping

  // Song status information for parsing purposes
  int pattern_id, frames;
} ftsong;

// an instument envelope
typedef struct ftmacro {
  int8_t length, loop, release;
  int8_t arp_type; // relative/absolute? not used by ft2pently
  int8_t sequence[MAX_MACRO_LEN];
  uint8_t decay_rate;   // if 0, decay isn't used
  uint8_t decay_volume; // starting volume to use for decay
  uint8_t decay_index;  // index decay starts 
} ftmacro;

//////////////////// functions ////////////////////

// creates a note with some given information
ftnote make_note(uint8_t octave, char note, int8_t instrument) {
  ftnote new_note;
  memset(&new_note, 0, sizeof(new_note));
  new_note.octave = octave;
  new_note.note = note;
  new_note.instrument = instrument;
  return new_note;
}

// returns 1 for channels that have notes and a pitch
static inline int channel_is_pitched(int channel) {
  return channel != CH_DPCM && channel != CH_NOISE;
}

// convert a note and an octave into a semitone number
int note_to_semitone(char note, int octave) {
  return (strchr(scale, note)-scale)+(octave*NUM_SEMITONES);
}

// convert the number back to a note name and octave
void semitone_to_note(int semitone, char *note, uint8_t *octave) {
  *note = scale[semitone % NUM_SEMITONES];
  *octave = semitone / NUM_SEMITONES;
}

// offsets a note by a given number of semitones
void shift_semitones(ftnote *note, int offset) {
  if(!isalpha(note->note))
    return;
  // convert to semitones so I can just shift the integer value
  int semitones = note_to_semitone(note->note, note->octave);
  // add to the integer value
  semitones += offset;
  // change back to a note
  semitone_to_note(semitones, &note->note, &note->octave);
}

// asserts that a value is in a given range
void check_range(const char *name, int value, int low, int high, const char *location) {
  if(value >= low && value < high)
    return;
  printf("Error: %s out of range (%i, must be below %i) %s\n", name, value, high, location?location:"");
  exit(-1);
}

// like strncpy but good
void strlcpy(char *Destination, const char *Source, int MaxLength) { 
  // MaxLength is directly from sizeof() so it includes the zero
  int SourceLen = strlen(Source);
  if((SourceLen+1) < MaxLength)
    MaxLength = SourceLen + 1;
  memcpy(Destination, Source, MaxLength-1);
  Destination[MaxLength-1] = 0;
}

// removes one line ending if found
void remove_line_ending(char *text, char ending) {
  text = strrchr(text, ending);
  if(text)
    *text = 0;
}

// removes \n, \r or " if found on the end of a string
void remove_line_endings(char *buffer) {
  remove_line_ending(buffer, '\n');
  remove_line_ending(buffer, '\r');
  remove_line_ending(buffer, '\"');
}

// makes a label-friendly version of a name
char *sanitize_name(char *outbuf, const char *input, int length) {
  char hex[3];
  char temp[strlen(input)*2+1];
  char *output = temp;

  if(!isalpha(*input) && *input != '_') // names usually have to start with an letter
    *(output++) = '_';
  while(*input) {
    if(isalnum(*input))    // copy directly if alphanumeric
      *(output++) = *input;
    else if(*input == ' ' || *input == '-' || *input == '_') { // change certain characters to underscores
      *(output++) = '_';
    } else {               // escape other characters into their hexadecimal code
      sprintf(hex, "%.2x", *input);
      strcpy(output, hex);
      output += 2;
    }
    input++;
  }
  *(output) = 0;

  strlcpy(outbuf, temp, length);
  return outbuf;
}

// return 1 if a string starts with another specific string
int starts_with(char *string, const char *start, char **arg) {
  // optionally, set a pointer to the spot in the string after the text being matched against
  if(arg)
    *arg = string+strlen(start);
  return !memcmp(string, start, strlen(start));
}

// increases a pointer until it gets to a digit or a dash
char *skip_to_number(char *string) {
  while(*string && !isdigit(*string) && *string!='-')
    string++;
  return string;
}

//////////////////// global variables ////////////////////
ftsong song;  // song being parsed
ftsong xsong; // song being exported

// module parsing state
int song_num = 0, sfx_num = 0;
int8_t instrument[MAX_INSTRUMENTS][MACRO_SET_COUNT];
uint8_t instrument_used[MAX_INSTRUMENTS];
uint8_t instrument_ignore[MAX_INSTRUMENTS];
int num_auto_drums = 0;
uint8_t auto_drum_noise[MAX_DRUMS];
uint8_t auto_drum_tri[MAX_DRUMS];
ftmacro instrument_macro[MACRO_SET_COUNT][MAX_INSTRUMENTS];
char instrument_name[MAX_INSTRUMENTS][32];
uint16_t instrument_noise[MAX_INSTRUMENTS]; // each bit in each 16-bit value corresponds to a needed frequency
char drum_name[NUM_OCTAVES][NUM_SEMITONES][16];
soundeffect soundeffects[MAX_SFX];
int duplicate_name_counter = 0;
char song_name[MAX_SONGS][SONG_NAME_LEN]; // exists solely to check for duplicates

// export options
int decay_enabled = 0;    // use the decay feature
int auto_noise = 0;       // automatically convert noise instruments to drums
int auto_dual_drums = 0;  // automatically convert fixed arpeggio noise instruments to drums (with triangle part)
int hex_rows = 0;         // display row numbers in hex instead of decimal
int strict = 0;           // turn warnings into errors
int tri_sxx_to_cut = 0;   // convert delayed triangle note cuts to regular note cuts
int dotted_durations = 0; // use dotted durations in the output file
char decay_envelope[MAX_DECAY_START][MAX_DECAY_RATE][MAX_DECAY_LEN]; // pre-calculated decay tables
const char *in_filename, *out_filename;

// displays a warning or an error
void error(int stop, const char *fmt, ...) {
    if(strict)
      stop = 1;
    va_list args;
    va_start(args, fmt);
    printf((stop)?"Error: ":"Warning: ");
    vprintf(fmt, args);
    putchar('\n');
    va_end(args);
    if(stop)
      exit(-1);
}

// creates a string that describes a location in a song
const char *error_location(ftsong *the_song, int channel, int pattern, int row) {
  static char buffer[200];

  if(hex_rows) {
    if(row == -1)
      sprintf(buffer, "[%s - %s pattern $%x]", the_song->real_name, chan_name[channel], pattern);
    else
      sprintf(buffer, "[%s - %s pattern $%x row $%x]", the_song->real_name, chan_name[channel], pattern, row);
  } else {
    if(row == -1)
      sprintf(buffer, "[%s - %s pattern %i]", the_song->real_name, chan_name[channel], pattern);
    else
      sprintf(buffer, "[%s - %s pattern %i row %i]", the_song->real_name, chan_name[channel], pattern, row);
  }
  return buffer;
}

// finds a auto/dual drum automatically, or creates a new one if necessary
uint8_t find_auto_drum(uint8_t noise, uint8_t triangle) {
  for(int i=0; i<num_auto_drums; i++) {
    if(auto_drum_noise[i] == noise && auto_drum_tri[i] == triangle)
      return i;
  }
//  printf("Adding drum combo: noise %x tri %x\n", noise, triangle);
  if(num_auto_drums == MAX_DRUMS)
    error(1, "Maximum number of drums is %i", MAX_DRUMS);

  auto_drum_noise[num_auto_drums] = noise;
  auto_drum_tri[num_auto_drums] = triangle;

  return num_auto_drums++;
}

// writes the numbers for an instrument's envelope, including the loop point
void write_macro(FILE *file, ftmacro *macro) {
  int i;
  for(i=0; i<macro->length; i++) {
    if(i == macro->loop)
      fprintf(file, "| ");
    fprintf(file, "%i ", macro->sequence[i]);
  }
  fprintf(file, "\r\n");
}

// writes an octave using ' and ,
void write_octave(FILE *file, int octave) {
  int i;
  if(octave > 2)
    for(i=2; i!= octave; i++)
      fputc('\'', file);
  if(octave < 2)
    for(i=2; i!= octave; i--)
      fputc(',', file);
}

// flags for write_instrument
enum {
  ABSOLUTE_PITCH = 1,
  ALLOW_DECAY = 2,
};

// writes an instrument's envelopes
void write_instrument(FILE *file, int i, int flags) {
  unsigned int num_macro_volume = (unsigned)instrument[i][MS_VOLUME];
  unsigned int num_macro_duty   = (unsigned)instrument[i][MS_DUTY];
  unsigned int num_macro_arp    = (unsigned)instrument[i][MS_ARPEGGIO];

  // write the envelopes the instrument has
  if(instrument[i][MS_VOLUME] >= 0) {
    // read the decay information first to find out if the instrument has an automatic decay
    // (and make a copy of the macro that can be modified without changing the original)
    ftmacro macro = instrument_macro[MS_VOLUME][num_macro_volume];
    int decay_rate   = macro.decay_rate;
    int decay_volume = macro.decay_volume;
    int decay_index  = macro.decay_index;

    // do not use decay if it would interfere with the arpeggio or duty envelopes, or if disallowed
    if((decay_rate && decay_enabled && (flags & ALLOW_DECAY))
                  && (instrument[i][MS_ARPEGGIO] < 0 || ((instrument_macro[MS_ARPEGGIO][num_macro_arp].length < decay_index) && 
                                                        (instrument_macro[MS_ARPEGGIO][num_macro_arp].loop == -1)))
                  && (instrument[i][MS_DUTY] < 0 || ((instrument_macro[MS_DUTY][num_macro_duty].length < decay_index) &&
                                                    (instrument_macro[MS_DUTY][num_macro_duty].loop == -1)))) {
      // if a decay can be used, cut off the volume envelope at the decay point and write the decay command
      macro.sequence[decay_index] = decay_volume;
      macro.length = decay_index + 1;
      fprintf(file, "  decay %i\r\n", decay_rate);
    }
    fprintf(file, "  volume ");
    write_macro(file, &macro);
  }
  if(instrument[i][MS_DUTY] >= 0) {
    fprintf(file, "  timbre ");
    write_macro(file, &instrument_macro[MS_DUTY][num_macro_duty]);
  }
  if(instrument[i][MS_ARPEGGIO] >= 0) {
    ftmacro *macro = &instrument_macro[MS_ARPEGGIO][num_macro_arp];
    fprintf(file, "  pitch ");

    if(flags & ABSOLUTE_PITCH) { // Pently sfx pitch envelopes require music notes, not semitone numbers
      int j;
      for(j=0; j<macro->length; j++) {
        if(j == macro->loop)
          fprintf(file, "| ");
        // convert to note
        int semitones = macro->sequence[j];
        char note;
        uint8_t octave;
        semitone_to_note(semitones, &note, &octave);
        // print it
        fprintf(file, "%c%s", tolower(note), isupper(note)?"#":"");
        write_octave(file, octave);
        fputc(' ', file);
      }
      fprintf(file, "\r\n");
    } else {
      write_macro(file, macro);
    }
  }
}

// converts the number of rows to a Pently note duration
void write_duration(FILE *file, int duration, int slur) {
  const char *long_duration[] = {
    /* 1 */ "16",
    /* 2 */ "8",
    /* 3 */ "8 w16",
    /* 4 */ "4",
    /* 5 */ "4 w16",
    /* 6 */ "4 w8",
    /* 7 */ "4 w8 w16",
    /* 8 */ "2",
    /* 9 */ "2 w16",
    /*10 */ "2 w8",
    /*11 */ "2 w8 w16",
    /*12 */ "2 w4",
    /*13 */ "2 w4 w16",
    /*14 */ "2 w4 w8",
    /*15 */ "2 w4 w8 w16",
    /*16 */ "1"
  };
  const char *dotted_duration[] = {
    /* 1 */ "16",
    /* 2 */ "8",
    /* 3 */ "8.",
    /* 4 */ "4",
    /* 5 */ "4 w16",
    /* 6 */ "4.",
    /* 7 */ "4. w16",
    /* 8 */ "2",
    /* 9 */ "2 w16",
    /*10 */ "2 w8",
    /*11 */ "2 w8.",
    /*12 */ "2.",
    /*13 */ "2. w16",
    /*14 */ "2. w8",
    /*15 */ "2. w8.",
    /*16 */ "1"
  };
  const char **durations = dotted_durations ? dotted_duration : long_duration;

  duration--;
  fprintf(file, "%s%s ", durations[duration%16], slur?"~":"");
  while(duration > 16) {
    fprintf(file, "w1 ");
    duration -= 16;
  }
}

// write a time in the format "at" takes
void write_time(FILE *file, int rows) {
  int measure = rows / 16;
  int beat    = (rows % 16) / 4;
  int row     = (rows % 16) % 4;

  fprintf(file, "%i", measure+1);
  if(beat || row) {
    fprintf(file, ":%i:%i", beat+1, row);
  }
}

// writes a tempo
void write_tempo(FILE *file, int speed, int tempo) {
  float real_tempo = 6;
  real_tempo /= speed;
  real_tempo *= tempo;
  fprintf(file, "  tempo %.2f", real_tempo);
}

// writes a pattern to the output file
void write_pattern(FILE *file, int id, int channel) {
  // skip over noise channel if auto_noise and auto_dual_drums are both off
  // skip over DPCM channel if auto_noise or auto_dual_drums are on
  if((channel == CH_NOISE && !(auto_noise || auto_dual_drums)) ||
     (channel == CH_DPCM && (auto_noise || auto_dual_drums)))
    return;

  ftnote *pattern = xsong.pattern[id][channel];
  int i, slur = 0, delay_cut = 0;

  // find the instrument used for the pattern
  int instrument = -1;
  for(i=0; i<xsong.rows; i++)
    if(pattern[i].instrument >= 0) {
      instrument = pattern[i].instrument;
      break;
    }
  if(instrument == -1)
    error(1, "note with no instrument %s", error_location(&xsong, channel, id, -1));

  // generate pattern name and specify absolute octaves
  fprintf(file, "\r\n  pattern pat_%i_%i_%i", song_num, channel, id);
  if(channel_is_pitched(channel))
    fprintf(file, " with %s on %s\r\n    absolute", instrument_name[instrument], chan_name[channel]);
  fprintf(file, "\r\n    ");

  // for each row
  int row = 0;
  while(row < xsong.pattern_length[id][channel]) {
    char this_note = pattern[row].note;
    int next, octave = pattern[row].octave;

    // find the next note
    for(next = row+1; next < xsong.pattern_length[id][channel]; next++)
      if(pattern[next].note || pattern[next].volume)
        break;
    // the distance between this note and the next note is the duration
    int duration = next-row;

    // write any instrument changes
    if(isalnum(this_note) && pattern[row].instrument >= 0 && pattern[row].instrument != instrument) {
      instrument = pattern[row].instrument;
      if(channel_is_pitched(channel))
        fprintf(file, "@%s ", instrument_name[instrument]);
    }

    // write volume changes
    if(pattern[row].volume) {
      switch(pattern[row].volume) {
        case VOL_FF:
          fprintf(file, "ff ");
          break;
        case VOL_MF:
          fprintf(file, "mf ");
          break;
        case VOL_MP:
          fprintf(file, "mp ");
          break;
        case VOL_PP:
          fprintf(file, "pp ");
          break;
      }
    }

    // handle any effects
    for(i=0; i<MAX_EFFECTS; i++) {
      switch(pattern[row].effect[i]) {
        case FX_SLUR:
          slur = pattern[row].param[i] != 0;
          break;
        case FX_ARP:
          if(channel_is_pitched(channel))
            fprintf(file, "EN%.2x ", pattern[row].param[i]);
          break;
        case FX_VIBRATO:
          if(channel_is_pitched(channel)) {
            switch(pattern[row].param[i] & 15) {
              case 0:
                fprintf(file, "MP0 ");
                break;
              case 1: case 2:
                fprintf(file, "MP1 ");
                break;
              case 3: case 4:
                fprintf(file, "MP2 ");
                break;
              case 5: case 6:
                fprintf(file, "MP3 ");
                break;
              default:
                fprintf(file, "MP4 ");
                break;
            }
          }
          break;
        case FX_DELAYCUT:
          if(this_note) {
            delay_cut = pattern[row].param[i];
            break;
          }
          // if it's an empty row, turn it into a delay and insert a note cut right here instead of at the next note
          pattern[row].note = '-';
        case FX_DELAY:
          fprintf(file, "r%ig ", pattern[row].param[i]);
          break;
      }
    }

    // write note
    if(this_note == '-') { // note cut
        fprintf(file, "r");
    } else if(!this_note) { // no not
        fprintf(file, "w");
    } else if(channel_is_pitched(channel)) { // a note
      // just write normal notes
      fprintf(file, "%c%s", tolower(this_note), isupper(this_note)?"#":"");

      // shift the octave in the direction needed
      write_octave(file, octave);
    } else if(channel == CH_NOISE) { // noise
      if(auto_dual_drums) { // auto_dual_drums
        uint8_t noise = instrument;
        uint8_t triangle = 255; // default to no triangle part
        if(pattern[row].effect[0] == FX_ATTACK_ON) { // repurposed effect
          triangle = pattern[row].param[0];
        }
        uint8_t drum_no = find_auto_drum(noise, triangle);
        fprintf(file, "autodrum%i_", drum_no);
      } else { // auto_noise
        // for noise, use the instrument name and the note frequency

        // mark frequency as being used
        char hex[2] = {this_note, 0};
        instrument_noise[instrument] |= 1 << strtol(hex, NULL, 16);

        fprintf(file, "%s_%c_", instrument_name[instrument], this_note);
      }
    } else { // DPCM
      // for DPCM: write drum name
      char *scale_note = strchr(scale, this_note);
      fprintf(file, "%s", drum_name[octave][scale_note-scale]);
    }
    if(delay_cut && isalpha(this_note)) {
      fprintf(file, "%ig r", delay_cut);
      delay_cut = 0;
    }
    write_duration(file, duration, slur|pattern[row].slur);

    row = next;
  }

}

int main(int argc, char *argv[]) {
  int i, j;
  char buffer[700];
  memset(&instrument, 0, sizeof(instrument));
  memset(&instrument_used, 0, sizeof(instrument_used));
  memset(&instrument_ignore, 0, sizeof(instrument_ignore));
  memset(&instrument_macro, 0, sizeof(instrument_macro));
  memset(&instrument_name, 0, sizeof(instrument_name));
  memset(&instrument_noise, 0, sizeof(instrument_noise));
  memset(&drum_name, 0, sizeof(drum_name));
  memset(&auto_drum_noise, 255, sizeof(auto_drum_noise));
  memset(&auto_drum_tri,   255, sizeof(auto_drum_tri));
  memset(&song_name, 0, sizeof(song_name));
  memset(&soundeffects, 0, sizeof(soundeffects));

  // Generate decay tables
  for(i=0;i<MAX_DECAY_START;i++) {
    for(j=0;j<MAX_DECAY_RATE;j++) {
      int volume = (i+1)<<4;
      int value, index = 0, decay = j+1;
            
      while(volume >= 0x08) {
        volume -= decay;
        value = ((volume+8)>>4);
        decay_envelope[i][j][index++] = value;
      }
      if(value != 0)
        decay_envelope[i][j][index++] = 0;
    }
  }

  // read arguments
  for(i=1; i<argc; i++) {
    if(!strcmp(argv[i], "-i"))
      in_filename = argv[i+1];
    if(!strcmp(argv[i], "-o"))
      out_filename = argv[i+1];
    if(!strcmp(argv[i], "-strict"))
      strict = 1;
    if(!strcmp(argv[i], "-hexrow"))
      hex_rows = 1;
    if(!strcmp(argv[i], "-dotted"))
      dotted_durations = 1;
    if(!strcmp(argv[i], "-autonoise"))
      auto_noise = 1;
    if(!strcmp(argv[i], "-autodualdrums"))
      auto_dual_drums = 1;
    if(!strcmp(argv[i], "-autodecay"))
      decay_enabled = 1;
  }

  // complain if input or output not specified
  if(!in_filename || !out_filename) {
    puts("syntax: ft2p -i input -o output");
    exit(-1);
  }

  // start reading file
  FILE *input_file = fopen(in_filename, "rb");
  if(!input_file)
    error(1,"Input file couldn't be opened");
  FILE *output_file = fopen(out_filename, "wb");
  if(!output_file)
    error(1,"Output file couldn't be opened");
  fprintf(output_file, "durations stick\r\nnotenames english\r\n");

  // process each line
  int need_song_export = 0;
  while(1) {
    int end_of_file = fgets(buffer, sizeof(buffer), input_file) == NULL;
    if(end_of_file) // Run through the code with an empty line buffer
      *buffer = 0;

    char *arg;
    remove_line_endings(buffer);

    if(starts_with(buffer, "TRACK ", &arg)) {
      if(song_num) {
        // copy song to xsong, because song is going to be reset for the new track
        need_song_export = 1;
        xsong = song;
      }
      song_num++;
      memset(&song, 0, sizeof(song));
      song.rows = strtol(arg, &arg, 10);
      for(i=0; i<MAX_PATTERNS; i++)
        for(j=0; j<CHANNEL_COUNT; j++)
          song.pattern_length[i][j] = song.rows;
      song.speed = strtol(arg, &arg, 10);
      song.tempo = strtol(arg, &arg, 10);
      arg = strchr(arg, '\"');

      strlcpy(song.real_name, arg+1, sizeof(song.real_name));
      sanitize_name(song.name, arg+1, sizeof(song.name));
      strlcpy(song_name[song_num-1], song.name, SONG_NAME_LEN);

      // check for and fix duplicate song names
      for(i=0;i<song_num-1;i++) {
        if(!strcmp(song.name, song_name[i])) {
          sprintf(buffer, "%s__%i", song.name, duplicate_name_counter++);
          error(0, "Duplicate song name (%s), renaming to \"%s\"", song.name, buffer);
          strlcpy(song.name, buffer, sizeof(song.name));
          break;
        }
      }
    }

    else if(starts_with(buffer, "PATTERN ", &arg)) {
      song.pattern_id = strtol(arg, NULL, 16);
      check_range("pattern id", song.pattern_id, 0, MAX_PATTERNS, song.real_name);
    }

    else if(starts_with(buffer, "ROW ", &arg)) {
      int row = strtol(arg, &arg, 16);
      check_range("row id", row, 0, MAX_ROWS, error_location(&song, 0, song.pattern_id, -1));

      for(int channel=0; channel<CHANNEL_COUNT; channel++) {
         // find next channel
         arg = strchr(arg, ':');
         if(!arg)
           break;
         char *line = arg;
         arg++;

         // skip if the note is already filled in
         if(song.pattern[song.pattern_id][channel][row].note) {
           puts("skipping");
           continue;
         }

         // read note info
         ftnote note;
         memset(&note, 0, sizeof(note));
 
         note.instrument = -1;

         // volume column
         if(line[9] != '.') {
           int volume = VOL_SAME;
           int digit = strtol(line+9, NULL, 16);
           if(digit <= 6)
             volume = VOL_PP;
           else if(digit <= 9)
             volume = VOL_MP;
           else if(digit <= 12)
             volume = VOL_MF;
           else
             volume = VOL_FF;
           int last_volume = VOL_SAME;
           for(j=row-1; j>=0; j--)
             if(song.pattern[song.pattern_id][channel][j].volume) {
               last_volume = song.pattern[song.pattern_id][channel][j].volume;
               break;
             }
           if(volume != last_volume)    
             note.volume = volume;
         }

         if(line[2] == '=') { // note releases are not supported, so degrade to note cut or just nothing
           if(channel_is_pitched(channel))
             note.note = '-';
           else
             note.note = 0;
         } else if(line[2] != '.') { // will catch note cuts too
           // sharp note are uppercase
           note.note = (line[3]=='#')?toupper(line[2]):tolower(line[2]);
           // octave will be garbage for note cuts and noise notes, but that's OK
           note.octave = line[4]-'0';

           // read instrument if it's there
           if(isalnum(note.note) && line[6] != '.') {
             int read_instrument = strtol(line+6, NULL, 16);
             if(read_instrument < 0 || read_instrument >= MAX_INSTRUMENTS) {
               error(0, "instrument (%i) out of range - %s", read_instrument, error_location(&song, channel, song.pattern_id, row));
               // skip this note altogether
               continue;
             }
             // mark used if the note's not ignored (I should just probably actually bail out of parsing the note if it's ignored)
             if(channel_is_pitched(channel) && !(read_instrument != -1 && instrument_ignore[read_instrument] & (1 << channel)))
               instrument_used[read_instrument] = 1;
             note.instrument = read_instrument;
           } else { // if it's not, go back and find it
             for(j=row-1; j>=0; j--)
               if(song.pattern[song.pattern_id][channel][j].note && (song.pattern[song.pattern_id][channel][j].instrument != -1)) {
                 note.instrument = song.pattern[song.pattern_id][channel][j].instrument;
                 break;
               }
           }
         }

         // read effects
         for(j=0; j<song.effect_columns[channel]; j++) {
           // read in the effect type and value
           char *effect = line+11+4*j;
           if(!strchr(supported_effects, *effect))
             error(0, "unsupported effect (%c) %s", *effect, error_location(&song, channel, song.pattern_id, row));
           note.effect[j] = *effect;
           note.param[j]  = strtol(effect+1, NULL, 16);

           // some effects call for processing during pattern reading
           ftnote *next_note = &song.pattern[song.pattern_id][channel][row+1];
           switch(*effect) {
             case FX_DELAYCUT:
               if((tri_sxx_to_cut && channel == CH_TRIANGLE) || !note.param[j]) {
                 // S00 is identical to a note cut
                 // also cut if using tri_sxx_to_cut
                 note.note = '-';
                 note.effect[j] = '.'; // turn effect off
               }
               break;
             case FX_SLUR:
               if(note.param[j]) // set slur on previous note
                 for(int k=row-1; k >= 0; k--) // find previous note
                   if(song.pattern[song.pattern_id][channel][k].note) {
                     song.pattern[song.pattern_id][channel][k].slur = 1;
                     break;
                   }
               break;
             // mark the note as a slur and make the note to slur into
             case FX_SLUR_UP:
               note.slur = 1;
               *next_note = make_note(note.octave, note.note, note.instrument);
               shift_semitones(next_note, note.param[j]&15);
               break;
             case FX_SLUR_DN:
               note.slur = 1;
               *next_note = make_note(note.octave, note.note, note.instrument);
               shift_semitones(next_note, -(note.param[j]&15));
               break;
             // loops, pattern cuts and fines all reduce the length of the pattern
             case FX_LOOP:
               song.loop_to = note.param[j];
               goto pattern_cut;
             case FX_FINE:
               song.loop_to = -1;
             case FX_PAT_CUT:
             pattern_cut:
               song.pattern_length[song.pattern_id][channel] = row+1;
           }
         }

         // write the note only if the instrument is not ignored
         if(!(note.instrument != -1 && instrument_ignore[note.instrument] & (1 << channel))) {
           // finally write the note we made into the pattern
           song.pattern[song.pattern_id][channel][row] = note;
         }
      }

    }

    else if(starts_with(buffer, "TITLE ", &arg)) {
      char *temp = strchr(arg, '\"');
      if(temp) {
        arg = temp+1;
      }
      fprintf(output_file, "\r\ntitle %s", arg);
	}
    else if(starts_with(buffer, "AUTHOR ", &arg)) {
      char *temp = strchr(arg, '\"');
      if(temp) {
        arg = temp+1;
      }
      fprintf(output_file, "\r\nauthor %s", arg);
	}
    else if(starts_with(buffer, "COPYRIGHT ", &arg)) {
      char *temp = strchr(arg, '\"');
      if(temp) {
        arg = temp+1;
      }
      fprintf(output_file, "\r\ncopyright %s\r\n", arg);
	}

    // comments are used for song metadata
    else if(starts_with(buffer, "COMMENT ", &arg)) {
      remove_line_ending(buffer, '\r');
      if(*arg == '\"')
        arg++;
      char *arg2;
      if(starts_with(arg, "ignore ", &arg2)) { // ignore instruments on specific channels
        int instrument_id = 0;
        int channel_id = 0;

        // separate the channel name and instrument ID
        char *space = strchr(arg2, ' ');
        if(!space)
          error(1, "'ignore' takes two parameters");
        *space = 0;
        space = skip_to_number(space+1);
        if(!isxdigit(*space))
          error(1, "'ignore' needs an instrument number in hex");
        instrument_id = strtol(space, NULL, 16);

        while(strcmp(chan_name[channel_id], arg2) && channel_id != CHANNEL_COUNT)
          channel_id++;
        if(channel_id == CHANNEL_COUNT)
          error(1, "'ignore' needs a channel name; use pulse1, pulse2, triangle, noise, drum, or attack");

        printf("ignoring %x on %s\n", instrument_id, chan_name[channel_id]);
        instrument_ignore[instrument_id] |= 1 << channel_id;
      }
      if(starts_with(arg, "include ", &arg2)) {
        // import another file into this file
        FILE *included = fopen(arg2, "rb");
        if(!included)
          error(1,"couldn't open included file \"%s\"", arg2);
        while(!feof(included)) {
          char c = fgetc(included);
          if(c != EOF)
            fputc(c, output_file);
        }
        fclose(included);
      } else if(!strcmp(arg, "auto noise")) {
        auto_noise = 1;
      } else if(!strcmp(arg, "auto dual drums")) {
        auto_dual_drums = 1;
      } else if(!strcmp(arg, "tri sxx to cut")) {
        tri_sxx_to_cut = 1;
      } else if(!strcmp(arg, "auto decay")) {
        decay_enabled = 1;
      } else if(starts_with(arg, "sfx ", &arg2)) {
        // define a sound effect using an instrument
        soundeffects[sfx_num].instrument = strtol(arg2, &arg2, 16);
        // skip to channel
        while(*arg2 == ' ')
          arg2++;
        // select the channel
        char channel = *(arg2++);
        if(channel == 's')
          channel = CH_SQUARE1;
        else if(channel == 'n')
          channel = CH_NOISE;
        else if(channel == 't')
          channel = CH_TRIANGLE;
        soundeffects[sfx_num].channel = channel;

        // skip to name
        while(*arg2 == ' ')
          arg2++;
        strlcpy(soundeffects[sfx_num].name, arg2, 64);
        sfx_num++;
      } else if(starts_with(arg, "drumsfx ", &arg2)) {
        // define a drum using sound effects
        fprintf(output_file, "drum %s\r\n", arg2);
      } else if(starts_with(arg, "drum ", &arg2)) {
        // drum = assign a drum to a DPCM note
        char *note = strchr(scale, tolower(arg2[0]));
        if(!note)
          error(1,"invalid note in drum definition (%c)");
        char *octave_ptr = arg2+1;
        if(*octave_ptr == '#') 
          note++;
        if(!isdigit(*octave_ptr))
          octave_ptr++;
        int octave = *octave_ptr-'0';
        check_range("drum octave", octave, 0, NUM_OCTAVES, NULL);
        strlcpy(drum_name[octave][note-scale], octave_ptr+2, 16);
      }
    }

    else if(starts_with(buffer, "COLUMNS ", &arg)) {
      arg = skip_to_number(arg);
      for(i=0;*arg && (i < CHANNEL_COUNT);i++)
        song.effect_columns[i] = strtol(arg, &arg, 10);
    }

    else if(starts_with(buffer, "MACRO ", &arg)) {
      int setting = strtol(arg, &arg, 10);
      check_range("macro setting type", setting, 0, MACRO_SET_COUNT, NULL);
      int id = strtol(arg, &arg, 10);
      check_range("macro id", id, 0, MAX_INSTRUMENTS, NULL);
      instrument_macro[setting][id].loop = strtol(arg, &arg, 10);
      instrument_macro[setting][id].release = strtol(arg, &arg, 10);
      instrument_macro[setting][id].length = 0;
      instrument_macro[setting][id].arp_type = strtol(arg, &arg, 10);
      arg = skip_to_number(arg);

      // read all the numbers and count them
      while(*arg) {
        instrument_macro[setting][id].sequence[instrument_macro[setting][id].length++] = strtol(arg, &arg, 10);
        if(instrument_macro[setting][id].length >= MAX_MACRO_LEN)
          error(1,"instrument \"%s\" has a %s envelope that's too long (max length is %i)", instrument_name[id], envelope_types[setting], MAX_MACRO_LEN);
      }

      // if auto decay is enabled and this is a volume envelope, try to find a decay envelope
      if(decay_enabled && setting == MS_VOLUME && instrument_macro[setting][id].loop == -1 &&
        !instrument_macro[setting][id].sequence[instrument_macro[setting][id].length-1]) {

        int stop = 0;
        int length_envelope = instrument_macro[setting][id].length-1;     // length in bytes, including the zero so -1
        for(i=MAX_DECAY_START-1;i>=2 && !stop; i--)                       // try starting volumes in reverse order
          for(j=0; j<MAX_DECAY_RATE && !stop; j++) {
            int length_decay = strlen(decay_envelope[i][j]);              // length in bytes, not including zero

            int start_offset = length_envelope - length_decay;            // end of the envelope, backed up to where the decay would start
            if(!memcmp(instrument_macro[setting][id].sequence + start_offset, decay_envelope[i][j], length_decay)) {
              instrument_macro[setting][id].decay_index = start_offset;
              instrument_macro[setting][id].decay_volume = i+1;
              instrument_macro[setting][id].decay_rate = j+1;
              stop = 1;                                                   // break out of the loop
            }
          }
      }
      if(end_of_file)
        break;
    }

    else if(starts_with(buffer, "INST2A03 ", &arg)) {
      int id = strtol(arg, &arg, 10);
      check_range("instrument id", song.pattern_id, 0, MAX_INSTRUMENTS, NULL);
      for(i=0; i<MACRO_SET_COUNT; i++) {
        instrument[id][i] = strtol(arg, &arg, 10);
        check_range("macro sequence id", instrument[id][i], -1, MAX_INSTRUMENTS, NULL);
      }
      arg = strchr(arg, '\"');
      sanitize_name(instrument_name[id], arg+1, sizeof(instrument_name[id]));

      // check for duplicate names
      for(i=0; i<id; i++) {
         if(!strcmp(instrument_name[i], instrument_name[id])) {
           char temp[20];
           duplicate_name_counter++;
           sprintf(temp, "__%i", duplicate_name_counter);
           strcat(instrument_name[id], temp);
           error(0, "Duplicate instrument name (%s), renaming to \"%s\"", instrument_name[i], instrument_name[id]);
           break;
         }
      }
    }

    else if(starts_with(buffer, "ORDER ", &arg)) {
      int id = strtol(arg, &arg, 16);
      song.frames = id+1; // assume last frame in file is last frame in song
      check_range("frame number", id, 0, MAX_FRAMES, song.real_name);
      arg = skip_to_number(arg);
      for(i=0; i<CHANNEL_COUNT; i++)
        song.frame[id][i] = strtol(arg, &arg, 16);
    }

    // export things if needed
    if(end_of_file) {
      // write automatic noise+triangle drums if needed
      if(auto_dual_drums) {
        for(int j=0; j<MAX_INSTRUMENTS; j++) {
          // create noise sound effects
          // for instruments that appear in auto_drum_noise
          for(int i=0; i<num_auto_drums; i++) {
            if(auto_drum_noise[i] != j)
              continue;
            soundeffects[sfx_num].instrument = j;
            soundeffects[sfx_num].channel = CH_NOISE;
            sprintf(soundeffects[sfx_num].name, "autonoise%x_", j);
            sfx_num++;
            break;
          }

          // create triangle sound effects
          // for instruments that appear in auto_drum_tri
          for(int i=0; i<num_auto_drums; i++) {
            if(auto_drum_tri[i] != j)
              continue;
            soundeffects[sfx_num].instrument = j;
            soundeffects[sfx_num].channel = CH_TRIANGLE;
            sprintf(soundeffects[sfx_num].name, "autotriangle%x_", j);
            sfx_num++;
            break;
          }

        }

        // create drums using both these sound effects
        for(int i=0; i<num_auto_drums; i++) {
          printf("%i noise %x, triangle %x\n", i, auto_drum_noise[i], auto_drum_tri[i]);
          if(auto_drum_tri[i] == 255)
            fprintf(output_file, "\r\ndrum autodrum%i_ autonoise%x_", i, auto_drum_noise[i]);
          else
            fprintf(output_file, "\r\ndrum autodrum%i_ autonoise%x_ autotriangle%x_", i, auto_drum_noise[i], auto_drum_tri[i]);
        }
      }

      xsong = song;
      // write sound effects
      for(i=0; i<sfx_num; i++) {
        int instrument = soundeffects[i].instrument, channel = soundeffects[i].channel;
        // sound effects don't like being put on "pulse1" so replace it with "pulse"
        const char *channel_name = chan_name[channel];
        if(channel == CH_SQUARE1)
          channel_name = "pulse";
        fprintf(output_file, "\r\nsfx %s on %s\r\n", soundeffects[i].name, channel_name);

        // use absolute pitch for non-noise; decay disallowed
        write_instrument(output_file, instrument, (channel != CH_NOISE)?ABSOLUTE_PITCH:0);
      }
      // write instruments
      for(i=0; i<MAX_INSTRUMENTS; i++)
        if(instrument_used[i]) {
          fprintf(output_file, "\r\ninstrument %s\r\n", instrument_name[i]);
          write_instrument(output_file, i, ALLOW_DECAY);
        }
      need_song_export = 1;
    }
    if(need_song_export) {
      fprintf(output_file, "\r\nsong %s\r\n  time 4/4\r\n  scale 16\r\n  title %s\r\n", xsong.name, xsong.real_name);
      write_tempo(output_file, xsong.speed, xsong.tempo);
      fprintf(output_file, "\r\n");

      // write the actually used (not empty) patterns
      for(j=0; j<CHANNEL_COUNT; j++)
        for(i=0; i<MAX_PATTERNS; i++) {
          int not_empty = 0;
          for(int row = 0; row < xsong.rows; row++)
            if(isalnum(xsong.pattern[i][j][row].note)) {
              not_empty = 1;
              break;
            }
          xsong.pattern_used[i][j] = not_empty;

          if(not_empty)
            write_pattern(output_file, i, j);
        }

      // write the frames
      int channel_playing[CHANNEL_COUNT] = {1, 1, 1, auto_noise||auto_dual_drums, !(auto_noise||auto_dual_drums), 0};
      int total_rows = 0;
      for(i=0; i<xsong.frames; i++) {
        fprintf(output_file, "\r\n  at ");
        write_time(output_file, total_rows);
        if(xsong.loop_to == i && xsong.loop_to)
          fprintf(output_file, "\r\n  segno");

        int min_length = MAX_ROWS; // minimum pattern length in this frame
        for(j=0; j<CHANNEL_COUNT; j++) {
          int pattern = xsong.frame[i][j];
          if(( (!(auto_noise||auto_dual_drums) && j != CH_NOISE)
             || ((auto_noise||auto_dual_drums) && j != CH_DPCM))
            && xsong.pattern_used[pattern][j]) {
            fprintf(output_file, "\r\n  play pat_%i_%i_%i", song_num, j, pattern);
            channel_playing[j] = 1;
          } else if(channel_playing[j]) { // stop channel if it was playing but now it isn't
            if(j == CH_NOISE || j == CH_DPCM)
              fprintf(output_file, "\r\n  stop drum");
            else
              fprintf(output_file, "\r\n  stop %s", chan_name[j]);
            channel_playing[j] = 0;
          }
          if(xsong.pattern_length[pattern][j] < min_length)
            min_length = xsong.pattern_length[pattern][j];
        }

        // look for tempo changes
        for(int row=0; row<min_length; row++) {
          int speed = 0, tempo = 0, attack=-1;
          for(int j=0; j<CHANNEL_COUNT; j++) {
            int pattern = xsong.frame[i][j];
            ftnote *note = &xsong.pattern[pattern][j][row];
            for(int fx=0; fx<MAX_EFFECTS; fx++)
              if(note->effect[fx] == FX_TEMPO) {
                if(note->param[fx] < 0x20)
                  speed = note->param[fx];
                else
                  tempo = note->param[fx];
              } else if(note->effect[fx] == FX_ATTACK_ON && j == CH_ATTACK)
                attack = note->param[fx];
          }
          if(speed||tempo||(attack>=0)) {
            if(row) {
              fprintf(output_file, "\r\n  at ");
              write_time(output_file, total_rows+row);
            }
            if(speed||tempo) {
              fprintf(output_file, "\r\n");
              write_tempo(output_file, speed?speed:xsong.speed, tempo?tempo:xsong.tempo);
            }
            if(attack>=0) {
              fprintf(output_file, "\r\n  attack on %s", chan_name[attack]);
            }
          }
        }
        total_rows += min_length;
      }
      fprintf(output_file, "\r\n  at ");
      write_time(output_file, total_rows);
      fprintf(output_file, "\r\n  ");
      if(xsong.loop_to != -1)
        fprintf(output_file, "dal segno");
      else
        fprintf(output_file, "fine");

      need_song_export = 0;
    }

    if(end_of_file)
      break;
  }

  // write automatic noise instruments if needed
  if(auto_noise)
    for(i=0; i<MAX_INSTRUMENTS; i++)
      if(instrument_noise[i])
        for(j=0; j<16; j++)
          if(instrument_noise[i] & (1 << j)) {
            // make a new sound effect for the noise frequency
            fprintf(output_file, "\r\nsfx noise_%s_%x on noise\r\n", instrument_name[i], j);

            // get the arpeggio envelope, change it and restore it to what it was
            unsigned int num_macro_arp    = (unsigned)instrument[i][MS_ARPEGGIO];
            unsigned int num_macro_duty   = (unsigned)instrument[i][MS_DUTY];
            if(num_macro_arp >= MAX_INSTRUMENTS) {
              // no arpeggio set, so make one
              ftmacro new_macro = {1, -1, -1, 0, {0}, 0, 0, 0};
              instrument_macro[MS_ARPEGGIO][MAX_INSTRUMENTS-1] = new_macro;
              instrument[i][MS_ARPEGGIO] = MAX_INSTRUMENTS-1;
              num_macro_arp = MAX_INSTRUMENTS - 1;
            }
            int k;
            if(num_macro_duty < MAX_INSTRUMENTS) {
              // if duty is used, wrap all duty values to 0 and 1
              // we don't need to worry about saving and restoring because the macros
              // won't be needed after the automatic noise drums are written
              ftmacro *duty_macro = &instrument_macro[MS_DUTY][num_macro_duty];
              for(k=0; k<duty_macro->length; k++)
                duty_macro->sequence[k] &= 1;
            }

            ftmacro *arp_macro = &instrument_macro[MS_ARPEGGIO][num_macro_arp];
            ftmacro old = *arp_macro;
            for(k=0; k<arp_macro->length; k++)
              arp_macro->sequence[k] = (arp_macro->sequence[k]+j)&15;
            write_instrument(output_file, i, 0); // disallow decay
            *arp_macro = old;

            // define a drum for the frequency
            fprintf(output_file, "\r\ndrum %s_%x_ noise_%s_%x", instrument_name[i], j, instrument_name[i], j);
          }

  // close files
  fclose(input_file);
  fprintf(output_file, "\r\n\r\n");
  fclose(output_file);

  return 0;
}
