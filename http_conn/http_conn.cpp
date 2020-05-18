#include"http_conn.h"
#include <sys/syscall.h>

int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;


//定义http响应的一些状态信息
const char*ok_200_title = "OK";
const char* error_400_title = "Bab request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to statisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You don not have permission to get file from this server.\n";
const char* error_404_title = "Not found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the request line.\n";
const char *doc_root = "./resource";
static const char* szret[] = {"I get a correct result\n","something wrong"};

void http_conn::init(int sockfd,const sockaddr_in &addr)
{
	m_sockfd = sockfd;
	m_addr = addr;
	snprintf(m_client_info,99,"%s:%d",inet_ntoa(m_addr.sin_addr),ntohs(m_addr.sin_port));
	addfd(m_epollfd,sockfd,true);
	m_user_count++;
	init();
}
//init buffer
void http_conn::init()
{
	m_bytes_to_send = 0;
	m_bytes_have_send = 0;
	m_check_state = CHECK_STATE_REQUESTLINE;
	m_linger = false;//??默认情况应该是true还是false呢
	m_method = GET;
	m_url = 0;
	m_version = 0;
	m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
	memset(m_read_buf,'\0',READ_BUFFER_SIZE);
	memset(m_write_buf,'\0',WRITE_BUFFER_SIZE);
	memset(m_string,'\0',sizeof(m_string));
	m_read_idx = 0;
	m_write_idx = 0;
}
char* http_conn::get_client()
{
	return m_client_info;
}

void http_conn::unmap()
{
	if(m_file_address)
	{
		munmap(m_file_address, m_file_stat.st_size);
		m_file_address = 0;
	}
}
void http_conn::close_conn()
{
	LOG_INFO("close connection to client(%s)",get_client());
	LOG_FLUSH();
	if(m_sockfd!=-1)
	{
		removefd(m_epollfd,m_sockfd);
		m_sockfd = -1;
		m_user_count--;
	}
}

bool http_conn::m_read()
{
	LOG_INFO("receive data from client(%s)",get_client());
	if(m_read_idx >= READ_BUFFER_SIZE)
		return false;
	
	int bytes_read = 0;
	while(1)
	{
		bytes_read = recv(m_sockfd,m_read_buf + m_read_idx,
						  READ_BUFFER_SIZE - m_read_idx ,0 );
		//只有当对端关闭连接（服务端收到FIN）时，才会返回0,
		//在main里面提前捕获EPOLLRDHUP，所以应该不会出现bytes_read=0的情况
		if(bytes_read == 0)
			return false;
		if(bytes_read < 0)
		{
			if(errno==EAGAIN||errno == EWOULDBLOCK)
				return true;
			LOG_ERROR("error in m_read(),errno is %d",errno);
			return false;
		}
		m_read_idx += bytes_read;
	}
	return true;
}

bool http_conn::m_write()
{
	//https://blog.lucode.net/linux/talk-about-the-problem-of-writev.html
	//writev并不会为你做任何事情，重新处理iovec是调用者的任务
	int temp = 0;
	LOG_INFO("send data to client(%s)",get_client());
    if (m_bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        if (m_linger)
		{
			init();
			return true;
		}
		else
			return false;
    }

    while (1)
    {
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp > 0)
        {
            //m_bytes_have_send += temp;
			m_bytes_to_send -= temp;
			if (m_bytes_to_send <= 0)
			{
				unmap();
				modfd(m_epollfd, m_sockfd, EPOLLIN);
				if (m_linger)
				{
					init();
					return true;
				}
				else
					return false;
			}

			if(temp >= m_iv[0].iov_len)
			{
				m_iv[1].iov_base= m_iv[1].iov_base+ (temp-m_iv[0].iov_len);
				m_iv[1].iov_len = m_iv[1].iov_len-(temp - m_iv[0].iov_len);
				m_iv[0].iov_len = 0;
			}
			else
			{
				m_iv[0].iov_base = m_iv[0].iov_base + temp;
				m_iv[0].iov_len -= temp;
			}
        }
        if (temp <= -1)
        {
            if (errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
			LOG_ERROR("error in m_write, errno is %d",errno);
            return false;
        }
    }
}

//当 EPOLL_CTL_ADD或EPOLL_CTL_MOD 时 , 如果socket输出缓冲区没满就能触发一次
void http_conn::process() 
{
	LOG_INFO("process request from client(%s)",get_client());
	HTTP_CODE ret = process_read();
	if(ret == NO_REQUEST)
	{
		modfd(m_epollfd,m_sockfd,EPOLLIN);
		return;
	}
	bool write_ret = process_write(ret);
	if(!write_ret)
		close_conn();
	if(!m_write())
		close_conn();
	//modfd(m_epollfd,m_sockfd,EPOLLOUT);
}
//解析m_read_buf中的内容，每次读取一行
http_conn:: LINE_STATUS http_conn::parse_line()
{
	char text;

	for(;m_checked_idx < m_read_idx;++m_checked_idx)
	{
		//获得当前字节
		text = m_read_buf[m_checked_idx];

		//若为'\r'，则表示可能读到了一个完整的行
		if(text == '\r')
		{
			//若正好是buffer的最末尾的字节，则返回LINE_OPEN
			if(m_checked_idx == m_read_idx -1)
				return LINE_OPEN;
			//若下一个字节是'\n'，则说明读到了完整的行
			if(m_read_buf[m_checked_idx+1] == '\n')
			{
				//更新checked_index
				m_read_buf[m_checked_idx++] = '\0';
				m_read_buf[m_checked_idx++] = '\0';
				return LINE_OK;
			}
			return LINE_BAD;
		}
		//若为'\n',也说明可能读到了一个完整的行
		else if(text == '\n')
		{
			if((m_checked_idx > 1)&&m_read_buf[m_checked_idx-1]=='\r')
			{	
				//更新checked_index
				m_read_buf[m_checked_idx-1] = '\0';
				m_read_buf[m_checked_idx++] = '\0';
				return LINE_OK;
			}
			return LINE_BAD;
		}
	}
	return LINE_OPEN;

}
//解析请求行，参数text: 请求行（在m_read_buf中）的地址
http_conn:: HTTP_CODE http_conn::parse_requestline(char* text)
{
	//若请求行无空格或\t，则一定出错，因此先判断有无空格
	//m_url位置指向请求行text的第二个字段
	m_url = strpbrk(text, " \t");
    if (!m_url)
        return BAD_REQUEST;
    *m_url++ = '\0';

	//text的第一个字段是请求方法
    char *method = text;
    if (strcasecmp(method, "GET") == 0)
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0)
        m_method = POST;
    else
        return BAD_REQUEST;

	//请求行有可能以多个空格分隔字段，所以m_url需要再次移动
    m_url += strspn(m_url, " \t");

	//version在第三个字段
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
        return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if (!(strcasecmp(m_version, "HTTP/1.1") == 0||strcasecmp(m_version, "HTTP/1.0") == 0))
        return BAD_REQUEST;

	//开始处理m_url,提取出/.....
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }
    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;
	
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}	

//解析请求头部，参数text: 该请求头部（在m_read_buf中）的地址
http_conn:: HTTP_CODE http_conn::parse_headers(char*text)
{
	//遇到空行，需要判断后面是否有请求体
	if (text[0] == '\0')
    {
        if (m_content_length != 0)
        {
			//如果有请求体，则需要更新状态
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
		//若无请求体，则解析完成
        return GET_REQUEST;
    }
	//处理Connection
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
		//printf("Connection请求头部内容：%s\n",text);
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
		else if(strcasecmp(text, "close") == 0)
			m_linger = false;

    }
	//处理Content-length
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
	//处理Host
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else
    {
		;
    }
    return NO_REQUEST;
}

//读取请求体，暂不进行处理，参数text: 请求体（在m_read_buf中）的地址
http_conn:: HTTP_CODE http_conn::parse_content(char*text)
{
	//判断请求体是否已经全部在m_read_buf中
	if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
		
		memset(m_string,'\0',sizeof(m_string));

        //POST请求中最后为输入的用户名和密码
        strcpy(m_string,text);
		return GET_REQUEST;
    }
    return NO_REQUEST;
}

//主状态机，处理整个请求
http_conn::HTTP_CODE http_conn::process_read()
{
	LINE_STATUS line_status = LINE_OK;//当前行的状态
	HTTP_CODE ret = NO_REQUEST;
	char * text = 0;
	//每次调用parse_line()获取一行，解析到content时无需再调用parse_line逐行获取
	while((m_check_state == CHECK_STATE_CONTENT&&line_status == LINE_OK)||((line_status = parse_line())==LINE_OK))
	{
		//得到当前行:buf首地址+行起始位置
		text = m_read_buf + m_start_line;
		m_start_line = m_checked_idx;//更新行起始位置

		switch(m_check_state)
		{
			case CHECK_STATE_REQUESTLINE:
			{
				ret = parse_requestline(text);
				if(ret==BAD_REQUEST)
					return BAD_REQUEST;
				break;
			}
			case CHECK_STATE_HEADER:
			{
				ret = parse_headers(text);
				if(ret == BAD_REQUEST)
					return BAD_REQUEST;
				else if(ret == GET_REQUEST)
					return do_request();
				break;
			}
			case CHECK_STATE_CONTENT:
			{
				ret = parse_content(text);
				if(ret==GET_REQUEST)
					return do_request();
				//如果parse_content返回值不为GET_REQUEST,则表示
				//m_read_buf中未包含完整的请求，需要继续监听并读取
				else
					line_status = LINE_OPEN;
				break;
			}
			default:
				return INTERNAL_ERROR;
		}
	}
	return NO_REQUEST;
}
//整个http请求被解析完成后，调用do_request进行处理（响应）
http_conn:: HTTP_CODE http_conn::do_request()
{
	if(m_content_length != 0)
	{
		char name[25],password[25];
		int i;
		for(i = 5;m_string[i] != '&';i++)
			name[i-5] = m_string[i];
		name[i-5] = 0;

		int j = 0;
		for(i = i + 10;m_string[i] != '\0';i++,j++)
			password[j] = m_string[i];
		password[j] = '\0';

		if(strcmp(name,"yao")==0&&strcmp(password,"123456")==0)
			strncpy(m_url,"/welcome.html\0",14);
		else
			strncpy(m_url,"/login2.html\0",13);
	}


	memset(m_real_file,'\0',sizeof(m_real_file));
	strcpy(m_real_file,doc_root);
	int len = strlen(m_real_file);
	
	//一个坑，如果temp_url直接指向m_url的话，temp_url会修改http_conn的内容。
	char* temp_url = (char*)malloc(sizeof(char)*200);
	strcpy(temp_url,m_url);
	if(strcmp(temp_url,"/")==0)
		strncpy(temp_url,"/login.html\0",12);
	//*(++temp_url) = '\0';

	strncpy(m_real_file+len,temp_url,strlen(temp_url));

	//将m_file_stat与m_real_file 绑定，并判断资源是否存在
	if(stat(m_real_file,&m_file_stat) < 0)	
		return NO_RESOURCE;
	//判断权限
	if(!(m_file_stat.st_mode & S_IROTH))
		return FORBIDDEN_REQUEST;
	//判断是否为目录
	if(S_ISDIR(m_file_stat.st_mode))
		return BAD_REQUEST;
	
	//是正确的文件请求
	int fd = open(m_real_file,O_RDONLY);
	m_file_address = (char*)mmap(0,m_file_stat.st_size,PROT_READ,MAP_PRIVATE,fd,0);
	close(fd);
	free(temp_url);
	return FILE_REQUEST;
}

//ret是process_read的返回值
bool http_conn::process_write(HTTP_CODE ret)
{
	switch (ret)
    {
    case INTERNAL_ERROR:
    {
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
            return false;
        break;
    }
    case NO_RESOURCE:
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
            return false;
        break;
    }
	case BAD_REQUEST:
	{
		add_status_line(400,error_400_title);
		add_headers(strlen(error_400_form));
		if(!add_content(error_400_form))
			return false;
		break;
	}
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    case FILE_REQUEST:
    {
        add_status_line(200, ok_200_title);
        if (m_file_stat.st_size != 0)
        {
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            m_bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        else
        {
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
                return false;
        }
    }
    default:
        return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    m_bytes_to_send = m_write_idx;
    return true;
}

bool http_conn::add_response(const char *format, ...)
{
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;
    va_list arg_list;
	//第一个参数是arg_list（类型是va_list），format是最后一个确定的参数
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}
bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
bool http_conn::add_headers(int content_len)
{
    add_content_length(content_len);
    add_linger();
    add_blank_line();
}
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}
bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}
bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}
