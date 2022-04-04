import aaai as ai
import sys

inp = open ("./kk.mp3", "rb")

ai.process(ai.open_read (inp),
        ai.open_write (sys.stdout))
