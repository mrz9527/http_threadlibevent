#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <event.h>
#include <thread.h>
#include <pthread.h>

#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"

#define MAX_FD 65536

// 全局变量
threadpool< http_conn >* pool = nullptr;    // 线程池对象
http_conn* users = nullptr;     // 任务类集合

/**
 * 向客户端发送错误信息
*/
void show_error(int connfd, const char* info)
{
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
}

/**
 * HTTP请求到来事件回调函数
*/
void httprequest_cb(int fd, short events, void* arg)
{
    if (users[fd].read())   // 读取到数据，进行HTTP请求分析
    {
        pool->append(users + fd);
    }
    else        // 读取失败，关闭连接，释放资源
    {
        users[fd].close_conn();
    }
}

/**
 * 可写事件回调函数
*/
void abletowrite_cb(int fd, short events, void* arg)
{
    if (!users[fd].write())     // 写HTTP响应
    {
        // 写失败，关闭连接，释放资源
        users[fd].close_conn();
    }
}

/**
 * 新连接到来处理函数
*/
void accept_cb(int listenfd, short events, void* arg)
{
    evutil_socket_t sockfd;
    struct sockaddr_in client;
    socklen_t len = sizeof(client);
 
    // 接受客户连接
    sockfd = accept(listenfd, (struct sockaddr*)&client, &len);
    if (sockfd < 0)
    {
        printf("errno is: %d\n", errno);
        return;
    }
    if (http_conn::m_user_count >= MAX_FD )
    {
        show_error(sockfd, "Internal server busy");
        close(sockfd);
        return;
    }
 
    // 为新客户连接创建读写事件处理器
    struct event_base* base = (event_base*)arg;
    struct event *read_ev = event_new(NULL, -1, 0, NULL, NULL);
    event_assign(read_ev, base, sockfd, EV_READ | EV_ET | EV_PERSIST,
                 httprequest_cb, (void*)read_ev);
    struct event *write_ev = event_new(NULL, -1, 0, NULL, NULL);
    event_assign(write_ev, base, sockfd, EV_WRITE | EV_ET | EV_PERSIST,
                 abletowrite_cb, (void*)write_ev);
    
    // 初始化http_conn
    users[sockfd].init(sockfd, client, read_ev, write_ev);
    
}

int main(int argc, char* argv[])
{
    if( argc <= 2 )
    {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }
    const char* ip = argv[1];
    int port = atoi(argv[2]);

    // 启动libevent多线程机制
    evthread_use_pthreads();

    try
    {
        pool = new threadpool< http_conn >;
    }
    catch( ... )
    {
        return 1;
    }

    // 预先为每一个可能的客户连接分配一个http_conn对象
    users = new http_conn[MAX_FD];
    assert(users);


    /**** 创建服务器 ****/

    struct event_base* base = event_base_new();
    assert(base != nullptr);
    http_conn::base = base;
    evthread_make_base_notifiable(base);

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);
    // 设置断开连接方式为RST
    struct linger tmp = { 1, 0 };
    setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof( address ));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    assert( ret >= 0 );

    ret = listen(listenfd, 5);
    assert(ret >= 0);

    // 忽略SIGPIPE信号
    struct event* ev_sigpipe = event_new(base, SIGPIPE, EV_SIGNAL | EV_PERSIST, nullptr, nullptr);
    event_add(ev_sigpipe, NULL);

    // 为listenfd注册永久读事件
    struct event* ev_listen = event_new(base, listenfd, EV_READ | EV_ET | EV_PERSIST, accept_cb, base);
    event_add(ev_listen, NULL);

    // 开始事件循环
    event_base_dispatch(base);

    close(listenfd);
    delete [] users;
    delete pool;
    return 0;
}

