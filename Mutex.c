
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "Mutex.h"

void mutex_init(l_mutex_t *mutex)
{
    pthread_mutexattr_t attr;

    pthread_mutexattr_init( &attr );

    pthread_mutex_init( &mutex->p_mutex, &attr );
}

void mutex_destroy(l_mutex_t *mutex)
{
    pthread_mutex_destroy( &mutex->p_mutex );
}

void mutex_lock(l_mutex_t *mutex)
{
    /* Disable thread cancelling */
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &mutex->old_cancel_state);

    /* Get the list protection mutex */
    pthread_mutex_lock( &mutex->p_mutex );
}

int mutex_trylock(l_mutex_t *mutex)
{
    int iRet = 0; // default. Return 0 if lock is successful. Return 1 if lock fails.
    /* Disable thread cancelling */
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &mutex->old_cancel_state);
    
    if (EBUSY == pthread_mutex_trylock( &mutex->p_mutex )) {
        /* Restore thread cancelling state */
        pthread_setcancelstate(mutex->old_cancel_state, NULL);
        iRet = 1; // Failure.
    }
    
    return iRet;
}

void mutex_unlock(l_mutex_t *mutex)
{
    /* Release the list protection mutex */
    pthread_mutex_unlock( &mutex->p_mutex );

    /* Restore thread cancelling state */
    pthread_setcancelstate(mutex->old_cancel_state, NULL);

    /* Test for cancel */
    pthread_testcancel();
}
