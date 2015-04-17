#include<sys/socket.h>
#include<stdio.h>
#include<string.h>
#include<netinet/in.h>
#include<pthread.h>
#include<stdlib.h>

/**
 *
 *一阶进化版:使用线程池,避免每个连接过来都需要新建线程 
 */
struct client_info {
	int fp;
	struct sockaddr_in addr;
};

//==================线程池相关的声明BEGIN===========
typedef struct {//定义通用的任务
 void *(*function)(void *);
 void *arg;
} thread_task_t;

typedef struct { //定义队列
	thread_task_t *task;
	int length;
	pthread_mutex_t lock;
	pthread_cond_t queue_not_empty;
	pthread_cond_t queue_not_full;
	int max_wait_task;//队列中最大等待任务数,超过这个数值会启动更多的线程
} task_queue_t;

enum reject_policy {a, b};//入队失败策略

typedef struct { //定义线程池
	int core_thread_count;
	int max_thread_count;	
	pthread_t *threads;
	task_queue_t* queue;	
    enum reject_policy policy;	//enum的用法很怪异
} thread_pool_t;

thread_pool_t *threadpool_create(int core, int max, int queue_len, int max_wait_task);
int threadpool_clean(thread_pool_t *pool);

//==================线程池相关的声明END=============

int main() {

	struct sockaddr_in s_add, c_add;
	int sfp, cfp;
	unsigned short portnum = 0x8888;

	pthread_t thread = pthread_self();
	printf("the main thread is %lu \n", thread);

	sfp = socket(AF_INET, SOCK_STREAM, 0);
	if (-1 == sfp) {
		printf("socket create error\n");
		return -1;
	}

	bzero(&s_add, sizeof(struct sockaddr_in));
	s_add.sin_family = AF_INET;
	//define ((in_addr_t) 0x00000000) INADDR_ANY
	//typedef uint32_t in_addr_t;
	//typedef unsigned int __uint32_t;
	s_add.sin_addr.s_addr = htonl(INADDR_ANY);
	s_add.sin_port = htons(portnum);

	if (-1 == bind(sfp, (struct sockaddr *)(&s_add), sizeof(struct sockaddr))) {
		printf("bind error\n");
		return -1;
	}

	if (-1 == listen(sfp, 10)) {
		printf("listen error\n");
		return -1;
	}

	thread_pool_t *pool;
	if (NULL == (pool = threadpool_create(5, 10, 20))) {
		printf("thread pool create fail\n");
		return -1;
	}
	

	while(1) {
		unsigned int sin_size = sizeof(struct sockaddr_in);
		//开始我想在accept函数中直接使用&sizeof(struct sockaddr_in)来作为第三个参数,后来一直报错,因为sizeof(...)的结果是一个值,不能对一个值取地址,so...
		cfp = accept(sfp, (struct sockaddr *)(&c_add), &sin_size);
		if (-1 == cfp) {
			printf("accept error\n");
			continue;
		}

		//x:以十六进制的形式输出无符号整数(不输出前缀0x)
		//u:以十进制的形式输出无符号整数
		//#对x类,在输出时加前缀0x
		printf("accept one from %#x:%u\n", ntohl(c_add.sin_addr.s_addr), ntohs(c_add.sin_port));

		if ( 0 != create_process_thread(cfp, c_add)) {
			printf ("create process thread error\n");
			continue;
		}

	}
	close(cfp);
	return 0;
}

void  *client_process(void *arg) {
	//开始我给这个函数定义的参数是struct client_info *c_info ,结果编译不通过,就改成这种形式了,实参和形参都要void*
	struct client_info *c_info = (struct client_info *) arg;

	pthread_t thread = pthread_self(); //这里的ip和port都没有使用ntohl和ntohs函数转换,在测试的时候可以看到其序列是反的,说明我的host使用的是小端序(因为网络序是大端序),其实使用x86的处理器的情况下都是小端序,即低地址存放低位数据, [参考](http://www.cnblogs.com/jacktu/archive/2008/11/24/1339789.html)
	printf("thread[%lu] is processing the request form %#x:%u\n", thread, c_info->addr.sin_addr.s_addr, c_info->addr.sin_port);

	char echo[] = "hello,just test\n";

	if (-1 == write(c_info->fp, echo, strlen(echo))) {
		printf("thread[%lu] write error\n", thread);
	}

	printf("thread[%lu] exit\n", thread);
	close(c_info->fp);
}

int create_process_thread(int fp, struct sockaddr_in c_add) {
	struct client_info c_info; 
	bzero(&c_info, sizeof(struct client_info));
	c_info.fp = fp;
	memcpy(&c_info.addr, &c_add, sizeof(struct sockaddr_in));

	//typedef unsigned long int pthread_t; file:sysdeps/x86/bits/pthreadtypes.h
	pthread_t thread;

	//pthread_create defined in sysdeps/nptl/pthread.h
	if (0 != pthread_create(&thread, NULL, client_process, (void*)&c_info)) {
		return -1;
	}
	return 0;
}		

//========================================线程池相关实现======================

void *thread_process(void *arg);//线程运行处

thread_pool_t *threadpool_create(int core, int max, int queue_len, int max_wait_task) {
	thread_pool_t *pool = NULL;
	do {
		//使用这个malloc需要include<stdlib.h>,否则其匹配的是int malloc(),就会报warning:incompatible implicit declaration of built-in 'malloc'.....
		if (NULL == (pool = (thread_pool_t *)malloc(sizeof(thread_pool_t)))) {
			printf("init threadpool fail\n");
			break;
		}
		pool->core_thread_count = core;
		pool->max_thread_count = max;

		pthread_t * threads = NULL;
		if (NULL == (threads = (pthread_t*)malloc(sizeof(pthread_t) * core))) {
			printf("malloc core threads fail\n");
			break;
		}
		pool->threads = threads;
		
		task_queue_t *queue = NULL;
		if (NULL == (queue = (task_queue_t*)malloc(sizeof(task_queue_t)))) {
			printf("init task queue fail\n");
			break;
		}
		queue->length = queue_len;
		queue->max_wait_task = max_wait_task;	

		thread_task_t *task = NULL;
		if (NULL == (task = (thread_task_t*)malloc(sizeof(thread_task_t) * queue->length))) {
			printf("malloc task list fail\n");
			break;
		}
		queue->task = task;

		pool->queue = queue;

		if (pthread_mutex_init(&queue->lock, NULL) != 0) {
			printf("init queue's mutex lock fail\n");
			break;
		}

		if (pthread_cond_init(&pool->queue->queue_not_empty, NULL) != 0) {
			printf ("init queue_not_empty fail \n");
			break;
		}

		if (pthread_cond_init(&pool->queue->queue_not_full, NULL) != 0) {
			printf ("init queue_not_full fail \n");
			break;
		}

		//c11的for循环中不允许进行声明,到c99才可,这里把声明提出来了
		int i = 0;
		for (; i < core; i++) {
			if (0 != pthread_create(pool->threads + i, NULL, thread_process, NULL)) {
				printf("create core thread fail\n");
				break;
			}
			printf("core thread[%lu] started\n", *(pool->threads + i));
		}
		
		return pool;	
	} while(0);
	return NULL;
}

int threadpool_clean(thread_pool_t *pool) {
	if (NULL == pool) {
		return -1;
	}	
	if (pool->queue) {
		if (queue->task) {
			free(pool->queue->task);
		}
		free(pool->queue);
	}
	if (pool->threads) {
		free(pool->threads);	
	}
	//TODO: free the locks and conds
	free(pool);
	return 0;
}

void *thread_process(void *arg) {
	pthread_t thread = pthread_self();
	printf("thread[%lu] is running\n", thread);
}



