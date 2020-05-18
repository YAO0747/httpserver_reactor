#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include "block_queue.h"
using namespace std;

class Log
{
private:
	Log();
	~Log();

private:
	char m_dir_name[128];				//日志文件所在目录
	char m_real_filename[128];			//当前日志文件名
	int m_spilt_lines;					//每个日志文件中最多行数
	//int m_log_buf_size;				//日志缓冲区大小
	int m_count;						//日志行数记录
	int m_num_day;						//当天的文件数量
	int m_today;						//日期
	FILE* m_fp;							//文件指针
	//char* m_buf;						//缓冲区
	block_queue<string> *m_log_queue;	//阻塞队列
	locker m_mutex;						//多线程安全

public:
	//C++11之后，局部静态变量是线程安全的，因此不需要加锁
	//每次调用函数时，静态局部变量不会重新分配空间
	static Log*get_instance()
	{
		static Log instance;
		return &instance;
	}

	//将缓冲队列中的日志写入文件
	static void*flush_log_thread(void *args)
	{
		Log::get_instance()->async_write_log();
	}
	
	//初始化唯一的实例
	bool init(int split_lines = 5000000,int max_queue_size = 0);
	
	//将日志写入阻塞队列
	void write_log(int level,const char *format,...);
	
	//强制将缓冲区内的数据写回日志文件中
	void flush(void);
	int get_count(const char*);

	void *async_write_log()
	{
		string single_log;
		//从阻塞队列中取出一条日志并写入文件
		//如果队列为空则会挂起等待(等待条件变量)
		while(m_log_queue->pop(single_log))
		{
			m_mutex.lock();
			fputs(single_log.c_str(),m_fp);
			m_mutex.unlock();
		}
	}
};
#define LOG_DEBUG(format,...) Log::get_instance()->write_log(0, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) Log::get_instance()->write_log(1, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) Log::get_instance()->write_log(2, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) Log::get_instance()->write_log(3, format, ##__VA_ARGS__)

#define LOG_FLUSH() Log::get_instance()->flush()
#endif

