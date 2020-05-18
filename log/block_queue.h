//阻塞环形队列
#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H
#include<iostream>
#include<stdlib.h>
#include<pthread.h>
#include<sys/time.h>
#include"../lock/lock.h"
using namespace std;

template <typename T>
class block_queue
{
private:
	locker m_mutex;
	cond m_cond;

	T*m_array;
	int m_front;
	int m_rear;
	int m_size;
	int m_length;

public:
	block_queue(int max_size=1000)
	{
		m_size = max_size;
		m_front = 1;
		m_rear = 0;
		m_array = new T[m_size];
		m_length = 0;
	}
	~block_queue()
	{
		m_mutex.lock();
		delete[] m_array;
		m_mutex.unlock();
	}

	bool push(const T&elem)
	{
		m_mutex.lock();

		if(m_size == m_length)
		{
			m_cond.broadcast();
			m_mutex.unlock();
			return false;
		}
		m_rear++;
		m_rear = m_rear%m_size;
		m_array[m_rear] = elem;
		m_length++;

		m_cond.broadcast();
		m_mutex.unlock();
		return true;
	}
	bool pop(T& elem)
	{
		m_mutex.lock();
		if(m_length == 0)
		{
			if(!m_cond.wait(m_mutex.get()))
			{
				m_mutex.unlock();
				return false;
			}
		}
		elem = m_array[m_front];
		m_front = (1+m_front)%m_size;
		m_length--;
		m_mutex.unlock();
		return true;
	}
	bool full()
	{
		m_mutex.lock();
		if(m_length >= m_size)
		{
			m_mutex.unlock();
			return true;
		}
		m_mutex.unlock();
		return false;
	}
	bool front(T&elem)
	{
		m_mutex.lock();
		if(m_length == 0)
		{
			m_mutex.unlock();
			return false;
		}
		elem = m_array[m_front];
		m_mutex.unlock();
		return true;
	}
	
	bool back(T&elem)
	{
		m_mutex.lock();
		if(m_length == 0)
		{
			m_mutex.unlock();
			return false;
		}
		elem = m_array[m_rear];
		m_mutex.unlock();
		return true;
	}


	int size()
	{
		int ret = 0;
		m_mutex.lock();
		ret = m_length;
		m_mutex.unlock();
		return ret;
	}
	int max_size()
	{
		int ret = 0;
		m_mutex.lock();
		ret = m_size;
		m_mutex.unlock();
		return ret;
	}
};
#endif
