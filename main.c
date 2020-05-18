#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>
#include "./log/log.h"
#include "./http_conn/http_conn.h"
#include "./threadpool/threadpool.h"
#include "./pub/pub.h"
#define MAX_FD 65535			//最大文件描述符(连接数+1)
#define MAX_EVENT_NUMBER 10000	//最大事件数
static int epollfd = 0;
void sigint_handler(int sig)
{
	LOG_INFO("shut down the server");
	LOG_FLUSH();
	exit(0);
}
int main(int argc,char**argv)
{
	if(argc <= 1)
	{
		perror("参数错误");
		return 1;
	}
	
	//初始化日志系统	
	Log::get_instance()->init(800000, 800);
	
	//信号处理函数
	signal(SIGPIPE, SIG_IGN);//忽略SIGPIPE
	signal(SIGSEGV,SIG_IGN); //忽略SIGSEGV
	//捕获SIGINT/SIGKILL/SIGTERM
	sigset(SIGINT,sigint_handler);
	sigset(SIGKILL,sigint_handler);
	sigset(SIGTERM,sigint_handler);

	//初始化工作线程池
	threadpool<http_conn> *pool = NULL;
	try
	{
		pool = new threadpool<http_conn>(8);
	}
	catch(std::exception& e)
	{
		LOG_ERROR("error in main : new threadpool error");
		return 1;
	}
	
	//http连接处理类
	http_conn *users = new http_conn[MAX_FD];
	http_conn::m_user_count = 0;

	//服务端socket开始工作
	int user_count = 0;
	int listenfd;
	
	//socket()
	listenfd = socket(AF_INET,SOCK_STREAM,0);
	assert(listen >= 0);

	struct sockaddr_in servaddr;
	bzero(&servaddr,sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(atoi(argv[1]));
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

	int ret = 0,flag = 1;
	//设置套接字选项，可参考《UNIX网络编程》第7.5节P165
	setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&flag,sizeof(flag));
	//bind()
	ret = bind(listenfd,(struct sockaddr*)&servaddr,sizeof(servaddr));
	assert(ret>=0);
	//listen()
	ret = listen(listenfd,5);
	assert(ret>=0);

	//创建内核事件表,用来接收epoll_wait
	//传出来的满足监听事件的fd结构体
	epoll_event events[MAX_EVENT_NUMBER];
	
	//epoll_create创建红黑树，epoll_create的参数在Linux2.6
	//之后就没什么用了，但是要大于0
	epollfd = epoll_create(5);
	assert(epollfd!=-1);
	
	//封装了epoll_ctl函数，往epollfd中加入listenfd
	addfd(epollfd,listenfd,false);
	http_conn::m_epollfd = epollfd;

	//epoll开始监听,采用ET（边缘触发）
	bool stop_server = false;
	LOG_INFO("start the server");
	int nready = 0;
	while(!stop_server)
	{
		//调用epoll_wait
		nready=epoll_wait(epollfd,events,MAX_EVENT_NUMBER,-1);
		if(nready<0&&errno != EINTR)
		{
			LOG_ERROR("error in epoll_wait, errno is %d",errno);
			break;
		}
		
		//处理events
		for(int i=0;i < nready;i++)
		{
			int sockfd = events[i].data.fd;
			
			//listenfd上面有新连接，在ET模式下，需要将当前的
			//所有连接都处理,否则延迟会高
			if(sockfd == listenfd)
			{	
				struct sockaddr_in cliaddr;
				socklen_t clilen = sizeof(cliaddr);
				int connfd;
				
				while(1)
				{
					connfd=accept(listenfd,(struct sockaddr*)
											&cliaddr,&clilen);
					//所有连接都处理完毕
					if(connfd < 0)
					{
						if(errno != EAGAIN)
							LOG_ERROR("accept error, errno is %d",errno);
						break;
					}
					//用户数过多
					if(http_conn::m_user_count >= MAX_FD)
					{
						LOG_ERROR("accept error,too much client");
						break;
					}
					
					//init初始化新接收的连接，并加入epoll监听列表
					users[connfd].init(connfd,cliaddr);
					LOG_INFO("connect with client(%s)",users[connfd].get_client());
					continue;
				}
			}
			//处理错误
			//对端连接断开时会同时触发EPOLLRDHUP和EPOLLIN
			else if(events[i].events&(EPOLLRDHUP|EPOLLHUP|EPOLLERR))
			{
				LOG_INFO("client(%s) disconnected",users[sockfd].get_client());
				//在reactor模式下，请求队列的锁可能会是程序的瓶颈
				users[sockfd].set_event(3);
				pool->append(users+sockfd);
				//users[sockfd].close_conn();
			}
			//处理读事件
			else if(events[i].events&EPOLLIN)
			{
				users[sockfd].set_event(1);
				pool->append(users+sockfd);

				/*if(users[sockfd].m_read())
				{
					pool->append(users+sockfd);
				}
				else
				{
					users[sockfd].close_conn();
				}*/
			}
			//处理写事件
			else if(events[i].events&EPOLLOUT)
			{
				users[sockfd].set_event(2);
				pool->append(users+sockfd);

				/*if(!users[sockfd].m_write())
				{
					users[sockfd].close_conn();
				}*/
			}
		}
	}
	close(epollfd);
	close(listenfd);
	delete[]users;
	delete pool;
	return 0;
}
