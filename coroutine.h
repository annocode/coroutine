#ifndef C_COROUTINE_H
#define C_COROUTINE_H

#define COROUTINE_DEAD    0 // Э���˳�״̬
#define COROUTINE_READY   1 // Э�̾���״̬
#define COROUTINE_RUNNING 2 // Э������״̬
#define COROUTINE_SUSPEND 3 // Э�̹���״̬

struct schedule;    // �������ṹ��

typedef void (*coroutine_func)(struct schedule *, void *ud);

struct schedule * coroutine_open(void);     // ����������������һ��������ʵ��

void coroutine_close(struct schedule *);    // �رյ�����

int  coroutine_new(struct schedule *, coroutine_func, void *ud);    // ����һ��Э��ʵ��
void coroutine_resume(struct schedule *, int id);                   // �ָ�����һ��Э��
int  coroutine_status(struct schedule *, int id);                   // ��ȡЭ������״̬
int  coroutine_running(struct schedule *);                          // �ж�Э���Ƿ�������
void coroutine_yield(struct schedule *);                            // �г�����״̬

#endif
