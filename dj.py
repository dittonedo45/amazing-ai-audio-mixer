import aaai as ai
import sys
import os

for i in os.listdir('.'):
    if (not i.endswith ('.mp4')) and (not i.endswith ('.mp3')):
        continue
    while True:
        inp = open (i, "rb")
        try:
            inp_handler = ai.open_read (inp)
            ai.process( inp_handler, ai.open_write (sys.stdout, "sox"))
        except Exception:
            None
        inp.close ();
        break;
