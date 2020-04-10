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

#define STACK_SIZE        (1024*1024)   // 默认的协程运行时栈大小
#define DEFAULT_COROUTINE 16            // 调度器默认的初始协程容器大小

struct coroutine;

// 调度器结构体
struct schedule 
{
	char        stack[STACK_SIZE];  // 协程运行时栈（被所有协程共享）
	ucontext_t  main;               // 主协程的上下文
	int         nco;                // 当前存活的协程数量
	int         cap;                // 调度器中的协程容器的最大容量。后期可以根据需要进行扩容。
	int         running;            // 当前正在运行的协程ID
	struct coroutine **co;          // 调度器中的协程容器
};

// 协程结构体
struct coroutine 
{
	coroutine_func    func;         // 协程所运行的函数
	void            * ud;           // 协程参数
	ucontext_t        ctx;          // 当前协程的上下文
	struct schedule * sch;          // 当前协程所属的调度器
	ptrdiff_t         cap;          // 当前栈缓存的最大容量
	ptrdiff_t         size;         // 当前栈缓存的大小
	int               status;       // 当前协程的运行状态（即：COROUTINE_{DEAD,READY,RUNNING,SUSPEND}这四种状态其一）
	char            * stack;        // 当前协程切出时保存下来的运行时栈
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
	S->nco      = 0;                    // 当前运行协程数量为0
	S->cap      = DEFAULT_COROUTINE;    // 初始协程数量
	S->running  = -1;                   // 设置当前正在运行的协程
	S->co       = malloc(sizeof(struct coroutine *) * S->cap); // 分配协程容器
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

// 该函数是协程入口的包裹函数
// 协程开始运行时先运行该函数，然后在其中通过调用C->func再运行协程真实的函数体
static void mainfunc(uint32_t low32, uint32_t hi32) 
{
	uintptr_t ptr = (uintptr_t)low32 | ((uintptr_t)hi32 << 32); // 根据传入的参数，重构出调度器指针
	struct schedule *S = (struct schedule *)ptr;
	int id = S->running;
	struct coroutine *C = S->co[id];

	C->func(S, C->ud);  // 开始运行协程的真实函数体

    // 以下代码协程函数体运行完毕，整个函数退出时，亦即这个协程退出时，才运行的
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
		    getcontext(&C->ctx);                  // 初始化结构体，将当前的上下文放到C->ctx中
		    C->ctx.uc_stack.ss_sp   = S->stack;   // 设置当前协程的运行时栈顶，每个协程都共享S->stack
		    C->ctx.uc_stack.ss_size = STACK_SIZE; // 设置当前协程的运行时栈大小
		    C->ctx.uc_link          = &S->main;   // 设置后继上下文，协程运行完毕后，切换到S->main指向的上下文中运行
                                                  // 如果该值设置为NULL，那么协程运行完毕后，整个程序退出
		    C->status               = COROUTINE_RUNNING; // 设置当前协程状态为运行中
            S->running              = id;                // 设置当前运行协程的ID
		    
            uintptr_t ptr           = (uintptr_t)S;

            // 设置当待运行协程的运行函数体，以及所需参数
		    makecontext(&C->ctx, (void (*)(void))mainfunc, 2, (uint32_t)ptr, (uint32_t)(ptr>>32));
		    swapcontext(&S->main, &C->ctx); // 将当前上下文放到S->main中，再将C->ctx设置为当前的上下文
		    break;
        case COROUTINE_SUSPEND:
            // 将原来保存的栈数据，拷贝到当前运行时栈中，恢复原运行时栈
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

    // top - &dummy表示当前协程所用的运行时栈的大小
	if (C->cap < top - &dummy) // 如果协程结构体中栈空间小于所需空间大小，则重新分配内存空间
    {
		free(C->stack);             // 释放老的栈缓存区
		C->cap = top - &dummy;      // 设置新的栈缓存区最大容量
		C->stack = malloc(C->cap);  // 重新分配栈缓存区
	}

	C->size = top - &dummy; // 设置新的栈缓存区大小

	memcpy(C->stack, &dummy, C->size); // 将当前的运行时栈的数据，保存到协程中的数据缓存区中
}

void coroutine_yield(struct schedule * S) 
{
	int id = S->running;    // 获得当前运行协程的id
	assert(id >= 0);
	struct coroutine * C = S->co[id];
	assert((char *)&C > S->stack);
	_save_stack(C, S->stack + STACK_SIZE);  // 保存当前子协程的运行时栈，到协程私有栈缓存中
	C->status = COROUTINE_SUSPEND;  // 设置为挂起状态
	S->running = -1;
	swapcontext(&C->ctx , &S->main); // 将当前运行时栈保存到ctx中，并且切换到S->main所指向的上下文中
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

