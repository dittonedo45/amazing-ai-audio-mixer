#!python
import aaai as ai
import sys
import os
import random
import re
from signal import SIGINT,signal

tracks = []
total_duration = 0
per_file_duration = 0;

signal (SIGINT, lambda: sys.exit (0))

class NextTrack(Exception):
    pass

def duration_displayer(duration, div, time_base_q):
    global total_duration
    global per_file_duration
    total_duration = total_duration + duration;
    per_file_duration = per_file_duration + duration
    if per_file_duration<56.60:
        raise NextTrack("Am tired")
    sys.stderr.write("{}\n".format (total_duration/time_base_q));

try:
    taste = re.compile ('Migos', flags=re.IGNORECASE);
except re.error:
    sys.exit (1);

for i in os.listdir('.'):
    if (not i.endswith ('.mp4')) and (not i.endswith ('.mp3')):
        continue
    if (taste.match (i) is not None):
        tracks.append (i)


while True:
    try:
        inp = open (tracks[random.randint(0, len(tracks))], "rb")
        try:
            inp_handler = ai.open_read (inp)
            ai.process( inp_handler, ai.open_write (sys.stdout, "sox"), duration_displayer)
            per_file_duration = 0;
        except Exception:
            None
        inp.close ();
    except (IndexError,FileNotFoundError):
        print("Am tried, Let me die, in peace");
        sys.exit (0)
