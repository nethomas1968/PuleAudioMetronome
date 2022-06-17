#
# Makefile
#

all: Metronome

%.o: %.c
	gcc -O2 -c $<
	
Metronome: Metronome.o Mutex.o PCMPlayer.o Socket.o 
	gcc -O2 Metronome.o Mutex.o PCMPlayer.o Socket.o -o Metronome `pkg-config --cflags --libs libpulse` -lpthread
	
	
.PHONY: clean
clean:
	rm -f *.o Metronome
