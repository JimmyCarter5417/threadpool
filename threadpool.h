#ifndef _THREAD_POOL_
#define _THREAD_POOL_

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*CALLBACK_PF)(void*);

void* Pool_Init(int num_threads);
int Pool_AddTask(void* p, CALLBACK_PF cb, void* arg);
void Pool_Wait(void* p);
void Pool_Destroy(void* p);
int Pool_GetNumWorkingThread(void* p);


#ifdef __cplusplus
}
#endif

#endif