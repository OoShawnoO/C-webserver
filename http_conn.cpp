#include "http_conn.h"

int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;

const char* doc_root = "/home/ubuntu/Documents/C++/webserver/resources";

const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Yor requests has bad syntax or inherently impossible";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server";
const char* error_404_title = "Not Found";
const char* error_404_form = "Not Found The File";
const char* error_500_form = "Unusual problem";
const char* error_500_title = "Internal Error";

void setnonblocking(int fd){
    int old_flag = fcntl(fd,F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd,F_SETFL,new_flag);
}

void addfd(int epollfd,int fd,bool one_shot){
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;
    if(one_shot){
        event.events | EPOLLONESHOT;
    }
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
    //设置文件描述符非阻塞
    setnonblocking(fd);
}


void removefd(int epollfd,int fd){
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
    close(fd);
}

void modfd(int epollfd,int fd,int ev){
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}

void http_conn::init(){
    m_check_state = CHECK_STATE_REQUESTLINE; // 初识状态为解析请求首行
    m_linger = false; // 默认不保持链接  Connection : keep-alive保持连接
    
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;

    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;

    bzero(m_read_buf, READ_BUFFER_SIZE);
    bzero(m_write_buf, WRITE_BUFFER_SIZE);
    bzero(m_real_file, 200);
}

//初始化连接
void http_conn::init(int sockfd,struct sockaddr_in addr){
    m_sockfd = sockfd;
    m_address = addr;
    //端口复用
    int reuse = 1;
    setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));

    //添加epoll对象
    addfd(m_epollfd,sockfd,true);
    m_user_count++;
    
    init();
}

//关闭连接
void http_conn::close_conn(){
    if(m_sockfd!=-1){
        removefd(m_epollfd,m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

bool http_conn::add_response(const char* format,...){
    if(m_write_idx >= WRITE_BUFFER_SIZE){
        return false;
    }
    va_list arg_list;
    va_start(arg_list,format);
    int len = vsnprintf(m_write_buf+m_write_idx,WRITE_BUFFER_SIZE-1-m_write_idx,format,arg_list);
    if(len>=(WRITE_BUFFER_SIZE-1-m_write_idx)){
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool http_conn::add_status_line(int status,const char* title){
    return add_response("%s %d %s\r\n","HTTP/1.1",status,title);
}

bool http_conn::add_headers(int content_len){
    add_content_length(content_len);
    if(strstr(m_real_file,".html")!=NULL){
        add_content_type();
    }else if(strstr(m_real_file,".jpg")!=NULL){
        add_content_pic_type();
    }
    add_linger();
    add_blank_line();
}

bool http_conn::add_content_length(int content_len){
    return add_response("Content-Length:%d\r\n",content_len);
}

bool http_conn::add_linger(){
    return add_response("Connection: %s\r\n",(m_linger==true)?"keep-alive":"close");
}

bool http_conn::add_blank_line(){
    return add_response("%s","\r\n");
}

bool http_conn::add_content(const char* content){
    return add_response("%s\r\n",content);
}

bool http_conn::add_content_type(){
    return add_response("Content-Type:%s\r\n","text/html");
}

bool http_conn::add_content_pic_type(){
    return add_response("Content-Type:%s\r\n","image/jpeg");
}

bool http_conn::read(){
    
    if(m_read_idx >= READ_BUFFER_SIZE){
        std::cout << "idx 越界" <<std::endl;
        return false;
    }
    std::cout<<m_read_buf<<std::endl;
    //读取到的字节
    int bytes_read = 0;
    while(true){
        std::cout << m_sockfd << std::endl;
        bytes_read = recv(m_sockfd,m_read_buf+m_read_idx,READ_BUFFER_SIZE-m_read_idx,0);
        
        if(bytes_read == -1){
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                //没有数据
                std::cout << "没有数据" << std::endl;
                break;
            }
            std::cout << "bytes_read  == -1" << std::endl;
            return false;
        }else if (bytes_read==0){
            //对方关闭连接
            std::cout << "对方关闭连接" << std::endl;
            return false;
        }
        m_read_idx += bytes_read;
        std::cout<< "读取到了数据:"<< std::endl << m_read_buf <<std::endl;
        return true;
    }

    return true;
}

bool http_conn::write(){
    int temp = 0;
    int bytes_have_send = 0;
    int bytes_to_send = m_write_idx;

    if(bytes_to_send==0){
        modfd(m_epollfd,m_sockfd,EPOLLIN);
        init();
        std::cout << "bytes to sen = 0" << std::endl;
        return true;
    }
    while(1){
        temp = writev(m_sockfd,m_iv,m_iv_count);
        if(temp == -1){
            //如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间
            if(errno == EAGAIN){
                std::cout << "EAGAIN" << std::endl;
                modfd(m_epollfd,m_sockfd,EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
        if(bytes_to_send <= bytes_have_send){
            unmap();
            if(m_linger){
                init();
                modfd(m_epollfd,m_sockfd,EPOLLIN);
                return true;
            }
            else{
                modfd(m_epollfd,m_sockfd,EPOLLIN);
                std::cout << "linger = false" << std::endl;
                return false;
            }
        }
    }
}

http_conn::HTTP_CODE http_conn::process_read(){
    
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    while(m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK
    ||(line_status = parse_line()) == LINE_OK){
        //解析到一行完整的数据
        char *text = get_line();
        m_start_line = m_checked_idx;
        std::cout<<"got 1 http line:  "<<text<<std::endl;
        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
            {
                
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                
                break;
                
            }
        case CHECK_STATE_HEADER:
            {
                
                ret = parse_headers(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }else if(ret == GET_REQUEST){
                    return do_request();
                }
                
            }
        case CHECK_STATE_CONTENT:
            {
                
                ret = parse_content(text);
                if(ret == GET_REQUEST){
                    
                    http_conn::HTTP_CODE code = do_request();
                    std::cout << code << std::endl;
                    return code;
                }
                line_status = LINE_OPEN;
                break;
                
            }
        
        default:
            std::cout << "Internal error" << std::endl;
            return INTERNAL_ERROR;
        }
    }

    return NO_REQUEST;
}

bool http_conn::process_write(http_conn::HTTP_CODE ret){
    switch(ret){
        case INTERNAL_ERROR:{
            add_status_line(500,error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form)){
                return false;
            }
            break;
        }
        case BAD_REQUEST:{
            add_status_line(400,error_400_title);
            add_headers(strlen(error_400_form));
            if(!add_content(error_400_form)){
                return false;
            }
            break;
        }
        case NO_RESOURCE:{
            add_status_line(404,error_404_title);
            add_headers(strlen(error_404_form));
            if(!add_content(error_404_form)){
                return false;
            }
            break;
        }
        case FOBIDDEN_REQUEST:{
            add_status_line(403,error_403_title);
            add_headers(strlen(error_403_form));
            if(!add_content(error_403_form)){
                return false;
            }
            break;
        }
        case FILE_REQUEST:{
            std::cout << "file request." << std::endl;
            add_status_line(200,ok_200_title);
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base =  m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            std::cout << m_write_buf << std::endl;
            return true;
        }
        default:{
            return false;
        }
    }
}


http_conn::HTTP_CODE http_conn::parse_request_line(char *text){
//解析http 请求行
    //GET /index.html HTTP/1.1
    m_url = strpbrk(text," \t");
    *m_url++ = '\0';
    char *method = text;
    if(strcasecmp(method,"GET")==0){
        m_method = GET;
    }else{
        std::cout << "parse request line bad request coursed by method" << std::endl;
        return BAD_REQUEST;
    }

    m_version = strpbrk(m_url," \t");
    if(!m_version){
        std::cout << "parse request line bad request coursed by version" << std::endl;
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    if(strcasecmp(m_version,"HTTP/1.1")!=0){
        return BAD_REQUEST;
    }
    if(strncasecmp(m_url,"http://",7)==0){
        m_url+=7;
        m_url = strchr(m_url,'/');
    }
    if(!m_url || m_url[0]!='/'){
        std::cout << "parse request line bad request coursed by url" << std::endl;
        return BAD_REQUEST;
    }

    m_check_state = CHECK_STATE_HEADER;

    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_headers(char *text){
    if(text[0]=='\0'){
        if(m_content_length!=0){
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }else if(strncasecmp(text,"Connection:",11)==0){
        text += 11;
        text += strspn(text,"\t");
        if(strcasecmp(text,"keep-alive")==0){
            m_linger = true;
        }
    }else if(strncasecmp(text,"Content-Length",15)==0){
        text += 15;
        text += strspn(text," \t");
        m_content_length = atol(text);
    }else if(strncasecmp(text,"Host:",5)==0){
        text += 5;
        text += strspn(text," \t");
        m_host = text;
    }else{
        std::cout << "oop! unknowheader" << text << std::endl;
    }

    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_content(char *text){
    if(m_read_idx >= (m_content_length + m_checked_idx)){
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

//解析一行 \r\n
http_conn::LINE_STATUS http_conn::parse_line(){
    char temp;
    for(;m_checked_idx<m_read_idx;++m_checked_idx){
        temp = m_read_buf[m_checked_idx];
        if(temp == '\r'){
            if(m_checked_idx+1==m_read_idx){
                return LINE_OPEN;
            }else if(m_read_buf[m_checked_idx+1]=='\n'){
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }else if(temp=='\n'){
            if(m_checked_idx>1&&m_read_buf[m_checked_idx-1]=='\r'){
                m_read_buf[m_checked_idx-1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

http_conn::HTTP_CODE http_conn::do_request(){
    strcpy(m_real_file,doc_root);
    int len = strlen(doc_root);
    strncpy(m_real_file+len,m_url,200 - len -1);
    if(stat(m_real_file,&m_file_stat)<0){
        return NO_RESOURCE;
    }
    if(!(m_file_stat.st_mode & S_IROTH)){
        return FOBIDDEN_REQUEST;
    }
    if(S_ISDIR(m_file_stat.st_mode)){
        return BAD_REQUEST; 
    }
    std::cout << m_real_file << std::endl;
    int fd = open(m_real_file,O_RDONLY);
    m_file_address = (char*)mmap(NULL,m_file_stat.st_size,PROT_READ,MAP_PRIVATE,fd,0);
    close(fd);
    return FILE_REQUEST;
}

void http_conn::unmap(){
    if(m_file_address){
        munmap(m_file_address,m_file_stat.st_size);
        m_file_address = 0;
    }
}

void http_conn::process(){
    //解析HTTP请求生成响应
    http_conn::HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST){
        modfd(m_epollfd,m_sockfd,EPOLLIN);
        return;
    }
    //生成响应
    bool write_ret = process_write(read_ret);
    if(!write_ret){
        close_conn();
    }
    modfd(m_epollfd,m_sockfd,EPOLLOUT);
}
