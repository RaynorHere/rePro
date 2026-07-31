# Python Projects

### copyComb: 
Program designed to accept a directory and compare every file in it with every other (subdirectories included). Intended purpose is to weed out duplicates by "combing" through directories, esp picture backup directories. Generally intended to be run on the root of whatever folder could be harboring the same file under different names, etc. \
Effectively legacy, investigation into multithreading options (see '__cCheck__') led to the decision to port to C, which represented the 'completion' of the experiment copyComb started.

****

### cCheck:
The fork off from copyComb to start investigating optimization strategies and multithreading. Has its own README explaining experimentation and discovery. \
Also legacy, as its development was superceded by the C port.

****

### reaveScript AKA "netReaver": 
My attempt to self-teach how to design an internet scraper. Initial run yielded ~20,000 files, with a coverage rating of .9767081. That is, the scraper missed a total of 2.5% of the images hosted on the root site. Satisfactory first run \
Legacy, but primarily left up for a very likely more advanced reaver I will write, so I can compare my development in development.

****