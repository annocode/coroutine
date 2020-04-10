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
		coroutine_yield(S); // �г���ǰЭ��
	}
}

static void test(struct schedule *S) 
{
	struct args arg1 = { 0 };
	struct args arg2 = { 100 };

	int co1 = coroutine_new(S, foo, &arg1); // ����Э��
	int co2 = coroutine_new(S, foo, &arg2); // ����Э��
	printf("main start\n");

    while (coroutine_status(S,co1) && coroutine_status(S,co2)) 
    {
		coroutine_resume(S, co1);   // �ָ�Э��1������
		coroutine_resume(S, co2);   // �ָ�Э��2������
	} 

    printf("main end\n");
}

int main() 
{
	struct schedule * S = coroutine_open(); // ����һ������������������������Э��
	
    test(S);

    coroutine_close(S); // �رյ�����

	return 0;
}
