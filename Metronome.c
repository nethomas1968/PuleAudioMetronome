/*
 * PulseAudioMetronome - Demonstrate X threads running at the same time. Threads are launched by the main thread.
 * Each thread will play a pcm audio file in turn.
 * 
 * See NUM_WORKER_THREADS for the number of threads.
 * 
 * You can control pause/play controls through CURL commands. There is a socket thread started on port 5100. See Socket.c file.
 * You can Pause/Play/Quit, like this:
 * 
 * curl http://127.0.0.1:5100/api/play
 * curl http://127.0.0.1:5100/api/pause
 * curl http://127.0.0.1:5100/api/quit
 * curl -d "repeat=on" http://127.0.0.1:5100
 * curl -d "repeat=off" http://127.0.0.1:5100
 * curl -d "speed=120" http://127.0.0.1:5100
 * curl -d "beats=6" http://127.0.0.1:5100
 */

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>

#ifndef TRUE
#define FALSE 0
#define TRUE (!FALSE)
#endif

#include "Mutex.h"
#include "PCMPlayer.h"
#include "Socket.h"

#include "Metronome.h"

#define NUM_WORKER_THREADS 6

#define THREAD_STATE_OK 0
#define THREAD_STATE_TO_QUIT 1
#define THREAD_STATE_DONE 2

int GiPlayIdx = 1;  // First beat of the bar.

// Note, first number is the length of data.
int GiGlobalPlayCounter = 0;
int GiBeatsPerBar = 4;

typedef enum
{
    PLAY_STATE_PAUSED,
    PLAY_STATE_PLAYING,
}   play_state_t;


/* Globals */
int GiDebug = 0;
pthread_key_t main_thread_key;
pthread_key_t worker_thread_key[NUM_WORKER_THREADS];
l_mutex_t p_mutex[NUM_WORKER_THREADS];
pthread_cond_t cond[NUM_WORKER_THREADS];
int GiWorkerThreadSequenceIndex[NUM_WORKER_THREADS];
play_state_t Gplay_state = PLAY_STATE_PAUSED;
int GiSpeedBPM = 60;
bool GbRepeat = TRUE;

int ThreadIdx = 0;


void showThreadInfo(pthread_key_t theKey)
{
  void *value = pthread_getspecific(theKey);
  if (value)
    if (GiDebug == 1) printf("showThreadInfo(): ThrID:%u Value is '%s'\n", (unsigned int) pthread_self(), (char *) value);
  else
    if (GiDebug == 1) printf("showThreadInfo(): Value is null\n");
}

#define BPMToUS(iBPM) (60*1000*1000/iBPM)

/*
int BPMToUS(int iBPM) {
    return (60*1000*1000/iBPM);
}
*/

/*********************************************************************************************
 * 
 *********************************************************************************************/

void *Thread2(void *x_void_ptr)
{
    int iLocalCounter = 0;
    int *px = x_void_ptr;
    char *buf;
    int threadIndex = *px;
    int iInstrumentIndex;
    
    pthread_key_create(&worker_thread_key[*px], NULL);
    
    buf = (char *)malloc(64);
    if (buf == NULL) {
        fprintf(stderr, "Failed to allocate memory at line:%d\n", __LINE__);
        pthread_key_delete(worker_thread_key[*px]);
        return NULL;
    }

    sprintf(buf, "Thread:%d", *px);
    pthread_setspecific(worker_thread_key[*px], buf);

    showThreadInfo(worker_thread_key[*px]);
    
    if (GiDebug == 1) printf("Thread[%d]: START, passed index value = %d\n", threadIndex, *px);
    
    // Reset the int value passed to zero, we use that as a flag lower down.
    *px = THREAD_STATE_OK;
    
    while (1) {
        if (GiDebug == 1) printf("Thread[%d]: Waiting for lock\n", threadIndex);
        mutex_lock(&p_mutex[threadIndex]);
        
        if (GiDebug == 1) printf("Thread[%d]: unlock and waiting for signal...\n", threadIndex);
        pthread_cond_wait(&cond[threadIndex], &p_mutex[threadIndex].p_mutex);
        if (GiDebug == 1) printf("Thread[%d]: Got lock and signal, do work.\n", threadIndex);
        
        if (*px == THREAD_STATE_TO_QUIT) break;
        
        iInstrumentIndex = GiWorkerThreadSequenceIndex[threadIndex];
        
        //printf("Thread[%d]: Proceeding. iLocalCounter=%d, Playing Instrument %d, PlayCounter=%d\n", threadIndex, iLocalCounter, iInstrumentIndex, GiGlobalPlayCounter);
        
        if (Gplay_state == PLAY_STATE_PLAYING) {
        
            //printf("Thread[%d]: Playing iInstrumentIndex=%d\n", threadIndex, iInstrumentIndex);

            PCMPlayFile(iInstrumentIndex);
        
            ++GiGlobalPlayCounter;
        }
        
        if (GiDebug == 1) printf("Thread[%d]: Done work, unlock\n", threadIndex);
        mutex_unlock(&p_mutex[threadIndex]);
        
        ++iLocalCounter;
    }

    if (GiDebug == 1) printf("Thread[%d]: Finished\n", threadIndex);
    
    /* Show to main thread that we finished.  */
    *px = THREAD_STATE_DONE;
    
    pthread_key_delete(worker_thread_key[threadIndex]);
    free(buf);

    return NULL;
}

void Usage(char *name)
{
    printf("\n Usage: %s [-d] \n", name);
}


/*********************************************************************************************
 * 
 *********************************************************************************************/
int main(int argc, char **argv)
{
    char *buf;
    int x[NUM_WORKER_THREADS];
    pthread_t t2[NUM_WORKER_THREADS];
    int idx;
    pthread_condattr_t    attr;
    int fin;
    int option;
    int iSocketQuit = 0;
    bool bStartPlaying = FALSE;
    
    while ((option = getopt(argc, argv,"dp")) != -1)
    {
        switch (option)
        {
            case 'd' :  GiDebug = 1;
                break;
            case 'p' :  bStartPlaying = TRUE;
                break;
            default: Usage(argv[0]);
                return -1;
        }
    }
    
    pthread_condattr_init(&attr);

    for (idx = 0; idx < NUM_WORKER_THREADS; idx++) {
        mutex_init(&p_mutex[idx]);
        //cond[idx] = PTHREAD_COND_INITIALIZER;
        pthread_cond_init(&cond[idx], &attr);
        x[idx] = idx;
        GiWorkerThreadSequenceIndex[idx] = -1; // No instrument to play initially.
    }

    pthread_key_create(&main_thread_key, NULL);

    buf = (char *)malloc(64);
    if (buf == NULL) {
        fprintf(stderr, "Failed to allocate memory at line:%d\n", __LINE__);
        pthread_key_delete(main_thread_key);
        return -1;
    }

    strcpy(buf, "Main Thread");
    pthread_setspecific(main_thread_key, buf);

    showThreadInfo(main_thread_key);

    for (idx = 0; idx < NUM_WORKER_THREADS; idx++) {
        if (GiDebug == 1) printf("Starting Thread[%d]\n", idx);
        if (pthread_create(&t2[idx], NULL, Thread2, &x[idx]))
        {
            fprintf(stderr, "Error Creating Thread\n");
            return -1;
        }
        if (GiDebug == 1) printf("Thread[%d] is started\n", idx);
    }
    
    
    /* Start a Socket listener Thread */
    SocketThreadStart(GiDebug);

    ThreadIdx=0;

    if (bStartPlaying == TRUE) {
        MultiThreadSequencer_Play();
    }


#if 0
    // Initally play a period of silence.
    mutex_lock(&p_mutex[ThreadIdx]); // Now Thread2 get a chance to work.
    GiWorkerThreadSequenceIndex[ThreadIdx] = 2;
    pthread_cond_signal(&cond[ThreadIdx]);
    mutex_unlock(&p_mutex[ThreadIdx]);
    ++ThreadIdx;
    if (ThreadIdx >= NUM_WORKER_THREADS) ThreadIdx = 0;
#endif
    //usleep(100000);
    
    while (1) {
        
        /* Check if all threads have finished */
        fin=1;
        for (idx = 0; idx < NUM_WORKER_THREADS; idx++) {
            if (x[idx] != THREAD_STATE_DONE) fin = 0;
        }
        if (fin == 1) break;
        
        if (GiDebug == 1) printf("Main: Waiting for lock\n");
        mutex_lock(&p_mutex[ThreadIdx]); // Now Thread2 get a chance to work.
        if (GiDebug == 1) printf("Main: Got lock Thread[%d] is free to do work, signalling...\n", ThreadIdx);
        
        if (GiPlayIdx == 1) {
          GiWorkerThreadSequenceIndex[ThreadIdx] = 1;
        } 
        else {
          GiWorkerThreadSequenceIndex[ThreadIdx] = 0;
        }
        
        pthread_cond_signal(&cond[ThreadIdx]);
        mutex_unlock(&p_mutex[ThreadIdx]);
        
        if (Gplay_state == PLAY_STATE_PLAYING) {
            ++GiPlayIdx;
            if (GiPlayIdx > GiBeatsPerBar) {
                GiPlayIdx = 1; // Back to beat 1.
                /* Check if repeat is off */
                if (GbRepeat == FALSE) {
                    usleep(1000);
                    Gplay_state = PLAY_STATE_PAUSED;
                }
            }
        }
     
        // Thread2 now has the lock, and is progressing under protection of the lock.
        
        if (-1 == SocketThreadTryLock()) {
            // Socket thread has finished, we probably need to finish too.
            iSocketQuit = 1;
            GiSpeedBPM = 1000;
        }

        if (iSocketQuit == 1) {
            /* Signal to Thread2 to do something by changing the value of the int value we passed in at thread creation. */
            
            for (idx = 0; idx < NUM_WORKER_THREADS; idx++) {
                if (x[idx] == 0) {
                    if (GiDebug == 1) printf("Main Thread - signalling to end Thread[%d] now.\n", idx);
                    x[idx] = THREAD_STATE_TO_QUIT;
                }
            }
        }
        
        // Use a different thread next time, just for variety.
        ++ThreadIdx;
        if (ThreadIdx >= NUM_WORKER_THREADS) ThreadIdx = 0;

        
        if (GiDebug == 1) printf("MAIN - Sleeping\n");
        usleep(BPMToUS(GiSpeedBPM));
    }

    for (idx = 0; idx < NUM_WORKER_THREADS; idx++) {
        /* Wait for t2 thread to finish */
        if (pthread_join(t2[idx], NULL))
        {
            fprintf(stderr, "Error joining thread.\n");
            return -2;
        }
    }

    if (GiDebug == 1) printf("Thread Main finished.\n");
    
    if (GiDebug == 1) printf("Closing Socket Thread.\n");
    SocketThreadEnd();

    for (idx = 0; idx < NUM_WORKER_THREADS; idx++) {
        mutex_destroy(&p_mutex[idx]);
    }
    
    pthread_key_delete(main_thread_key);
    free(buf);

    return 0;
}



void MultiThreadSequencer_Play(void)
{
    GiPlayIdx = 1; // Back to beat 1.
    Gplay_state = PLAY_STATE_PLAYING;
    if (GiDebug == 1) printf("State is now playing\n");

    // Initally play a period of silence.
    mutex_lock(&p_mutex[ThreadIdx]);
    GiWorkerThreadSequenceIndex[ThreadIdx] = 2; // Index 2 is silence.
    pthread_cond_signal(&cond[ThreadIdx]);
    mutex_unlock(&p_mutex[ThreadIdx]);
    ++ThreadIdx;
    if (ThreadIdx >= NUM_WORKER_THREADS) ThreadIdx = 0;
    usleep(100000);
}

void MultiThreadSequencer_Pause(void)
{
    Gplay_state = PLAY_STATE_PAUSED;
    if (GiDebug == 1) printf("State is now paused\n");
}

void MultiThreadSequencer_SetSpeedBPM(int iSpeedBPM)
{
    if (iSpeedBPM > 0) GiSpeedBPM=iSpeedBPM;
}
    
void MultiThreadSequencer_SetRepeat(bool bRepeat)
{
    GbRepeat = bRepeat;
}

void MultiThreadSequencer_SetBeatsPerBar(unsigned int i)
{
    if (i > 0) {
      GiBeatsPerBar = i;
    } 
    else {
        printf("MultiThreadSequencer_SetBeatsPerBar(), Error setting beats to bar to %u\n", i);
    }
}

