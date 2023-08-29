#ifndef _THREADPOOL_H
#define _THREADPOOL_H
//struct ThreadPool;��������һ��������ṹ�壬�����ڱ�ĵط���(��ʵҲ����ֱ�Ӷ��������ͷ�ļ���)
//ʹ��typedef�����˸�����ThreadPool
typedef struct ThreadPool ThreadPool;
//�����̳߳ز���ʼ��
ThreadPool *threadPoolCreate(int min, int max, int queueSize);
//�����̳߳�
int thradPoolDestory(ThreadPool* pool);

//��������̳߳�
void threadPoolAdd(ThreadPool* pool, void(*func)(void*), void* arg);

//��õ�ǰ���ڹ������߳���
int threadPoolBusyNum(ThreadPool* pool);

//���ŵ��߳���
int threadPoolAliveNum(ThreadPool* pool);


void* worker(void* arg);//�����̵߳ĺ���
void* manager(void* arg);//�������̵߳ĺ���
void threadExit(ThreadPool* pool);//�����̣߳�������threadIDs��Ӧλ�õ�ֵΪ0�����ڴ洢�������������߳�id

#endif // !_THREADPOOL_H

