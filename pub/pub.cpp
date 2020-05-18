#include"pub.h"

//对文件描述符设置非阻塞
void setnonblocking(int sock)
{
     int opts;
     opts=fcntl(sock,F_GETFL);
     if(opts<0)
     {
          LOG_ERROR("fcntl error in function setnonblocking");
          exit(1);
     }
     opts = opts|O_NONBLOCK;
     if(fcntl(sock,F_SETFL,opts)<0)
     {
          LOG_ERROR("fcntl error in function setnonblocking");
          exit(1);
     }  
}


//one_shot选择是否开启EPOLLONESHOT：
//ONESHOT选项表示只监听一次，监听完这次事件后，如果还需要监听
//，那就要再次把这个socket加入到EPOLL队列里;

//在EPOLLONESHOT模式下，对于同一个socket，每次读完或者写完之后
//再将其加入到EPOLL队列，就可以避免两个线程处理同一个socket
//（两个线程处理同一个soocket编码复杂性很高）
void addfd(int epollfd,int fd,bool one_shot)
{
	epoll_event event;
	event.data.fd = fd;
	//对于新增的event，默认监听ET和读
	event.events = EPOLLIN|EPOLLET|EPOLLRDHUP;

	if(one_shot)
		event.events |= EPOLLONESHOT;
	epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);

	setnonblocking(fd);
}

void modfd(int epollfd,int fd,int ev)
{
	epoll_event event;
	event.data.fd = fd;
	event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
	epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}

void removefd(int epollfd,int fd)
{
	epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
	close(fd);
}
