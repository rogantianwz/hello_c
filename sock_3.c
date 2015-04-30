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
 *二阶进化版:实现简单的http协议;
 *gcc -g -L/usr/lib/x86_64-linux-gnu -pthread -o ./out/sock_3 sock_3.c 
 *gdb ./out/sock_3 core
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

typedef struct {
	char *method;
	char *uri;
    char *version;	
} request_line;
typedef struct {
	char *name;
	char *value;
} request_header;
void *http_process(void* arg);
int read_req_line_from_client(int fp, char *buf, int size, int *req_line_split_pos);
int read_req_header_from_client(int fp, char* buf_process_begin, int received, int size_to_read, request_header **headers);
int dbg_req_header_buf(request_header *headers, int header_num);


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
/*
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
*/
	char buf[8 * 1024];

	memset(buf, '\0', 8 * 1024);
	int req_line_split_pos[8];//第7个元素存储request_line的最后一个字符的位置;第8个元素存储已经读取到缓存的最后一个字符的位置
	int i = 0;
	for (;i < 8; i++) {
		req_line_split_pos[i] = 0;
	}
	int recv_count = read_req_line_from_client(request->c_info->fp, buf, 8 * 1024, req_line_split_pos);
	//此处应该根据不同的返回状态返回不同的response
	printf("the requset_line_process return :%d\n", recv_count);
	
	char *buf_process_header_begin = buf + req_line_split_pos[6] + 3;//跳过request_line最后的\r\n
	int buf_header_received = req_line_split_pos[7] - req_line_split_pos[6] - 2;//同上
	int size_to_read = 8 * 1024 - recv_count;	

	//printf("After read the request line, tht pos is [%d, %d, %d]\n", recv_count, req_line_split_pos[6], req_line_split_pos[7]);	
	request_header *headers = malloc(sizeof(request_header));
	request_header **headers_ptr;
	headers_ptr = &headers;
	int header_num = read_req_header_from_client(request->c_info->fp, buf_process_header_begin, buf_header_received, size_to_read, headers_ptr);
	free(headers);

	printf("the header_process return :%d\n", header_num);
	if (header_num > 0) {
		dbg_req_header_buf(*headers_ptr, header_num);
	}	
	close(request->c_info->fp);
}

int dbg_req_header_buf(request_header *headers, int header_num) {
	printf("begin print the header===================================\n");
	int i;
	for(i = 0; i < header_num; i++) {
		printf("name:%s\n" , (headers + i)->name);
		printf("value:%s\n" , (headers + i)->value);
	}
	printf("end print the header===================================\n");
}

int read_req_header_from_client(int fp, char* buf_process_begin, int received, int size_to_read, request_header **headers_ptr) {

	printf("I'm in read_req_header_from_client , received is %d, size_to_read is %d", received, size_to_read);
	char *origin_start_buf = buf_process_begin;
	int req_header_split_pos[3 * 50];//这里限制最多有50个header
	int req_header_split_index = 0;
	char last_end_line = 1;

	int header_item_begin = -1, header_item_end = -1, header_item_colon = -1;
	int header_item_has_colon = 0;
	size_to_read += received;

	int last_success_process_count = 0;
	int success_process_header = 0;

	int received_count = received;
	char *buf_read_begin = buf_process_begin + received_count;
	do {
		buf_process_begin = buf_process_begin + last_success_process_count;
		printf("the process address is :%p\n", buf_process_begin);
		int i = 0;
		for(; i < received; i++) {
			printf("I'm in for: %d\n", *(buf_process_begin + i));
			if (*(buf_process_begin + i) == '\n') {
				if (*(buf_process_begin + i -1) == '\r') {
					last_end_line ++;
				} else {
					return -1;//请求错误,必须以\r\n结束一行,??是否忽略这个\n还有待思考,这里暂且认为其不合法
				}
				if (last_end_line == 2) {
					success_process_header = 1;
					break;//连续读到两个\r\n,表明header读取结束
				}
				//一个header结束
				header_item_end = i - 2 + last_success_process_count;
				if (header_item_begin >= 0  && header_item_colon > 0) {
					int j = 0;
					req_header_split_pos[req_header_split_index++] = header_item_begin;
					req_header_split_pos[req_header_split_index++] = header_item_colon;
					req_header_split_pos[req_header_split_index++] = header_item_end;
					header_item_begin = -1;
					header_item_end = -1;
					header_item_colon = -1;
				} else {
					//printf("===the header_split_index is %d , and i is %d\n", req_header_split_index, i);
					return -2;//请求错误, header格式不正确
				}
			} else if (*(buf_process_begin + i) == ':' && header_item_colon == -1) {
				header_item_has_colon = 1;
				header_item_colon = i + last_success_process_count;
			} else if (last_end_line == 1 && *(buf_process_begin + i) != '\r'){
				header_item_begin = i + last_success_process_count;		
				last_end_line = 0;
			}
			printf("I'm in for End, last_end_line is %d\n", last_end_line);
		}	
		printf("I'm out of for, last_end_line is %d\n", last_end_line);
		last_success_process_count = i ;
		if (1 == success_process_header) {
			break;
		}
		printf("the last_success_process_count is %d\n", last_success_process_count);
		size_to_read -= received;
		if (size_to_read <=0 ) {
			return -3;//错误,header超过长度
		}
		received = recv(fp, buf_read_begin, size_to_read, 0);
		if (received <= 0) {
			printf("error receive again\n");
			return -4;
		}
		printf("buf_read_begin is :%p\n", buf_read_begin);
		buf_read_begin = buf_read_begin + received;
	} while(1);

	//printf("dbg: the req_header_split_index is [%d]\n", req_header_split_index);

	if (req_header_split_index <= 0) {
		return 0;
	}
	printf("dbg: the req_header_split_pos [%d,%d,%d,%d]", req_header_split_pos[0], req_header_split_pos[1], req_header_split_pos[2], req_header_split_pos[3]);

	printf("dbg:the req_header_split_pos [");
	int k = 0;
	for (;k<10;k++) {
		printf("%d,",req_header_split_pos[k]);
	}
	printf("]\n");

	int j = 0;
	for (;j<20;j++) {
		printf("%d,", *(origin_start_buf+j));
	}
	printf("\n");
	int i = 0;
	int header_item_num = req_header_split_index / 3;
	request_header *headers = (request_header *)malloc(sizeof(request_header) * header_item_num);
	*headers_ptr = headers;
	for (; i < header_item_num; i++) {
		(headers + i)->name = origin_start_buf + req_header_split_pos[i * 3 + 0];
		*(origin_start_buf + req_header_split_pos[i*3 + 1]) = '\0';
		printf("GOTC:name is->%s\n", (headers + i)->name);
		(headers + i)->value = origin_start_buf + req_header_split_pos[i*3 + 1] + 1;
		*(origin_start_buf + req_header_split_pos[i*3 + 2] + 1) = '\0';
	}

	return header_item_num;
}

void dbg_req_line_buf(char *buf, int *line_pos, char *prefix) {
	printf("%s,pos[", prefix);
	int i = 0;
	for (; i < 8; i++) {
		printf("%d,", line_pos[i]);
	}
	printf("],%s\n", buf);
}

int read_req_line_from_client(int fp, char *buf, int size, int *req_line_split_pos) {
	//int req_line_split_pos[6];
	int req_line_split_index = 0;
	int last_success_pos = 0;
	int received = 0;
	int recv_count = 0;
	int ends_with_new_line = 0;
	do {
		received = recv(fp, buf + last_success_pos, size-last_success_pos, 0);
		if (received <= 0) {
			printf("error receive \n");
			return -1;
		}
		recv_count += received;	

		char *tmp = buf + last_success_pos;
		int i = 0;
		char pre_is_space = 1;
		for (; i < received  && req_line_split_index < 6; i++) {
			if (*(tmp+i) == 32 && 0 == pre_is_space) {
				req_line_split_pos[req_line_split_index++] = i - 1 + last_success_pos;	
				pre_is_space = 1;
			} else if (*(tmp + i) == '\n') {
				if (i < 1) {
					break;
				}
				if (*(tmp + i - 1) == '\r' && i > 1) {
					req_line_split_pos[req_line_split_index++] = i - 2 + last_success_pos;	
				} else {
					req_line_split_pos[req_line_split_index++] = i - 1 + last_success_pos;	
				}
				ends_with_new_line = 1;
				break;
			} else if (1 == pre_is_space) {
				pre_is_space = 0;
				req_line_split_pos[req_line_split_index++] = i + last_success_pos;	
			} else {
				pre_is_space = 0;
			}
		}
		if (req_line_split_index > 0) {
			last_success_pos = req_line_split_pos[req_line_split_index - 1];
		}
	} while(0);
	
	if (ends_with_new_line != 1) {
		return -1;//\r\n的位置不对
	} else {
		req_line_split_pos[6] = last_success_pos;
	}
	req_line_split_pos[7] = recv_count - 1;
	dbg_req_line_buf(buf, req_line_split_pos, "dbg the buf:");
	return recv_count;
}

int request_error_process() {
	return 0;	
}

//========================================HTTP相关实现END==========================
