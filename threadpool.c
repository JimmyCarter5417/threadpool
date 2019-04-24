#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <sys/prctl.h>
#include "threadpool.h"


#ifdef DEBUG
    #define error(str) fprintf(stderr, str)
#else
    #define error(str)
#endif


#define ERROR_SUCCESS  0
#define ERROR_FAILED  -1


/* 条件变量 */
typedef struct MYCOND 
{
    pthread_mutex_t stLock;
    pthread_cond_t  stCond;
    bool            bSignal;
}MYCOND_S;

/* 任务 */
typedef struct TASK
{
    TASK*        pstNext;
    CALLBACK_PF  pfCallBack;
    void*        pArg;
}TASK_S;

/* 工作队列 */
typedef struct QUEUE
{
    pthread_mutex_t stLock;             /* 用于读写同步         */
    TASK_S         *pstHead;            
    TASK_S         *pstTail;            
    MYCOND_S        stNotEmpty;         /* 用于通知线程队列非空 */
    int             iLen;               /* 工作队列长度       */
}QUEUE_S;

/* 线程 */
typedef struct THREAD
{
    int        iID;
    pthread_t  stID;
    POOL_S*    pstPool;

/* 线程池 */
typedef struct POOL
{
    THREAD_S**       ppstThreads;           /* 动态创建所有线程        */
    QUEUE_S          stQueue;               /* 没必要动态创建任务队列 */
    pthread_mutex_t  stLock;                /* 用于同步各个计数 */
    pthread_cond_t   stAllIdle;             /* 用于通知线程池空闲 */
    int              iID;
    bool             bIsRunning;
    bool             bIsPausing;
    volatile int     iNumThreadsAlive;      /* 当前存活的线程数         */
    volatile int     iNumThreadsWorking;    /* 当前工作的线程数 */
}POOL_S;


static int _Thread_Create(POOL_S* pstPool, struct THREAD_S** ppstThread, int iID)
{
    int iRet = ERROR_SUCCESS;
    
    *ppstThread = (THREAD_S*)malloc(sizeof(THREAD_S));
    if (NULL == ppstThread)
    {
        error("_Thread_Create: malloc failed for a new thread %d\n", iID);
        return ERROR_FAILED;
    }

    (*ppstThread)->pstPool = pstPool;
    (*ppstThread)->iID     = iID;

    iRet = pthread_create(&(*ppstThread)->stID, NULL, _Thread_CallBack, (void*)(*ppstThread));
    iRet |= pthread_detach((*ppstThread)->stID);
    
    if (ERROR_SUCCESS != iRet)
    {
        error("pthread_create failed: iRet = %d\n", iRet);
    }
    
    return iRet;
}


/* 线程信号处理函数 */
static void _Thread_Signal(int iSignal) 
{
    switch (iSignal)
    {
        case SIGUSR1:
        {
            break;
        }
        case SIGUSR2:
        {
            break;
        }
        default:
        {
            break;
        }
    }

    return;
}

/* 线程回调函数 */
static void* _Thread_CallBack(void* pArg)
{
    if (NULL == pArg)
    {
        error("_Thread_CallBack: NULL pArg\n");
        return NULL;
    }
    
    THREAD_S* pstThread = (THREAD_S*)pArg;
    POOL_S*   pstPool   = pstThread->pstPool;
    
    /* 设置线程名字 */
    char szThreadName[128] = {0};
    snprintf(szThreadName, 
             sizeof(szThreadName), 
             "pool-%d-thread-%d", 
             pstPool->iID,
             pstThread->iID);
    prctl(PR_SET_NAME, szThreadName);

    /* 设置SIGUSR1/SIGUSR2的信号处理函数 */
    struct sigaction stSa;
    sigemptyset(&stSa.sa_mask);
    stSa.sa_flags   = SA_RESTART; /* 自动重启被中断的系统调用 */
    stSa.sa_handler = _Thread_Signal;
    if (-1 == sigaction(SIGUSR1, &stSa, NULL))
    {
        error("_Thread_CallBack: failed to set SIGUSR1\n");
    }
    if (-1 == sigaction(SIGUSR2, &stSa, NULL))
    {
        error("_Thread_CallBack: failed to set SIGUSR2\n");
    }

    /* 线程已启动，将存活线程计数加一 */
    pthread_mutex_lock(&pstPool->stLock);
    pstPool->iNumThreadsAlive++;
    pthread_mutex_unlock(&pstPool->stLock);

    while(pstPool->bIsRunning)
    {
        /* 在工作队列等待任务 */
        _Mycond_Wait(&pstPool->stQueue.stNotEmpty);
        /* 如果用户已经终止了线程池，就不处理任务了 */
        if (!pstPool->bIsRunning)
        {
            break;
        }

        /* 线程开始干活了，增加工作线程计数 */
        pthread_mutex_lock(&pstPool->stLock);
        pstPool->iNumThreadsWorking++;
        pthread_mutex_unlock(&pstPool->stLock);

        /* todo:增加批量处理功能 */
        TASK_S* pstTask = _Queue_Pull(&pstPool->stQueue);
        if (pstTask) 
        {
            CALLBACK_PF pfCB = pstTask->pfCallBack;
            void*      *pArg = pstTask->pArg;
            
            if (NULL != pfCB);
            {
                (void)pfCB(pArg);
            }

            /* todo：待完善 */
            free(pstTask); /* 释放task资源 */
        }

        /* 处理完任务后，线程空闲，工作线程计数减一 */
        pthread_mutex_lock(&pstPool->stLock);
        if (0 == --pstPool->iNumThreadsWorking) /* 所有线程都空闲，可发射信号量 */
        {
            pthread_cond_signal(&pstPool->stAllIdle);
        }
        pthread_mutex_unlock(&pstPool->stLock);
    }

    /* 线程已结束，将存活线程计数减一 */
    pthread_mutex_lock(&pstPool->stLock);
    pstPool->iNumThreadsAlive --;
    pthread_mutex_unlock(&pstPool->stLock);

    return NULL;
}


/* 销毁线程 */
static void _Thread_Destroy(THREAD_S* pstThread)
{
    assert(NULL != pstThread);

    free(pstThread);

    return;
}


/* 工作队列初始化 */
static int _Queue_Init(QUEUE_S* pstQueue)
{
    assert(NULL != pstQueue);

    pstQueue->iLen    = 0;
    pstQueue->pstHead = NULL;
    pstQueue->pstTail = NULL;

    pthread_mutex_init(&pstQueue->stLock, NULL);
    _Mycond_Init(&pstQueue->stNotEmpty);

    return ERROR_SUCCESS;
}

/* 向工作队列添加任务 */
static void _Queue_Push(QUEUE_S* pstQueue, TASK_S* pstTask)
{
    assert(NULL != pstQueue);
    assert(NULL != pstTask);

    pthread_mutex_lock(&pstQueue->stLock);
    
    if (0 == pstQueue->iLen)
    {
        pstQueue->pstHead = pstTask;
        pstQueue->pstTail = pstTask;
     }
    else
    {
        pstQueue->pstTail->pstNext = pstTask; /* 在尾部添加结点 */
        pstQueue->pstTail = pstTask;
    }
    pstTask->pstNext = NULL;
    pstQueue->iLen++;

    _Mycond_Signal(pstQueue->stNotEmpty); /* 发射信号 */
    
    pthread_mutex_unlock(&pstQueue->stLock);

    return;
}

/* 从工作队列获得任务 */
static TASK_S* _Queue_Pull(QUEUE_S* pstQueue)
{
    assert(NULL != pstQueue);
    
    TASK_S* pstTask = NULL;

    pthread_mutex_lock(&pstQueue->stLock);
    
    pstTask = pstQueue->pstHead;
    if (1 == pstQueue->iLen)
    {
        pstQueue->pstHead = NULL;
        pstQueue->pstTail = NULL;
        pstQueue->iLen    = 0;    /* 工作队列为空，不能发射信号 */
     }
    else if (1 < pstQueue->iLen)
    {
        pstQueue->pstHead = pstTask->pstNext; /* 摘掉头结点 */
        pstQueue->iLen--;

        _Mycond_Signal(pstQueue->stNotEmpty); /* 工作队列非空，需要发射信号 */
    }
    /* 不会出现长度为0的情况 */

    pthread_mutex_unlock(&pstQueue->stLock);
    
    return pstTask;
}

/* 销毁工作队列 */
static void _Queue_Destroy(QUEUE_S* pstQueue)
{
    assert(NULL != pstQueue);
    /* 既然要销毁了，必须保证没有其他线程在还使用工作队列 */
    /* 此处不需要也不能同步，因为连锁都要被销毁了 */
    while(pstQueue->iLen > 0)
    {
        free(_Queue_Pull(pstQueue));
    }
    
    pstQueue->iLen    = 0;
    pstQueue->pstHead = NULL;
    pstQueue->pstTail = NULL;

    pthread_mutex_destroy(&pstQueue->stLock);
    _Mycond_Destroy(&pstQueue->stNotEmpty);

    return;
}

/* 条件变量初始化 */
static void _Mycond_Init(MYCOND_S *pstMycond) 
{
    assert(NULL != pstMycond);
    
    pthread_mutex_init(&pstMycond->stLock, NULL);
    pthread_cond_init(&pstMycond->stCond, NULL);
    pstMycond->bSignal = false;

    return;
}

/* 销毁条件变量 */
static void _Mycond_Destroy(MYCOND_S *pstMycond) 
{
    assert(NULL != pstMycond);
    
    pthread_mutex_destroy(&pstMycond->stLock);
    pthread_cond_destroy(&pstMycond->stCond);
    pstMycond->bSignal = false;

    return;
}


/* 条件变量signal操作 */
static void _Mycond_Signal(MYCOND_S *pstMycond) 
{
    assert(NULL != pstMycond);

    pthread_mutex_lock(&pstMycond->stLock);
    pstMycond->bSignal = true;
    pthread_cond_signal(&pstMycond->stCond);
    pthread_mutex_unlock(&pstMycond->stLock);

    return;
}


/* 条件变量broadcast操作 */
static void _Mycond_Broadcast(MYCOND_S *pstMycond) 
{
    assert(NULL != pstMycond);

    pthread_mutex_lock(&pstMycond->stLock);
    pstMycond->bSignal = true;
    pthread_cond_broadcast(&pstMycond->stCond);
    pthread_mutex_unlock(&pstMycond->stLock);

    return;
}


/* 条件变量wait操作 */
static void _Mycond_Wait(MYCOND_S* pstMycond) 
{
    assert(NULL != pstMycond);

    pthread_mutex_lock(&pstMycond->stLock);
    while (!pstMycond->bSignal) 
    {
        pthread_cond_wait(&pstMycond->stCond, &pstMycond->stLock);
    }
    pstMycond->bSignal = false;
    pthread_mutex_unlock(&pstMycond->stLock);

    return;
}


/* Initialise thread pool */
struct void* Pool_Create(int iID, int iNumThreads)
{
    if (iNumThreads <= 0)
    {
        error("Pool_Create: invalid param, iNumThreads=%d\n", iNumThreads);

        return NULL;
    }

    /* 创建线程池 */
    POOL_S* pstPool = (POOL_S*)malloc(sizeof(POOL_S));
    if (NULL != pstPool)
    {
        error("Pool_Create: Could not allocate memory for thread pool\n");
        
        return NULL;
    }

    memset(pstPool, 0, sizeof(POOL_S));
    
    /* 创建工作队列 */
    if (ERROR_SUCCESS != _Queue_Init(&pstPool->stQueue))
    {
        error("Pool_Create: Could not allocate memory for task queue\n");
        
        free(pstPool);
        
        return NULL;
    }

    /* 为所有线程分配内存 */
    pstPool->ppstThreads = (THREAD_S**)malloc(iNumThreads * sizeof(THREAD_S *));
    if (NULL == pstPool->ppstThreads)
    {
        error("Pool_Create: Could not allocate memory for threads\n");

        free(pstPool);
        _Queue_Destroy(&pstPool->stQueue);
        
        return NULL;
    }

    /* 先初始部分变量，线程启动后会用到 */
    pthread_mutex_init(&pstPool->stLock, NULL);
    pthread_cond_init(&pstPool->stAllIdle, NULL);
    pstPool->iID = iID;
    pstPool->iNumThreadsAlive   = 0;
    pstPool->iNumThreadsWorking = 0;
    pstPool->bIsPausing = false;
    pstPool->bIsRunning = false;

    /* 创建所有线程 */
    int i;
    for (i = 0; i < iNumThreads; i++)
    {
        if (ERROR_SUCCESS != _Thread_Create(pstPool, &pstPool->ppstThreads[i], i))
        {
            error("Pool_Create: failed to _Thread_Create for i=%d\n", i);
        }
    }

    /* 等待所有线程启动然后进入等待状态，先简单用个循环处理一下 */
    while (pstPool->iNumThreadsAlive != iNumThreads) 
    {

    }
    
    /* 搞起 */
    pstPool->bIsRunning = true;

    return (void*)pstPool;
}

/* 向线程池添加任务 */
int Pool_AddTask(void* pPool, CALLBACK_PF pfCallBack, void* pArg)
{
    if ((NULL == pPool) || (NULL == pfCallBack))
    {
        error("Pool_AddTask: invalid param: pPool=%p, pfCallBack=%p\n", 
            pPool, 
            pfCallBack);

        return ERROR_FAILED;
    }
    
    POOL_S* pstPool = (POOL_S*)pPool;
    TASK_S* pstTask = (TASK_S*)malloc(sizeof(TASK_S));
    if (NULL == pstTask)
    {
        error("Pool_AddTask: Could not allocate memory for new job\n");
        
        return ERROR_FAILED;
    }

    /* 初始化task */
    pstTask->pstNext    = NULL;
    pstTask->pfCallBack = pfCallBack;
    pstTask->pArg       = pArg;

    /* 插入任务队列 */
    /* todo：为任务队列设置规格限制 */
    _Queue_Push(&pstPool->stQueue, pstTask);

    return ERROR_SUCCESS;
}


/* 等待线程池停止运行：任务队列为空且线程均空闲 */
void Pool_Wait(void* pPool)
{
    if (NULL == pPool)
    {
        error("Pool_Wait: NULL pPool\n");

        return ;
    }
    
    POOL_S* pstPool = (POOL_S*)pPool;

    /* 等待停止 */
    pthread_mutex_lock(&pstPool->stLock);
    while (pstPool->stQueue.iLen || pstPool->iNumThreadsWorking) // todo:这里有问题
    {
        pthread_cond_wait(&pstPool->stAllIdle, &pstPool->stLock);
    }
    pthread_mutex_unlock(&pstPool->stLock);

    return;
}

/* 销毁线程池 */
/* todo：待完善 */
void Pool_Destroy(void* pPool)
{
    if (NULL == pPool)
    {
        error("Pool_Destroy: NULL pPool\n");
        return;
    }

    POOL_S*      pstPool     = (POOL_S*)pPool;
    volatile int iNumThreads = pstPool->iNumThreadsAlive; /* 先保存线程数目 */

    pstPool->bIsRunning = false; /* 终止线程回调函数循环 */

    /* 先简单处理，广播后等一秒 */
    /* todo：待优化 */
    while (pstPool->iNumThreadsAlive)
    {
        _Mycond_Broadcast(pstPool->stQueue.stNotEmpty);
        sleep(1);
    }

    /* 销毁工作队列 */
    _Queue_Destroy(&pstPool->stQueue);
    /* 释放线程内存 */
    int i;
    for (i = 0; i < iNumThreads; i++)
    {
        _Thread_Destroy(pstPool->ppstThreads[i]);
    }
    /* 释放线程池内存 */
    free(pstPool->ppstThreads);
    free(pstPool);

    return;
}

/* 暂停线程池 */
/* todo:待完善 */
void Pool_Pause(void* pPool) 
{    
    if (NULL == pPool)
    {
        error("Pool_Pause: NULL pPool\n");
        return ;
    }
    
    POOL_S* pstPool = (POOL_S*)pPool;

    int i;
    for(i = 0; i < pstPool->iNumThreadsAlive; i++)
    {
        /* 向所有线程发送SIGUSR1信号 */
        pthread_kill(pstPool->ppstThreads[i]->stID, SIGUSR1);
    }

    return;
}

/* 重启线程池 */
/* todo:待完善 */
void Pool_Resume(void* pPool) 
{
    if (NULL == pPool)
    {
        error("Pool_Pause: NULL pPool\n");
        return ;
    }
    
    POOL_S* pstPool = (POOL_S*)pPool;

    int i;
    for(i = 0; i < pstPool->iNumThreadsAlive; i++)
    {
        /* 向所有线程发送SIGUSR2信号 */
        pthread_kill(pstPool->ppstThreads[i]->stID, SIGUSR2);
    }

    return;
}

int Pool_GetNumWorkingThreads(void* pPool)
{
    if (NULL == pPool)
    {
        error("Pool_GetNumWorkingThread: NULL pPool\n");
        return ;
    }

    POOL_S* pstPool = (POOL_S*)pPool;

    return pstPool->iNumThreadsWorking;
}

