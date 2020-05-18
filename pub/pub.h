#ifndef PUB_H
#define PUB_H
#include<sys/epoll.h>
#include <fcntl.h>
#include <stdio.h>
#include<stdlib.h>
#include <unistd.h>
#include "../log/log.h"
int setnonbloking(int fd);
void addfd(int epollfd,int fd,bool one_shot);
void modfd(int epollfd,int fd,int ev);
void removefd(int epollfd,int fd);
#endif
