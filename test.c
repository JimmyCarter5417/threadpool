#include <stdio.h>
#include <pthread.h>
#include <sys/prctl.h>


#include "threadpool.h"

static pthread_mutex_t g_stLock1 = PTHREAD_MUTEX_INITIALIZER;

int Func1(void* pArg)
{
    if (NULL != pArg)
    {
        char szThreadName[128] = {0};
        prctl(PR_GET_NAME, szThreadName);

        pthread_mutex_lock(&g_stLock1);
        printf("thread=%s, arg=%d\n", szThreadName, pArg);
        pthread_mutex_unlock(&g_stLock1);
    }

    return 0;
}

int main(int argc, char* argv[])
{
    void* pPool = Pool_Create(1, 4);
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
