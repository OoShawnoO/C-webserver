#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>
#include <arpa/inet.h>
#include "locker.h"
#include "ThreadPool.h"
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <sys/uio.h>
#include <string.h>


class http_conn{
public:
    static int m_epollfd;
    static int m_user_count;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;

    enum METHOD{GET=0,POST,HEAD,PUT,DELETE,TRACE,OPTIONS,CONNECT};
    enum CHECK_STATE{CHECK_STATE_REQUESTLINE=0,CHECK_STATE_HEADER,CHECK_STATE_CONTENT};
    /*
        CHECK_STATE_REQUESTLINE 当前正在分析请求行
        CHECK_STATE_HEADER 当前正在分析头部字段
        CHECK_STATE_CONTENT 当前正在解析请求体
    */
    enum HTTP_CODE{NO_REQUEST,GET_REQUEST,BAD_REQUEST,NO_RESOURCE,FOBIDDEN_REQUEST,FILE_REQUEST,INTERNAL_ERROR,CLOSED_CONNECTION};
    enum LINE_STATUS{LINE_OK=0,LINE_BAD,LINE_OPEN};


    http_conn(){};
    ~http_conn(){};
    void init(int sockfd,struct sockaddr_in addr);
    void close_conn();
    bool read(); //非阻塞读
    bool write(); //非阻塞写
    void process(); //处理客户端请求
    

private:
    int m_sockfd;
    sockaddr_in m_address;
    char m_read_buf[READ_BUFFER_SIZE];
    char m_write_buf[WRITE_BUFFER_SIZE];
    int m_read_idx;//表示读缓冲区中已经读入的客户端数据的最后一个字节的下一个位置;
    int m_checked_idx;//当前正在解析的字符在缓冲区的位置；
    int m_start_line;//当前正在解析的行的起始位置
    int m_write_idx;
    char *m_url;
    char *m_version;
    METHOD m_method;
    char *m_host;
    bool m_linger;
    long int m_content_length;
    char m_real_file[200];
    struct stat m_file_stat;
    char* m_file_address;
    struct iovec m_iv[2];
    int m_iv_count;
    

    CHECK_STATE m_check_state; //主机当前的状态
    HTTP_CODE process_read();//解析HTTP请求
    bool process_write(HTTP_CODE);
    HTTP_CODE parse_request_line(char *text);//解析请求首行
    HTTP_CODE parse_headers(char *text);//解析请求首行
    HTTP_CODE parse_content(char *text);//解析内容
    LINE_STATUS parse_line(); //解析行
    void init();//初始化连接相关的信息
    char* get_line(){
        return m_read_buf + m_start_line;
    }
    HTTP_CODE do_request();
    void unmap();

    bool add_response(const char* format,...);
    bool add_status_line(int status,const char* title);
    bool add_headers(int cotent_len);
    bool add_content_length(int content_len);
    bool add_linger();
    bool add_blank_line();
    bool add_content(const char* content);
    bool add_content_type();
    bool add_content_pic_type();


};
#endif