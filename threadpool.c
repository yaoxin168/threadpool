#include "threadpool.h"
#include <pthread.h>
#include <stdlib.h>//malloc
#include <stdio.h>//printf
#include <string.h>//memset
#include <unistd.h>//sleep

const int NUMBER = 2;//�����̳߳��ڴ����߳�����ʱ��һ�������������߳�

//����ṹ��
typedef struct Task
{
	void (*function)(void* arg);//����ָ�룬����ֵΪvoid
	void* arg;//�����Ĳ���
}Task;

//�̳߳ؽṹ��
struct ThreadPool
{
	Task* taskQ;//������У��ɶ�ͷ��β�±�ά����һ�����ζ���
	int queueCapacity;//������е�����
	int queueSize;//��ǰ�������
	int queueFront;//��ͷ�±�
	int queueRear;//��β�±�

	pthread_t managerID;//�������߳�ID���������߳�ֻ��һ��
	pthread_t *threadIDs;//�������߳�ID���ж�������߳�
	int minNum;//��С�߳���
	int maxNum;//����߳���
	int busyNum;//��ǰ�ڹ������̸߳���
	int liveNum;//��ǰ�����̸߳���
	int exitNum;//����󲿷��̶߳����ţ���Ҫɱ�����ٸ��߳�

	pthread_mutex_t mutexPool;//���������������̳߳�
	pthread_mutex_t mutexBusy;//����������busyNum����ΪbusyNum�仯��Ƶ�������ÿ�ζ�ֱ���������̳߳صĲ�̫��

	pthread_cond_t notFull;//����������������в���ʱ�����������߳�
	pthread_cond_t notEmpty;//������в�Ϊ��ʱ���ѹ����߳�(������)

	int shutdown;//Ҫ��Ҫ�����̳߳أ�����Ϊ1��������Ϊ0
};

//�̳߳س�ʼ��
ThreadPool* threadPoolCreate(int min, int max, int queueSize)
{
	ThreadPool* pool = (ThreadPool*)malloc(sizeof(ThreadPool));
	do
	{
		if (pool == NULL)//�ڴ����ʧ��
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

		memset(pool->threadIDs, 0, sizeof(pthread_t) * max);//����Ƭ�ڴ��ʼ��Ϊ0����ʾ��λ��δ�洢���߳�id���ǿ��ŵ�
		pool->minNum = min;
		pool->maxNum = max;
		pool->busyNum = 0;
		pool->liveNum = min;//�տ�ʼ�Ĵ���߳��� �� ��С�̸߳������
		pool->exitNum = 0;

		//ʹ�ö�̬������������������������
		if (pthread_mutex_init(&pool->mutexPool, NULL) != 0 ||
			pthread_mutex_init(&pool->mutexBusy, NULL) != 0 ||
			pthread_cond_init(&pool->notEmpty, NULL) != 0 ||
			pthread_cond_init(&pool->notFull, NULL) != 0)
		{
			printf("mutex or condition fail\n");
		}

		//�������
		pool->taskQ = (Task *)malloc(sizeof(Task) * queueSize);
		pool->queueCapacity = queueSize;
		pool->queueSize = 0;
		pool->queueFront = 0;
		pool->queueRear = 0;
		pool->shutdown = 0;

		//�����߳�
		pthread_create(&pool->managerID, NULL, manager, pool);//�������߳�
		//�����߳�
		for (int i = 0; i < min; i++)
		{//�����̵߳ĺ���Ӧ�ô���pool����ʱ�����ֱ��ȡ��pool������������taskQ��һЩ��
			pthread_create(&pool->threadIDs[i], NULL, worker, pool);
		}

		return pool;


	} while (0);

	//��ʼ��ʧ�ܣ���Ҫ�ͷ���Դ
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
	//�ر��̳߳�
	pool->shutdown = 1;
	//�����ȴ�ָ���߳�(�������߳�)�����ż���ִ��
	pthread_join(pool->managerID, NULL);
	//���������������������߳�(�����߳�)�����ǵĴ��봦���ж��̳߳��Ƿ��ѹرգ�����ѹرգ�����ɱ 
	for (int i = 0; i < pool->liveNum; i++)
	{
		pthread_cond_signal(&pool->notEmpty);
	}

	//�ͷŶ��ڴ�
	if (pool->taskQ) free(pool->taskQ);
	if (pool->threadIDs) free(pool->threadIDs);
	
	//�����л�����������������Դ�ͷŵ�
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
	//�̳߳����ˣ�������
	while (pool->queueSize == pool->queueCapacity && !pool->shutdown)
	{
		pthread_cond_wait(&pool->notFull, &pool->mutexPool);
	}
	//�̳߳��ѱ��ر�
	if (pool->shutdown)
	{
		pthread_mutex_unlock(&pool->mutexPool);
		return;
	}
	//�������������еĶ�β
	pool->taskQ[pool->queueRear].function = func;
	pool->taskQ[pool->queueRear].arg = arg;
	pool->queueRear = (pool->queueRear + 1) % pool->queueCapacity;
	pool->queueSize++;

	//��������������������ˣ���Ҫ���ѹ����߳�
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

//�����̵߳ĺ���
void* worker(void* arg)
{
	//1.���ȶ�arg��������ת��
	ThreadPool* pool = (ThreadPool*)arg;
	//2.ѭ����ȡ������У�����������ǹ�����Դ����Ҫ�������
	while (1)
	{
		pthread_mutex_lock(&pool->mutexPool);//mutexPool����ר���������̳߳�
		//��ǰ�������Ϊ0���̳߳�δ���ر�
		while (pool->queueSize == 0 && !pool->shutdown)
		{
			//��ôӦ�����������߳�
			pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);

			//�е�ǰ�費��Ҫ�����߳�
			if (pool->exitNum > 0)//�ڹ����߷������ŵ��̹߳���ʱ��������pool->exitNumֵ
			{
				//pthread_cond_wait��waitʱ�Զ��ͷ������ڱ����Ѻ����Զ�������
				//Ϊ�˱���������������Ҫ���߳���ɱǰ�ͷ���
				pool->exitNum--;//���ܹ����߳��Ƿ������ɱ�����ﶼӦ��--������exitNumʼ�շ�0�����б����ѵ��̶߳�����ɱ��
				if (pool->liveNum > pool->minNum)
				{
					pool->liveNum--;//�����߳���ɱ�ˣ����ŵ��߳���Ӧ��-1
					pthread_mutex_unlock(&pool->mutexPool);
					threadExit(pool);//�߳���ɱ
				}
				
			}

		}
		//�̳߳��Ѿ��ر���
		if (pool->shutdown)
		{
			pthread_mutex_unlock(&pool->mutexPool);
			/*pthread_exit�ĺô���
			���߳�ʹ��pthread_exit�˳���,�ں˻��Զ��������߳���ص����ݽṹ��
			���̵߳���wait()ʱ, ֱ�ӻ�ȡ�˳�״̬����Ҫ�����κ����������
			��ˣ������˽�ʬ���̵Ĳ���*/
			threadExit(pool);//���е���pthread_exit(NULL);
		}

		////////////��ǰ������������������߳̽�������////////////
		//��������е�ͷ��ȡ��һ������
		Task task;
		task.function = pool->taskQ[pool->queueFront].function;
		task.arg = pool->taskQ[pool->queueFront].arg;
		pool->queueFront = (pool->queueFront + 1) % pool->queueCapacity;
		pool->queueSize--;//��ǰ�������-1
		//��Ȼ�Ѿ�ȡ��������(����)����ôӦ�û��� ��Ϊ����������������� ������
		pthread_cond_signal(&pool->notFull);
		pthread_mutex_unlock(&pool->mutexPool);


		//��������������ô�����ֶ���һ��æ�̣߳�busyNumberӦ��+1
		printf("thread %ld start working\n", pthread_self());
		pthread_mutex_lock(&pool->mutexBusy);//ʹ��ר����busyNumber�������������������̳߳ص��������Ч��
		pool->busyNum++;
		pthread_mutex_unlock(&pool->mutexBusy);
		task.function(task.arg);
		free(task.arg);//���ڴ�����һ����ڴ棬��Ҫ�ͷ�
		task.arg = NULL;

		//������ִ�н�����æ�߳�-1
		printf("thread %ld end working\n", pthread_self());
		pthread_mutex_lock(&pool->mutexBusy);
		pool->busyNum--;
		pthread_mutex_unlock(&pool->mutexBusy);

	}
	return NULL;
}

//�������̣߳���������̳߳��е��̸߳���
void* manager(void* arg)
{
	ThreadPool* pool = (ThreadPool*)arg;

	while (!pool->shutdown)//�̳߳�ֻҪû�رգ������߾�һֱ���
	{
		//ÿ��3s���1��
		sleep(3);
		//����Ҫȡ������Դ�����̳߳���ر�����ֵ����Ҫ����
		pthread_mutex_lock(&pool->mutexPool);
		int queueSize = pool->queueSize;
		int liveNum = pool->liveNum;
		pthread_mutex_unlock(&pool->mutexPool);

		pthread_mutex_lock(&pool->mutexBusy);
		int busyNum = pool->busyNum;
		pthread_mutex_unlock(&pool->mutexBusy);

		//����̣߳��涨����ǰ�������>����߳��� && ����߳���<����߳�����������߳�
		if (queueSize > liveNum && liveNum < pool->maxNum)//maxNum����Զ���ᱻ�ı�ģ�������軥�����
		{
			pthread_mutex_lock(&pool->mutexPool);//��ΪҪ����pool->liveNum

			int counter = 0;
			//�Ӵ洢�߳�id���������ҵ�����λ�� ���洢�´������߳�id
			for (int i = 0; i < pool->maxNum && counter < NUMBER && pool->liveNum < pool->maxNum; i++)
			{
				if (pool->threadIDs[i] == 0)//��λ�õ��ڴ滹û�д洢�߳�id
				{
					pthread_create(&pool->threadIDs[i], NULL, worker, pool);//�����߳�
					counter++;
					pool->liveNum++;
				}

			}

			pthread_mutex_unlock(&pool->mutexPool);
		}

		//�����̣߳��涨��æ�߳���*2<����߳��� && ����߳���>��С�߳���
		if (busyNum*2<liveNum && liveNum>pool->minNum)
		{
			pthread_mutex_lock(&pool->mutexPool);
			pool->exitNum = NUMBER;
			pthread_mutex_unlock(&pool->mutexPool);

			//�ù����߳���ɱ������wait�Ĺ����̣߳������̻߳�ȥ���pool->exitNum����0�����߳���ɱ
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
	pthread_t tid = pthread_self();//��ȡ��ǰ�߳�ID
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



