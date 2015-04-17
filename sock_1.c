#include<sys/socket.h>
#include<stdio.h>
#include<string.h>
#include<netinet/in.h>
#include<pthread.h>

/**
 *
 * 最简单的实现:监听端口,每当有连接过来的时候新建一个线程处理这个连接的数据读写
 */
struct client_info {
	int fp;
	struct sockaddr_in addr;
};

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

	pthread_t thread = pthread_self();
	//这里的ip和port都没有使用ntohl和ntohs函数转换,在测试的时候可以看到其序列是反的,说明我的host使用的是小端序(因为网络序是大端序),其实使用x86的处理器的情况下都是小端序,即低地址存放低位数据, [参考](http://www.cnblogs.com/jacktu/archive/2008/11/24/1339789.html)
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


