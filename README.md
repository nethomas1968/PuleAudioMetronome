# PuleAudioMetronome
Simple clicks generated through the Pulse Audio Library. For Linux.


PulseAudioMetronome - Demonstrate X threads running at the same time. Threads are launched by the main thread.
Each thread will play a pcm audio file in turn.

See NUM_WORKER_THREADS for the number of threads.

You can control pause/play controls through CURL commands. There is a socket thread started on port 5100. See Socket.c file.
You can Pause/Play/Quit, like this:

curl http://127.0.0.1:5100/api/play
curl http://127.0.0.1:5100/api/pause
curl http://127.0.0.1:5100/api/quit
curl -d "repeat=on" http://127.0.0.1:5100
curl -d "repeat=off" http://127.0.0.1:5100
curl -d "speed=120" http://127.0.0.1:5100
curl -d "beats=6" http://127.0.0.1:5100

Simply type 'make' to build it.
