


void *SocketThread(void *x_void_ptr);
int SocketThreadStart(int iDebug);
void SocketThreadEnd(void);
int SocketThreadTryLock(void);
int LaunchSocket(void *x_void_ptr, int iPortNum);
int ProcessSocketRequests(int connfd);
