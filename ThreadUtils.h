
/* Mutex stuff */

typedef struct
{
    pthread_mutex_t p_mutex;
    int old_cancel_state;
} l_mutex_t;


void mutex_init(l_mutex_t *mutex);
void mutex_destroy(l_mutex_t *mutex);
void mutex_lock(l_mutex_t *mutex);
int mutex_trylock(l_mutex_t *mutex);
void mutex_unlock(l_mutex_t *mutex);


/* Thread stuff */

void printThreadSpecificValue(pthread_key_t *);
