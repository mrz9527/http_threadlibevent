#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <event.h>

#include "locker.h"

/**
 * HTTP任务类
*/
class http_conn
{
public:
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;
    enum METHOD { GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH };   // 请求方法
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };  // 主状态机：解析请求行、解析请求头部、解析正文
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };   // 解析结果
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };  // 从状态机：读取到一个完整行、行错误、行不完整

public:
    http_conn() {}
    ~http_conn()
    {
        if (read_ev != nullptr)
        {
            event_free(read_ev);
            read_ev = nullptr;
        }
        if (write_ev != nullptr)
        {
            event_free(write_ev);
            write_ev = nullptr;
        }
    }

public:
    void init(int sockfd, const sockaddr_in& addr, struct event* rev, struct event* wev);   // 初始化新接受的连接
    void close_conn();  // 关闭连接
    void process();     // 处理HTTP请求的入口函数
    bool read();    // 非阻塞读HTTP请求报文
    bool write();   // 非阻塞写HTTP响应

private:
    void init();    // 初始化HTTP请求解析状态变量
    HTTP_CODE process_read();   // 解析HTTP请求
    bool process_write(HTTP_CODE ret);  // 决定返回给客户端的内容

    /**** 下面一组函数由process_read()调用以解析HTTP请求 ****/
    HTTP_CODE parse_request_line( char* text );     // 解析请求行
    HTTP_CODE parse_headers( char* text );      // 解析头部
    HTTP_CODE parse_content( char* text );      // 解析正文
    HTTP_CODE do_request();     // 分析目标文件
    char* get_line() { return m_read_buf + m_start_line; }  // 得到行的起始地址
    LINE_STATUS parse_line();   // 解析得到一行数据

    /**** 下面一组函数由process_write()调用以填充HTTP响应 ****/
    void unmap();   // 对内存映射区执行munmap操作
    bool add_response(const char* format, ...);     // 构造HTTP相应内容
    bool add_content(const char* content);  // 添加正文内容
    bool add_status_line(int status, const char* title);    // 添加状态行
    bool add_headers(int content_length);   // 添加头部
    bool add_content_length(int content_length);    // 添加Content-Length字段到头部
    bool add_linger();  // 添加Connection字段到头部
    bool add_blank_line();  // 添加一个空行

public:
    static struct event_base* base;
    static int m_user_count;    // 统计用户数量

private:
    int m_sockfd;               // 该HTTP连接的socket
    sockaddr_in m_address;      // 对方的socket地址
    struct event* read_ev;      // 读事件处理器
    struct event* write_ev;     // 可写事件处理器

    char m_read_buf[READ_BUFFER_SIZE];    // 读缓冲区
    int m_read_idx;     // 标识读缓冲区中已经读入数据的最后一个字节的下一个位置
    int m_checked_idx;  // 当前正在分析的字符在读缓冲区中的位置
    int m_start_line;   // 当前正在解析的行的起始地址
    char m_write_buf[WRITE_BUFFER_SIZE];    // 写缓冲区
    int m_write_idx;    // 写缓冲区待发送的字节数

    CHECK_STATE m_check_state;      // 主状态机当前所处的状态
    METHOD m_method;    // HTTP请求方法

    char m_real_file[FILENAME_LEN];     // 客户请求目标文件的完整路径
    char* m_url;    // 请求文件名
    char* m_version;    // HTTP版本
    char* m_host;       // 主机名
    int m_content_length;   // 正文长度
    bool m_linger;      // HTTP请求是否要求保持连接

    char* m_file_address;   // 客户请求的目标文件被mmap到内存中的起始地址
    struct stat m_file_stat;    // 目标文件的状态
    struct iovec m_iv[2];   // 用于writev写操作
    int m_iv_count;
};

#endif

