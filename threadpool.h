#ifndef _THREADPOOL_H
#define _THREADPOOL_H
//struct ThreadPool;这里声明一下有这个结构体，定义在别的地方了(其实也可以直接定义在这个头文件里)
//使用typedef定义了个别名ThreadPool
typedef struct ThreadPool ThreadPool;
//创建线程池并初始化
ThreadPool *threadPoolCreate(int min, int max, int queueSize);
//销毁线程池
int thradPoolDestory(ThreadPool* pool);

//添加任务到线程池
void threadPoolAdd(ThreadPool* pool, void(*func)(void*), void* arg);

//获得当前正在工作的线程数
int threadPoolBusyNum(ThreadPool* pool);

//活着的线程数
int threadPoolAliveNum(ThreadPool* pool);


void* worker(void* arg);//工作线程的函数
void* manager(void* arg);//管理者线程的函数
void threadExit(ThreadPool* pool);//销毁线程，并重置threadIDs对应位置的值为0，便于存储后续新增工作线程id

#endif // !_THREADPOOL_H

