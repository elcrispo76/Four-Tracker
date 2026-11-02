LAME 3.100 - the MP3 encoder used by Four Tracker's REC (save a jam as MP3).

Source: LAME 3.100 (https://lame.sourceforge.io), from the git mirror https://github.com/despoa/LAME, tag RELEASE__3_100.
Only the encoder library (libmp3lame) and include/lame.h are here, unchanged. config.h is Four Tracker's own
small build configuration; mpglib_interface.c (the decoder interface) is left out.

Licence: GNU LGPL 2 (see COPYING and LICENSE in this folder). Four Tracker is AGPLv3 and its complete source
code, including this library, is published, so you can change LAME and rebuild Four Tracker with it.
LAME website: https://lame.sourceforge.io
