#include <stdio.h>
#include "threadpool.h"
#include <pthread.h>//pthread_self
#include <unistd.h>//sleep
#include <stdlib.h>//malloc
/*
有多个工作线程、1个管理者线程。管理者线程根据当前线程池中存活的线程数量与忙线程数量来动态的调整线程数量。
设计一个线程池结构体，用于存储线程所需的各种信息，也存储了一个环形任务队列，
这个任务队列的每个节点是一个Task结构体，包含了所要执行的任务的函数指针与函数参数。
对于线程池的各种操作，单独的写了一些函数用于创建线程和销毁线程。
为了管理者线程能够销毁空闲的工作线程(被阻塞的线程)，定义了一个变量exitNum，当阻塞的工作线程被唤醒发现exitNum非0就自杀。
其中实现线程同步使用了2个锁，一个锁用于锁线程池，另一个用于锁频繁改变的变量busyNum。
还使用了2个条件变量，一个用于在任务队列满时阻塞生产线程，另一个用于任务队列空时阻塞消费线程。
这里生产者就是主线程不断调用threadPoolAdd添加任务，消费者就是工作线程不断处理任务(执行传过来的任务函数)
*/
//工作函数
void taskFunc(void* arg)
{
    int num = *(int*)arg;
    printf("thread %ld is working, number = %d\n", pthread_self(), num);
    sleep(1);
}


int main()
{
    //创建线程池
    ThreadPool* pool = threadPoolCreate(3, 10, 100);
    //添加任务
    for (int i = 0; i < 100; i++)
    {
        int* num = (int*)malloc(sizeof(int));
        *num = i + 100;
        threadPoolAdd(pool, taskFunc, num);
    }

    sleep(30);

    //销毁线程池
    thradPoolDestory(pool);
    return 0;
}