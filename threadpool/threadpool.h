//同步IO模型：由用户代码自行进行IO操作
//proactor模式：主线程和内核负责处理读写数据、接受新连接等IO操作，工作线程仅负责业务逻辑处理
//采用线程池，节省新建工作线程的时间

#ifndef THREADPOOL
#define THREADPOOL

#include<list>
#include<cstdio>
#include<exception>
#include<pthread.h>
#include"../lock/lock.h"
#include <sys/syscall.h>
#define gettid() syscall(__NR_gettid)
template<typename T>
class threadpool
{
private:
	int m_thread_number;//工作线程的数量
	int m_max_requests; //请求队列上限
	pthread_t *m_threads;//工作进程数组
	std::list<T*> m_workqueue;//请求队列
	locker m_queuelocker;//请求队列的锁
	sem m_queuestat;	//任务数量的信号量
	bool m_stop;		//是否结束该进程


	//工作线程的处理函数，必须声明为static
	//声明为static后，this指针需要通过参数的形式进行传递，这样
	//才能调用this->run();(因为没有this指针就不能访问类的成员
	//函数和成员变量等)
	static void*worker(void *arg);
	void run();

public:
	threadpool(int thread_number= 8,int max_request = 10000);
	~threadpool();
	bool append(T*request);//在请求队列中增加一个请求
};

template<typename T>
threadpool<T>::threadpool(int thread_number,int max_request)
{
	//初始化一些数字
	m_thread_number = thread_number;
	m_max_requests = max_request;
	m_stop = false;
	m_threads = NULL;

	//工作线程数组分配空间
	if(thread_number <= 0|| max_request <=0)
		throw std::exception();
	m_threads = new pthread_t[m_thread_number];
	if(m_threads==NULL)
	{
		throw std::exception();
	}
	
	//工作线程初始化
	for(int i = 0;i < m_thread_number;i++)
	{
		//创建线程，绑定worker工作函数
		if(pthread_create(m_threads+i,NULL,worker,this)!=0)
		{
			//一旦失败，则释放空间病抛出异常
			delete [] m_threads;
			throw std::exception();
		}
		//detach模式，线程结束时自动释放占用的资源
		if(pthread_detach(m_threads[i]))
		{
			delete [] m_threads;
			throw std::exception();
		}
	}
}

template<typename T>
threadpool<T>::~threadpool()
{
	delete[] m_threads;//释放资源
	m_stop = true;//终止所有工作线程
}

template<typename T>
bool threadpool<T>::append(T*request)
{
	//锁定请求队列
	m_queuelocker.lock();
	if(m_workqueue.size() > m_max_requests)
	{
		//请求过多被拒绝
		m_queuelocker.unlock();
		return false;
	}

	m_workqueue.push_back(request);
	m_queuelocker.unlock();
	m_queuestat.post();//信号量+1
	return true;
}

//规定传入pthread_create的函数原型为：(void*) func(void* )
//因此不能传入this指针，即不能传入成员函数，只能传入静态函数
//然而this指针又是必须的，否则无法使用threadpool的成员，因此
//通过第四个参数将this指针传入
template<typename T>
void *threadpool<T>::worker(void*arg)
{
	threadpool * pool = (threadpool*) arg;
	pool->run();//线程工作
	return pool;
}

template<typename T>
void threadpool<T>::run()
{
	//线程持续工作直到m_stop信号到来（也就是析构函数被调用时）
	while(!m_stop)
	{
		//当没有请求时不能让线程一直在轮询（浪费CPU资源）
		//因此采用信号量，模拟生产者消费者模型
		
		//为什么用信号量来控制？原因是如果用条件变量，那么在
		//append函数时会调用signal，而signal唤醒的工作线程数
		//量是不可控的
		m_queuestat.wait();

		m_queuelocker.lock();
		if(m_workqueue.empty())
		{
			m_queuelocker.unlock();
			continue;
		}
		T*request = m_workqueue.front();
		m_workqueue.pop_front();
		m_queuelocker.unlock();

		if(!request)
			continue;
		
		//处理请求
		if(request->get_event() == 3)
			request->close_conn();
		else if(request->get_event() == 2)
		{
			if(!request->m_write())
				request->close_conn();
		}
		else
		{
			if(request->m_read())
				request->process();
			else
				request->close_conn();
		}
	}
}

#endif
