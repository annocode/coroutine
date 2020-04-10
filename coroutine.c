#include "coroutine.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdint.h>

#if __APPLE__ && __MACH__
	#include <sys/ucontext.h>
#else 
	#include <ucontext.h>
#endif 

#define STACK_SIZE        (1024*1024)   // Ĭ�ϵ�Э������ʱջ��С
#define DEFAULT_COROUTINE 16            // ������Ĭ�ϵĳ�ʼЭ��������С

struct coroutine;

// �������ṹ��
struct schedule 
{
	char        stack[STACK_SIZE];  // Э������ʱջ��������Э�̹���
	ucontext_t  main;               // ��Э�̵�������
	int         nco;                // ��ǰ����Э������
	int         cap;                // �������е�Э��������������������ڿ��Ը�����Ҫ�������ݡ�
	int         running;            // ��ǰ�������е�Э��ID
	struct coroutine **co;          // �������е�Э������
};

// Э�̽ṹ��
struct coroutine 
{
	coroutine_func    func;         // Э�������еĺ���
	void            * ud;           // Э�̲���
	ucontext_t        ctx;          // ��ǰЭ�̵�������
	struct schedule * sch;          // ��ǰЭ�������ĵ�����
	ptrdiff_t         cap;          // ��ǰջ������������
	ptrdiff_t         size;         // ��ǰջ����Ĵ�С
	int               status;       // ��ǰЭ�̵�����״̬������COROUTINE_{DEAD,READY,RUNNING,SUSPEND}������״̬��һ��
	char            * stack;        // ��ǰЭ���г�ʱ��������������ʱջ
};

struct coroutine * _co_new(struct schedule *S , coroutine_func func, void *ud) 
{
	struct coroutine * co = malloc(sizeof(*co));
	co->func   = func;
	co->ud     = ud;
	co->sch    = S;
	co->cap    = 0;
	co->size   = 0;
	co->status = COROUTINE_READY;
	co->stack  = NULL;
	return co;
}

void _co_delete(struct coroutine *co) 
{
	free(co->stack);
	free(co);
}

struct schedule * coroutine_open(void) 
{
	struct schedule *S = malloc(sizeof(*S));
	S->nco      = 0;                    // ��ǰ����Э������Ϊ0
	S->cap      = DEFAULT_COROUTINE;    // ��ʼЭ������
	S->running  = -1;                   // ���õ�ǰ�������е�Э��
	S->co       = malloc(sizeof(struct coroutine *) * S->cap); // ����Э������
	memset(S->co, 0, sizeof(struct coroutine *) * S->cap);
	return S;
}

void coroutine_close(struct schedule *S) 
{
	int i;
	
    for (i=0;i<S->cap;i++) 
    {
		struct coroutine * co = S->co[i];
		if (co) 
        {
			_co_delete(co);
		}
	}

	free(S->co);
	S->co = NULL;
	free(S);
}

int coroutine_new(struct schedule *S, coroutine_func func, void *ud) 
{
	struct coroutine *co = _co_new(S, func , ud);

    if (S->nco >= S->cap) 
    {
		int id = S->cap;
		S->co = realloc(S->co, S->cap * 2 * sizeof(struct coroutine *));
		memset(S->co + S->cap , 0 , sizeof(struct coroutine *) * S->cap);
		S->co[S->cap] = co;
		S->cap *= 2;
		++S->nco;
		return id;
	} 
    else 
    {
		int i;
		for (i = 0; i < S->cap; i++) 
        {
			int id = (i+S->nco) % S->cap;
			if (S->co[id] == NULL) 
            {
				S->co[id] = co;
				++S->nco;
				return id;
			}
		}
	}

	assert(0);
	return -1;
}

// �ú�����Э����ڵİ�������
// Э�̿�ʼ����ʱ�����иú�����Ȼ��������ͨ������C->func������Э����ʵ�ĺ�����
static void mainfunc(uint32_t low32, uint32_t hi32) 
{
	uintptr_t ptr = (uintptr_t)low32 | ((uintptr_t)hi32 << 32); // ���ݴ���Ĳ������ع���������ָ��
	struct schedule *S = (struct schedule *)ptr;
	int id = S->running;
	struct coroutine *C = S->co[id];

	C->func(S, C->ud);  // ��ʼ����Э�̵���ʵ������

    // ���´���Э�̺�����������ϣ����������˳�ʱ���༴���Э���˳�ʱ�������е�
    _co_delete(C);
	S->co[id] = NULL;
	--S->nco;
	S->running = -1;
}

void coroutine_resume(struct schedule * S, int id) 
{
	assert(S->running == -1);
	assert(id >=0 && id < S->cap);
	struct coroutine *C = S->co[id];
	if (C == NULL)
		return;

    int status = C->status;
	switch(status) 
    {
	    case COROUTINE_READY:
		    getcontext(&C->ctx);                  // ��ʼ���ṹ�壬����ǰ�������ķŵ�C->ctx��
		    C->ctx.uc_stack.ss_sp   = S->stack;   // ���õ�ǰЭ�̵�����ʱջ����ÿ��Э�̶�����S->stack
		    C->ctx.uc_stack.ss_size = STACK_SIZE; // ���õ�ǰЭ�̵�����ʱջ��С
		    C->ctx.uc_link          = &S->main;   // ���ú�������ģ�Э��������Ϻ��л���S->mainָ���������������
                                                  // �����ֵ����ΪNULL����ôЭ��������Ϻ����������˳�
		    C->status               = COROUTINE_RUNNING; // ���õ�ǰЭ��״̬Ϊ������
            S->running              = id;                // ���õ�ǰ����Э�̵�ID
		    
            uintptr_t ptr           = (uintptr_t)S;

            // ���õ�������Э�̵����к����壬�Լ��������
		    makecontext(&C->ctx, (void (*)(void))mainfunc, 2, (uint32_t)ptr, (uint32_t)(ptr>>32));
		    swapcontext(&S->main, &C->ctx); // ����ǰ�����ķŵ�S->main�У��ٽ�C->ctx����Ϊ��ǰ��������
		    break;
        case COROUTINE_SUSPEND:
            // ��ԭ�������ջ���ݣ���������ǰ����ʱջ�У��ָ�ԭ����ʱջ
		    memcpy(S->stack + STACK_SIZE - C->size, C->stack, C->size);
		    S->running = id;
		    C->status  = COROUTINE_RUNNING;
		    swapcontext(&S->main, &C->ctx);
		    break;
	    default:
		    assert(0);
	}
}

static void _save_stack(struct coroutine *C, char *top) 
{
	char dummy = 0;
	assert(top - &dummy <= STACK_SIZE);

    // top - &dummy��ʾ��ǰЭ�����õ�����ʱջ�Ĵ�С
	if (C->cap < top - &dummy) // ���Э�̽ṹ����ջ�ռ�С������ռ��С�������·����ڴ�ռ�
    {
		free(C->stack);             // �ͷ��ϵ�ջ������
		C->cap = top - &dummy;      // �����µ�ջ�������������
		C->stack = malloc(C->cap);  // ���·���ջ������
	}

	C->size = top - &dummy; // �����µ�ջ��������С

	memcpy(C->stack, &dummy, C->size); // ����ǰ������ʱջ�����ݣ����浽Э���е����ݻ�������
}

void coroutine_yield(struct schedule * S) 
{
	int id = S->running;    // ��õ�ǰ����Э�̵�id
	assert(id >= 0);
	struct coroutine * C = S->co[id];
	assert((char *)&C > S->stack);
	_save_stack(C, S->stack + STACK_SIZE);  // ���浱ǰ��Э�̵�����ʱջ����Э��˽��ջ������
	C->status = COROUTINE_SUSPEND;  // ����Ϊ����״̬
	S->running = -1;
	swapcontext(&C->ctx , &S->main); // ����ǰ����ʱջ���浽ctx�У������л���S->main��ָ�����������
}

int coroutine_status(struct schedule * S, int id) 
{
	assert(id>=0 && id < S->cap);
	if (S->co[id] == NULL) 
    {
		return COROUTINE_DEAD;
	}
	return S->co[id]->status;
}

int coroutine_running(struct schedule * S) 
{
	return S->running;
}

