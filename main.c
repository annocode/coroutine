#include "coroutine.h"
#include <stdio.h>

struct args 
{
	int n;
};

static void foo(struct schedule * S, void *ud) 
{
	struct args * arg = ud;
	int start = arg->n;
	int i;

    for (i = 0; i < 5; i++) 
    {
		printf("coroutine %d : %d\n",coroutine_running(S) , start + i);
		coroutine_yield(S); // 切出当前协程
	}
}

static void test(struct schedule *S) 
{
	struct args arg1 = { 0 };
	struct args arg2 = { 100 };

	int co1 = coroutine_new(S, foo, &arg1); // 创建协程
	int co2 = coroutine_new(S, foo, &arg2); // 创建协程
	printf("main start\n");

    while (coroutine_status(S,co1) && coroutine_status(S,co2)) 
    {
		coroutine_resume(S, co1);   // 恢复协程1的运行
		coroutine_resume(S, co2);   // 恢复协程2的运行
	} 

    printf("main end\n");
}

int main() 
{
	struct schedule * S = coroutine_open(); // 创建一个调度器，用来管理所有子协程
	
    test(S);

    coroutine_close(S); // 关闭调度器

	return 0;
}
