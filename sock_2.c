#include<sys/socket.h>
#include<stdio.h>
#include<string.h>
#include<netinet/in.h>
#include<pthread.h>
#include<stdlib.h>
#include<assert.h>
#include<errno.h>//开始我想使用extern int errno;结果连接报错所以使用这种方式

/**
 *
 *一阶进化版:使用线程池,避免每个连接过来都需要新建线程,该版本的线程池尚未实现线程池的监控,包括自动增加和减少线程,线程监控等
 */
struct client_info {
	int fp;
	struct sockaddr_in addr;
};

//==================线程池相关的声明BEGIN===========
typedef struct {//定义通用的任务
 void *(*function)(void *);
 void *arg;
 int arg_size;//参数的长度, it's important
} thread_task_t;

typedef struct { //定义队列
	thread_task_t *task;
	int size;
	int max_size;
	pthread_mutex_t lock;
	pthread_cond_t queue_not_empty;
	pthread_cond_t queue_not_full;
	int max_wait_task;//队列中最大等待任务数,超过这个数值会启动更多的线程
	int top;
	int rear;
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
int task_add(thread_pool_t *pool, void* (*function)(void*), void *arg, int arg_size);
void *thread_process(void *arg);
void queue_dbg(task_queue_t *queue, char *prefix);

//==================线程池相关的声明END=============

//==================HTTP相关的声明BEGIN=============
typedef struct {
	struct client_info *c_info;	
} request_t;

void *http_process(void* arg);

//==================HTTP相关的声明END===============

int main() {

	struct sockaddr_in s_add, c_add;
	int sfp, cfp;
	unsigned short portnum = 0x8887;

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
	if (NULL == (pool = threadpool_create(5, 10, 20, 10))) {
		printf("thread pool create fail\n");
		return -1;
	}
	
	struct client_info *c_info = NULL;//开始我是在while循环体内声明的变量struct client_info c_info,后来发现后面的循环会覆盖前面的这个变量的值.经过分析得出结论:使用struct client_info c_info 这种方式声明的变量由编译器自动分配栈空间,每次循环结束后,在上次循环中分配的栈空间就会释放,然后进入下次循环的时候又会在原来栈的地址重新分配该变量的内存空间,这时分配的内存空间与上次循环中的的内存空间的地址相同,并且在后面的循环中对该变量赋值的时候就会覆盖之前对该变量的引用的值.
	//把c_info 的声明改为指针形式使用malloc分配空间,就算后面的循环会覆盖c_info也没关系了.
	unsigned int sin_size = sizeof(struct sockaddr_in);
	while(1) {
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

		/*
		if ( 0 != create_process_thread(cfp, c_add)) {
			printf ("create process thread error\n");
			continue;
		}
*/
		c_info = malloc(sizeof(struct client_info)); 
		bzero(c_info, sizeof(struct client_info));
		c_info->fp = cfp;
		memcpy(&c_info->addr, &c_add, sizeof(struct sockaddr_in));

		request_t *request = (request_t *)malloc(sizeof(request_t));
		request->c_info = c_info;
		//prinft打印指针的值(即地址)的时候可以使用%p
		//printf("before add: client_info's address[%p]\n", c_info);
		task_add(pool, http_process, (void *)request, sizeof(request_t));

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

//========================================线程池相关实现BEGIN======================

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
		queue->max_size = queue_len;
		queue->size = 0;
		queue->max_wait_task = max_wait_task;	
		queue->top = queue->rear = 0;

		thread_task_t *task = NULL;
		if (NULL == (task = (thread_task_t*)malloc(sizeof(thread_task_t) * queue->max_size))) {
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
			if (0 != pthread_create(pool->threads + i, NULL, thread_process, (void*)pool)) {
				printf("create core thread fail\n");
				break;
			}
			printf("core thread[%lu] started\n", *(pool->threads + i));
		}
		
		return pool;	
	} while(0);
	return NULL;
}

int task_add(thread_pool_t *pool, void* (*function)(void*), void *arg, int arg_size) {
	assert(NULL != pool);
	task_queue_t *queue = pool -> queue;
	assert(NULL != queue);

	pthread_mutex_lock(&(queue->lock));
	while (queue->size == queue->max_size) {
		pthread_cond_wait(&(queue->queue_not_full), &(queue->lock));
	}
	int rear = queue->rear;
	(queue->task+rear)->function = function; 
	(queue->task+rear)->arg = arg; 
	(queue->task+rear)->arg_size = arg_size;
	queue->rear = (rear + 1) % queue->max_size;
	queue->size += 1;
	pthread_cond_signal(&(queue->queue_not_empty));
	pthread_mutex_unlock(&(queue->lock));
	return 0;
}

int threadpool_clean(thread_pool_t *pool) {
	if (NULL == pool) {
		return -1;
	}	
	if (pool->queue) {
		if (pool->queue->task) {
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

void queue_dbg(task_queue_t *queue, char *prefix) {
	printf("%s:top is %d, rear is %d\n", prefix, queue->top, queue->rear);
	int i = 0;
	for(;i < queue->rear; i++) {
		thread_task_t * task_tmp = (thread_task_t *)(queue->task+i);
		request_t *request = (request_t *)(task_tmp->arg);
		printf("%s:location[%d]'s fd is %d\n", prefix, i, request->c_info->fp);
	}
}

void *thread_process(void *arg) {//开始我定义成void *thread_process(thread_pool_t *pool)结果gcc报warning,其expect参数为void*, 所以这里把参数改成void*, 然后再函数里做强制转换
	thread_pool_t *pool = (thread_pool_t *) arg;
	pthread_t thread = pthread_self();
	printf("thread[%lu] is running\n", thread);

	task_queue_t *queue = pool->queue;
	thread_task_t *task = NULL; 
	while(1) {
		pthread_mutex_lock(&(queue->lock));
		while (0 == queue->size) {
			printf("thread[%lu] is waiting task\n", thread);
			pthread_cond_wait(&(queue->queue_not_empty), &(queue->lock));
		}
	
		int top = queue->top;
		task = queue->task+top;
		if (task->arg_size > 0) {
			void *arg = malloc(task->arg_size);//这里把队列中的该任务的arg复制一份出来,防止队列中这个位置被二次插入任务时arg被覆盖导致的错误以及内存未释放等问题
			memcpy(arg, task->arg, task->arg_size);
			free(task->arg);
			task->arg = arg;
		}
		queue->top = (top + 1) % queue->max_size;
		queue->size -= 1;
		pthread_cond_broadcast(&(queue->queue_not_full));//这里不知道有多少线程在等待not_full,所以用broadcast,让它们自己去竞争

		pthread_mutex_unlock(&(queue->lock));
		(*task->function)(task->arg);
	}	
	pthread_exit(NULL);
}

//========================================线程池相关实现END========================


//========================================HTTP相关实现BEGIN========================

void *http_process(void* arg) {
	pthread_t thread = pthread_self();
	request_t *request = (request_t *) arg;
	printf("thread[%lu] is processing task with sock[%d]\n", thread, request->c_info->fp);
	char echo[] = "hello,please input the command\n";
	char buf[10];

	do {
		if (-1 == write(request->c_info->fp, echo, strlen(echo))) {
			printf("thread[%lu] write error:%s\n", pthread_self(),strerror(errno));
			break;
		}

		//TODO: 研究以下read读取回车符的问题
		bzero(buf, sizeof(buf));
		if(-1 == read(request->c_info->fp, buf, sizeof(buf))) {
			break;
		} 
		printf("thread[%lu] get command:%s", pthread_self(), buf);
	} while(strcmp(buf,"quit\r\n") != 0); 

	printf("task with sock[%d] is end\n", request->c_info->fp);
	close(request->c_info->fp);
}

//========================================HTTP相关实现END==========================
