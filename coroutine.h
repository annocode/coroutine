#ifndef C_COROUTINE_H
#define C_COROUTINE_H

#define COROUTINE_DEAD    0 // 协程退出状态
#define COROUTINE_READY   1 // 协程就绪状态
#define COROUTINE_RUNNING 2 // 协程运行状态
#define COROUTINE_SUSPEND 3 // 协程挂起状态

struct schedule;    // 调度器结构体

typedef void (*coroutine_func)(struct schedule *, void *ud);

struct schedule * coroutine_open(void);     // 开启调度器，生成一个调度器实例

void coroutine_close(struct schedule *);    // 关闭调度器

int  coroutine_new(struct schedule *, coroutine_func, void *ud);    // 生成一个协程实例
void coroutine_resume(struct schedule *, int id);                   // 恢复运行一个协程
int  coroutine_status(struct schedule *, int id);                   // 获取协程运行状态
int  coroutine_running(struct schedule *);                          // 判断协程是否在运行
void coroutine_yield(struct schedule *);                            // 切出运行状态

#endif
