

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>

#ifndef TRUE
#define FALSE 0
#define TRUE (!FALSE)
#endif

#include "Mutex.h"
#include "Socket.h"
#include "Metronome.h"


#define PORT_NUM 5100

int GiPortNum = PORT_NUM;
static pthread_key_t Socketkey;
static l_mutex_t p_mutex;
char *sBuf;
int sListenfd = -1;
pthread_t t2;
int SiDebug = 0;

typedef enum
{
    NO_OP,
    QUIT,
    PLAY,
    PAUSE,
    SET_SPEED,
    REPEAT_OFF,
    REPEAT_ON,
    SET_BEATS_PER_BAR,
}   command_t;


int ProcessSocketRequests(int connfd)
{
    command_t command = NO_OP;
    char recvBuffer[1024] = {0};
    int recvNum;
    int iRet = 0;
    int iSpeed = 0;
    unsigned int uiBeatsPerBar = 0;
    
    /* Capture the bytes coming from CURL, we want to parse the text that comes in the URL, for example:
    * #> curl http://127.0.0.1:5000/api/getDate
    * We want to isolate the /api/getDate part.
    * 
    * It should look a bit like:
    * 'GET /api/getDate HTTP/1.1
        Host: 127.0.0.1:5000
        User-Agent: curl/7.68.0'

    */

    recvBuffer[0] = '\0';
    recvNum = recv(connfd, recvBuffer, 1022, 0);
    if (recvNum > 0) {
        
        /* Now to isolate the /api/getDate part (if there is one). */
        char *pch, *cmd = NULL;
        int idx = 0, gotGET=0, gotPOST=0;
        
        pch = strtok(recvBuffer, " \n\r");
        while (pch != NULL) {
            //printf("%d:'%s'\n", idx, pch);
            
            if (idx == 0 && 0 == strcmp(pch, "GET")) { gotGET = 1; }
            if ( gotGET == 1 && idx == 1) {
                cmd = pch;
            }
            if (idx == 0 && 0 == strcmp(pch, "POST")) { gotPOST = 1; }
            if (gotPOST == 1) {
                if (0 == strncmp(pch, "speed=", 6)) {
                    cmd = pch;
                }
                else if (0 == strncmp(pch, "repeat=", 7)) {
                    cmd = pch;
                }
                else if (0 == strncmp(pch, "beats=", 6)) {
                    cmd = pch;
                }
            }
            
            ++idx;
            pch = strtok(NULL, " \n\r");
        }
        if ((gotGET==1 || gotPOST==1) && cmd != NULL) {
            if (0 == strcmp(cmd, "/api/quit")) { command = QUIT; }
            if (0 == strcmp(cmd, "/api/play")) { command = PLAY; }
            if (0 == strcmp(cmd, "/api/pause")) { command = PAUSE; }
            if (0 == strncmp(cmd, "speed=", 6)) {
                // Separate out the speed variable.
                command=SET_SPEED;
                idx=0;
                pch = strtok(cmd, "=");
                while (pch != NULL) {
                    
                    if (idx == 1) {
                        iSpeed = atoi(pch);
                    }
                    
                    ++idx;
                    pch = strtok(NULL, "=");
                }
            }
            if (0 == strncmp(cmd, "repeat=off", 10)) { command = REPEAT_OFF; }
            if (0 == strncmp(cmd, "repeat=on", 9)) { command = REPEAT_ON; }
            if (0 == strncmp(cmd, "beats=", 6)) {
                // Separate out the beats per bar variable.
                command=SET_BEATS_PER_BAR;
                idx=0;
                pch = strtok(cmd, "=");
                while (pch != NULL) {
                    
                    if (idx == 1) {
                        uiBeatsPerBar = atoi(pch);
                    }
                    
                    ++idx;
                    pch = strtok(NULL, "=");
                }
            }
        }
    }
    
    if (command == QUIT) {
        if (SiDebug == 1) printf("Quit Received on socket.\n");
        close(connfd);
        iRet = -1;
    }
    else if (command == PLAY) {
        MultiThreadSequencer_Play();
    }
    else if (command == PAUSE) {
        MultiThreadSequencer_Pause();
    }
    else if (command == SET_SPEED) {
        //printf("Socket, received speed=%d\n", iSpeed);
        MultiThreadSequencer_SetSpeedBPM(iSpeed);
    }
    else if (command == REPEAT_OFF) { MultiThreadSequencer_SetRepeat(FALSE); }
    else if (command == REPEAT_ON) { MultiThreadSequencer_SetRepeat(TRUE); }
    else if (command == SET_BEATS_PER_BAR) {
        MultiThreadSequencer_SetBeatsPerBar(uiBeatsPerBar);
    }
    
    return iRet;
}


int LaunchSocket(void *x_void_ptr, int iPortNum)
{
    int *listenfd = x_void_ptr;
    int connfd = 0;
    struct sockaddr_in serv_addr;
    struct sockaddr_in accept_addr;
    int addrlen = sizeof(accept_addr);
    int atrue = 1;
    int iRet = 0;
    
    /* creates an UN-named socket inside the kernel and returns
     * an integer known as socket descriptor
     * This function takes domain/family as its first argument.
     * For Internet family of IPv4 addresses we use AF_INET
     */
    *listenfd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&serv_addr, '0', sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(iPortNum);
    
    if (setsockopt(*listenfd, SOL_SOCKET, SO_REUSEADDR, &atrue, sizeof(int)) == -1) {
        fprintf(stderr, "Error in setsockopt at line %d\n", __LINE__);
        iRet = -1;
    } else {

        /* The call to the function "bind()" assigns the details specified
        * in the structure 『serv_addr' to the socket created in the step above
        */
        if (0 != bind(*listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr))) {
            fprintf(stderr, "failed to bind at line %d, errno=%d\n", __LINE__, errno);
        } else {

            /* The call to the function "listen()" with second argument as 10 specifies
            * maximum number of client connections that server will queue for this listening
            * socket.
            */
            listen(*listenfd, 10);

            while (1)
            {
                /* In the call to accept(), the server is put to sleep and when for an incoming
                * client request, the three way TCP handshake* is complete, the function accept()
                * wakes up and returns the socket descriptor representing the client socket.
                */
                
                connfd = accept(*listenfd, (struct sockaddr*)&accept_addr, (socklen_t*)&addrlen);
                if (connfd >= 0) {
                    
                    if (-1 == ProcessSocketRequests(connfd)) {
                        break;
                    }

                    close(connfd);
                }
                else {
                    // Main Thread has probably signalled that this thread should shut down, but shutting down our socket.
                    break;
                }

                sleep(1);
            }
        }
    }
    
    close(*listenfd);
    
    return iRet;
}

void *SocketThread(void *x_void_ptr)
{
    mutex_lock(&p_mutex);
    
    /* This call blocks here, and services any messages coming in to the PORT using CURL requests. */
    LaunchSocket(x_void_ptr, GiPortNum);

    mutex_unlock(&p_mutex);

    return NULL;
}

int SocketThreadTryLock(void)
{
    /* Check the Socket thread. */
    if (0 == mutex_trylock(&p_mutex)) {
        /* We got the mutex, so Thread2 has finished. */
        if (SiDebug == 1) printf("Main Thread has the mutex, SocketThread has finished.\n");
        return -1;
    }
    
    return 0;
}


int SocketThreadStart(int iDebug)
{
    int iRet = 0;
    
    SiDebug = iDebug;
    
    sBuf = (char *)malloc(64);
    if (sBuf == NULL) {
        fprintf(stderr, "Failed to allocate memory at line:%d\n", __LINE__);
        return -1;
    }

    strcpy(sBuf, "Socket Thread");
    
    /* Socket Thread init... */
    mutex_init(&p_mutex);
    
    if (0 != pthread_key_create(&Socketkey, NULL)) {
        fprintf(stderr, "Failed in pthread_key_create at line:%d\n", __LINE__);
        iRet = -1;
    } else {
    
        pthread_setspecific(Socketkey, sBuf);
        
        // Thread2 start.
        if (SiDebug == 1) printf("Starting SocketThread\n");
        if (pthread_create(&t2, NULL, SocketThread, &sListenfd))
        {
            fprintf(stderr, "Error Creating Thread\n");
            iRet = -1;
        } else {

            /* Wait for thread 2 to start, and grab the mutex. */
            usleep(10000);
            /* Socket Thread ready. */
        }
    }
    
    return iRet;
}


void SocketThreadEnd(void)
{
    /* If we get here then the Socket listener processing has quit, time to cleanup. */
    mutex_unlock(&p_mutex);
    
    /* Wait for t2 thread to finish */
    if (pthread_join(t2, NULL))
    {
        fprintf(stderr, "Error joining thread.\n");
        return;
    }

    if (SiDebug == 1) printf("Socket Thread shut down.\n");

    mutex_destroy(&p_mutex);
    
    pthread_key_delete(Socketkey);
    free(sBuf);
}
