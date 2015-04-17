#include<stdio.h>

void main() {
	printf("please input the num> ");
	unsigned int n,j;
	scanf("%u", &n);
	for(j = 0; n >= 1 && (j += n--);) 
		;
	printf("the sum from 1 to %u is %d \n", n, j);
}
