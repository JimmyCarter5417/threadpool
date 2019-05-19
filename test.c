#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <stdlib.h>
#include <sys/prctl.h>


#include "threadpool.h"

static pthread_mutex_t g_stLock1 = PTHREAD_MUTEX_INITIALIZER;

int Func1(void* pArg)
{
    if (NULL != pArg)
    {
        char szThreadName[128] = {0};
        prctl(PR_GET_NAME, szThreadName);

        /* 随机睡眠1-100ms */
        srand((unsigned)time(NULL));
        struct timespec ts = {0, rand() % 100 * 1000000};
        nanosleep(&ts, NULL);

        pthread_mutex_lock(&g_stLock1);
        printf("thread=%s, arg=%03d\n", szThreadName, (int)pArg);
        pthread_mutex_unlock(&g_stLock1);
    }

    return 0;
}

int main(int argc, char* argv[])
{
    if ((3 != argc) || (atoi(argv[2]) <= 0))
    {
        printf("usage: test [pool-id] [thread-num]\n");
        return 1;
    }
    
    void* pPool = Pool_Create(atoi(argv[1]), atoi(argv[2]));
    if (NULL != pPool)
    {
        int i;
        for (i = 0; i < 1000; i++)
        {
            Pool_AddTask( pPool, Func1, (void*)i);
        }
        
        Pool_Wait(pPool);
        Pool_Destroy(pPool);
    }

    return 0;
}
