#ifndef _THREADPOOL_
#define _THREADPOOL_

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*CALLBACK_PF)(void*);


void* Pool_Create(int iID, int iNumThreads);
int   Pool_AddTask(void* pPool, CALLBACK_PF pfCallBack, void* pArg);
void  Pool_Wait(void* pPool);
void  Pool_Pause(void* pPool);
void  Pool_Resume(void* pPool);
void  Pool_Destroy(void* pPool);
int   Pool_GetNumWorkingThreads(void* pPool);


#ifdef __cplusplus
}
#endif

#endif