#include <string>
#include <cstring>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include<pthread.h>
using namespace std;

Log::Log()
{
	;
}

Log::~Log()
{
	if(m_fp != NULL)
		fclose(m_fp);
}
//获取日志文件的行数
int Log::get_count(const char*filename)
{
	FILE *fp = fopen(filename, "r");
    if (!fp)
    {
		LOG_ERROR("failed to open log file %s",filename);
		exit(1);
    }

	const int MAX_CHAR_PER_LINE = 128;
	char ch;
	long offset = -1;
	char lastline[MAX_CHAR_PER_LINE];
    memset(lastline, 0, sizeof(lastline));
 
    //将文件指针移动到倒数第一个字符
    fseek(fp, offset, SEEK_END);
	//从后往前寻找,删除末尾的回车符
    while ((ch = (char)fgetc(fp)) == '\n')
	{
		offset--;
        fseek(fp, offset, SEEK_END);
	}
	//如果上次只是新建文件却没有写入内容，则返回0，使得m_count = 0
	if(ch == EOF)
		return 0;
	
	//一个坑，如果前面已经fgetc一次，那么fp会自动后移一位，
	//再次fgetc就会导致读到下一个字符
	fseek(fp, offset, SEEK_END);
    while((ch=(char)fgetc(fp)) != '\n')
	{      
        offset --;
		fseek(fp, offset, SEEK_END);
    }
	//读取最后一行
	fgets(lastline,MAX_CHAR_PER_LINE,fp);
    fclose(fp);

	//提取最后一行的编号
	char num[20];
	int i;
	for(i = 0;i < 20;i++)
	{
		if(lastline[i]!=' ')
			num[i] = lastline[i];
		else
			break;
	}
	num[i] =0;
	int num_ = atoi(num);
	return num_;
}


bool Log::init(int spilt_lines,int max_queue_size)
{
	//初始化阻塞队列
	m_log_queue = new block_queue<string>(max_queue_size);
	
	//创建线程，异步将日志写入到文件中
	pthread_t tid;
	pthread_create(&tid,NULL,flush_log_thread,NULL);
	
	//初始化缓冲区，先在m_buf构造好日志记录，再转化为string并push进阻塞队列
	m_spilt_lines = spilt_lines;
	
	//准备好时间函数，用于创建日志文件名
	time_t timep;
	struct tm *p;
	time(&timep);
	p = localtime(&timep);
	m_today = p->tm_mday;

	//创建日志文件
	strncpy(m_dir_name,"./logfile/\0",11);
	//判断今天是否已经有日志文件了
	int i;
	char temp_filename[128];
	for(i = 1;i < 1000;i++)
	{
		snprintf(temp_filename,127,"%s%d-%02d-%02d(%d).log\0",m_dir_name,
							p->tm_year+1900,p->tm_mon+1,p->tm_mday,i);
		if((access(temp_filename,0)) == -1)
			break;
	}
	//说明今天还没有日志文件
	if(i == 1)	
	{
		m_num_day = 1;
		m_count = 0;
	}
	else
	{
		i--;
		m_num_day = i;
		snprintf(temp_filename,127,"%s%d-%02d-%02d(%d).log\0",m_dir_name,
					p->tm_year+1900,p->tm_mon+1,p->tm_mday,i);
		m_count = get_count(temp_filename);
	}
	snprintf(m_real_filename,255,"%s%d-%02d-%02d(%d).log\0",m_dir_name,
							p->tm_year+1900,p->tm_mon+1,p->tm_mday,m_num_day);

	m_fp = fopen(m_real_filename,"a");
	if (m_fp == NULL)
    {
        return false;
    }
    return true;
}

void Log::write_log(int type, const char *format,...)
{
	//准备好时间函数，用于创建日志文件名
	time_t timep;
	struct tm *t;
	time(&timep);
	t = localtime(&timep);
	
	struct timeval now = {0,0};
	gettimeofday(&now,NULL);

	char s_type[16] = {0};
	switch(type)
	{
	case 0:
		strcpy(s_type,"[debug]:");
		break;
	case 1:
		strcpy(s_type,"[info]");
		break;
	case 2:
		strcpy(s_type,"[warn]");
		break;
	case 3:
		strcpy(s_type,"[erro]");
		break;
	default:
		strcpy(s_type,"[info]");
		break;
	}

	//写入一条日志
	m_mutex.lock();
	m_count++;

	//判断是否需要创建新的文件
	if(m_today != t->tm_mday||m_count > m_spilt_lines)
	{
		fflush(m_fp);
		fclose(m_fp);
		m_count = 1;
		m_today = t->tm_mday;
		if(m_today!= t->tm_mday)
			m_num_day = 1;
		else
			m_num_day ++;

		snprintf(m_real_filename,255,"%s%d-%02d-%02d(%d).log\0",m_dir_name,
							t->tm_year+1900,t->tm_mon+1,t->tm_mday,m_num_day);
		m_fp = fopen(m_real_filename,"a");
	}

	//构造日志记录
	char temp_log[256];
	string str_log;
	va_list valst;
	va_start(valst,format);

	int n = snprintf(temp_log,48,"%d %d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
					m_count, t->tm_year+1900,t->tm_mon+1,t->tm_mday,t->tm_hour
					,t->tm_min, t->tm_sec,now.tv_usec,s_type);
	int m = vsnprintf(temp_log + n, 255,format,valst);
	temp_log[m+n] = '\n';
	temp_log[m+n+1] = '\0';
	str_log = temp_log;
	m_mutex.unlock();

	//日志记录push进入阻塞队列
	m_log_queue->push(str_log);
}
void Log::flush(void)
{
	m_mutex.lock();
	fflush(m_fp);
	m_mutex.unlock();
}
