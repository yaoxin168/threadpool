#include "threadpool.h"
#include <pthread.h>
#include <stdlib.h>//malloc
#include <stdio.h>//printf
#include <string.h>//memset
#include <unistd.h>//sleep

const int NUMBER = 2;//调节线程池内存活的线程数量时，一次性增减几个线程

//任务结构体
typedef struct Task
{
	void (*function)(void* arg);//函数指针，返回值为void
	void* arg;//函数的参数
}Task;

//线程池结构体
struct ThreadPool
{
	Task* taskQ;//任务队列，由队头队尾下标维护成一个环形队列
	int queueCapacity;//任务队列的容量
	int queueSize;//当前任务个数
	int queueFront;//队头下标
	int queueRear;//队尾下标

	pthread_t managerID;//管理者线程ID，管理者线程只有一个
	pthread_t *threadIDs;//工作的线程ID，有多个工作线程
	int minNum;//最小线程数
	int maxNum;//最大线程数
	int busyNum;//当前在工作的线程个数
	int liveNum;//当前存活的线程个数
	int exitNum;//如果大部分线程都闲着，需要杀死多少个线程

	pthread_mutex_t mutexPool;//互斥锁，锁整个线程池
	pthread_mutex_t mutexBusy;//单独用于锁busyNum，因为busyNum变化很频繁，如果每次都直接锁整个线程池的不太好

	pthread_cond_t notFull;//条件变量，任务队列不满时唤醒生产者线程
	pthread_cond_t notEmpty;//任务队列不为空时唤醒工作线程(消费者)

	int shutdown;//要不要销毁线程池，销毁为1，不销毁为0
};

//线程池初始化
ThreadPool* threadPoolCreate(int min, int max, int queueSize)
{
	ThreadPool* pool = (ThreadPool*)malloc(sizeof(ThreadPool));
	do
	{
		if (pool == NULL)//内存分配失败
		{
			printf("malloc threadpool fail...\n");
			break;
		}

		pool->threadIDs = (pthread_t*)malloc(sizeof(pthread_t) * max);
		if (pool->threadIDs == NULL)
		{
			printf("malloc threadIDs fail...\n");
			break;
		}

		memset(pool->threadIDs, 0, sizeof(pthread_t) * max);//把这片内存初始化为0，表示该位置未存储过线程id，是空着的
		pool->minNum = min;
		pool->maxNum = max;
		pool->busyNum = 0;
		pool->liveNum = min;//刚开始的存活线程数 和 最小线程个数相等
		pool->exitNum = 0;

		//使用动态方法创建互斥锁和条件变量
		if (pthread_mutex_init(&pool->mutexPool, NULL) != 0 ||
			pthread_mutex_init(&pool->mutexBusy, NULL) != 0 ||
			pthread_cond_init(&pool->notEmpty, NULL) != 0 ||
			pthread_cond_init(&pool->notFull, NULL) != 0)
		{
			printf("mutex or condition fail\n");
		}

		//任务队列
		pool->taskQ = (Task *)malloc(sizeof(Task) * queueSize);
		pool->queueCapacity = queueSize;
		pool->queueSize = 0;
		pool->queueFront = 0;
		pool->queueRear = 0;
		pool->shutdown = 0;

		//创建线程
		pthread_create(&pool->managerID, NULL, manager, pool);//管理者线程
		//工作线程
		for (int i = 0; i < min; i++)
		{//工作线程的函数应该传入pool，到时候可以直接取到pool里面的任务队列taskQ和一些锁
			pthread_create(&pool->threadIDs[i], NULL, worker, pool);
		}

		return pool;


	} while (0);

	//初始化失败，需要释放资源
	if (pool && pool->threadIDs)
	{
		free(pool->threadIDs);
	}
	if (pool && pool->taskQ)
	{
		free(pool->taskQ);
	}
	if (pool) free(pool);

	return NULL;
}

int thradPoolDestory(ThreadPool* pool)
{
	if (pool == NULL)
	{
		return -1;
	}
	//关闭线程池
	pool->shutdown = 1;
	//阻塞等待指定线程(管理者线程)结束才继续执行
	pthread_join(pool->managerID, NULL);
	//唤醒阻塞的所有消费者线程(工作线程)，它们的代码处会判断线程池是否已关闭，如果已关闭，就自杀 
	for (int i = 0; i < pool->liveNum; i++)
	{
		pthread_cond_signal(&pool->notEmpty);
	}

	//释放堆内存
	if (pool->taskQ) free(pool->taskQ);
	if (pool->threadIDs) free(pool->threadIDs);
	
	//把所有互斥锁和条件变量资源释放掉
	pthread_mutex_destroy(&pool->mutexPool);
	pthread_mutex_destroy(&pool->mutexBusy);
	pthread_cond_destroy(&pool->notEmpty);
	pthread_cond_destroy(&pool->notFull);

	free(pool);
	pool = NULL;
	return 0;
}

void threadPoolAdd(ThreadPool* pool, void(*func)(void*), void* arg)
{
	pthread_mutex_lock(&pool->mutexPool);
	//线程池满了，则阻塞
	while (pool->queueSize == pool->queueCapacity && !pool->shutdown)
	{
		pthread_cond_wait(&pool->notFull, &pool->mutexPool);
	}
	//线程池已被关闭
	if (pool->shutdown)
	{
		pthread_mutex_unlock(&pool->mutexPool);
		return;
	}
	//添加任务到任务队列的队尾
	pool->taskQ[pool->queueRear].function = func;
	pool->taskQ[pool->queueRear].arg = arg;
	pool->queueRear = (pool->queueRear + 1) % pool->queueCapacity;
	pool->queueSize++;

	//现在任务队列上有任务了，需要唤醒工作线程
	pthread_cond_signal(&pool->notEmpty);

	pthread_mutex_unlock(&pool->mutexPool);
}

int threadPoolBusyNum(ThreadPool* pool)
{
	pthread_mutex_lock(&pool->mutexBusy);
	int busyNum = pool->busyNum;
	pthread_mutex_unlock(&pool->mutexBusy);
	return busyNum;
}

int threadPoolAliveNum(ThreadPool* pool)
{
	pthread_mutex_lock(&pool->mutexPool);
	int aliveNum = pool->liveNum;
	pthread_mutex_unlock(&pool->mutexPool);
	return aliveNum;
}

//工作线程的函数
void* worker(void* arg)
{
	//1.首先对arg进行类型转换
	ThreadPool* pool = (ThreadPool*)arg;
	//2.循环读取任务队列，而任务队列是共享资源，需要互斥访问
	while (1)
	{
		pthread_mutex_lock(&pool->mutexPool);//mutexPool拿来专门锁整个线程池
		//当前任务个数为0且线程池未被关闭
		while (pool->queueSize == 0 && !pool->shutdown)
		{
			//那么应该阻塞工作线程
			pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);

			//判当前需不需要销毁线程
			if (pool->exitNum > 0)//在管理者发现闲着的线程过多时，会设置pool->exitNum值
			{
				//pthread_cond_wait在wait时自动释放锁，在被唤醒后又自动加锁了
				//为了避免死锁，这里需要在线程自杀前释放锁
				pool->exitNum--;//不管工作线程是否真的自杀，这里都应该--。否则，exitNum始终非0，所有被唤醒的线程都来自杀了
				if (pool->liveNum > pool->minNum)
				{
					pool->liveNum--;//马上线程自杀了，活着的线程数应该-1
					pthread_mutex_unlock(&pool->mutexPool);
					threadExit(pool);//线程自杀
				}
				
			}

		}
		//线程池已经关闭了
		if (pool->shutdown)
		{
			pthread_mutex_unlock(&pool->mutexPool);
			/*pthread_exit的好处：
			子线程使用pthread_exit退出后,内核会自动回收子线程相关的数据结构。
			父线程调用wait()时, 直接获取退出状态不需要进行任何清理操作。
			因此，避免了僵尸进程的产生*/
			threadExit(pool);//其中调用pthread_exit(NULL);
		}

		////////////当前是正常的情况，工作线程进行消费////////////
		//从任务队列的头部取出一个任务
		Task task;
		task.function = pool->taskQ[pool->queueFront].function;
		task.arg = pool->taskQ[pool->queueFront].arg;
		pool->queueFront = (pool->queueFront + 1) % pool->queueCapacity;
		pool->queueSize--;//当前任务个数-1
		//既然已经取出了任务(消费)，那么应该唤醒 因为任务队列满而阻塞的 生产者
		pthread_cond_signal(&pool->notFull);
		pthread_mutex_unlock(&pool->mutexPool);


		//调用任务函数，那么现在又多了一个忙线程，busyNumber应该+1
		printf("thread %ld start working\n", pthread_self());
		pthread_mutex_lock(&pool->mutexBusy);//使用专门锁busyNumber的锁，而不是锁整个线程池的锁，提高效率
		pool->busyNum++;
		pthread_mutex_unlock(&pool->mutexBusy);
		task.function(task.arg);
		free(task.arg);//由于传的是一块堆内存，需要释放
		task.arg = NULL;

		//任务函数执行结束，忙线程-1
		printf("thread %ld end working\n", pthread_self());
		pthread_mutex_lock(&pool->mutexBusy);
		pool->busyNum--;
		pthread_mutex_unlock(&pool->mutexBusy);

	}
	return NULL;
}

//管理者线程：负责调节线程池中的线程个数
void* manager(void* arg)
{
	ThreadPool* pool = (ThreadPool*)arg;

	while (!pool->shutdown)//线程池只要没关闭，管理者就一直检测
	{
		//每隔3s检测1次
		sleep(3);
		//由于要取共享资源，即线程池相关变量的值，需要加锁
		pthread_mutex_lock(&pool->mutexPool);
		int queueSize = pool->queueSize;
		int liveNum = pool->liveNum;
		pthread_mutex_unlock(&pool->mutexPool);

		pthread_mutex_lock(&pool->mutexBusy);
		int busyNum = pool->busyNum;
		pthread_mutex_unlock(&pool->mutexBusy);

		//添加线程：规定，当前任务个数>存活线程数 && 存活线程数<最大线程数，则添加线程
		if (queueSize > liveNum && liveNum < pool->maxNum)//maxNum是永远不会被改变的，因此无需互斥访问
		{
			pthread_mutex_lock(&pool->mutexPool);//因为要操作pool->liveNum

			int counter = 0;
			//从存储线程id的数组中找到空闲位置 来存储新创建的线程id
			for (int i = 0; i < pool->maxNum && counter < NUMBER && pool->liveNum < pool->maxNum; i++)
			{
				if (pool->threadIDs[i] == 0)//该位置的内存还没有存储线程id
				{
					pthread_create(&pool->threadIDs[i], NULL, worker, pool);//创建线程
					counter++;
					pool->liveNum++;
				}

			}

			pthread_mutex_unlock(&pool->mutexPool);
		}

		//销毁线程：规定，忙线程数*2<存活线程数 && 存活线程数>最小线程数
		if (busyNum*2<liveNum && liveNum>pool->minNum)
		{
			pthread_mutex_lock(&pool->mutexPool);
			pool->exitNum = NUMBER;
			pthread_mutex_unlock(&pool->mutexPool);

			//让工作线程自杀：唤醒wait的工作线程，工作线程会去检查pool->exitNum，非0则工作线程自杀
			for (int i = 0; i < NUMBER; i++)
			{
				pthread_cond_signal(&pool->notEmpty);
			}
		}

	}
	return NULL;
}

void threadExit(ThreadPool* pool)
{
	pthread_t tid = pthread_self();//获取当前线程ID
	for (int i = 0; i < pool->maxNum; i++)
	{
		if (pool->threadIDs[i] == tid)
		{
			pool->threadIDs[i] = 0;
			printf("threadExit() called, %ld exiting...\n", tid);
			break;
		}
	}
	pthread_exit(NULL);
}



