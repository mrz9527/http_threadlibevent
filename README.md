http_threadlibevent
=

## Introduction
基于libevent网络库和线程池实现的支持高并发的http服务器，提供对HTTP请求头部的解析并根据解析结果返回HTTP应答  

threadpool：使用模板实现的线程池类，其实现与具体的业务无关，配合其他任务类可用于实现其他服务器。主线程和工作线程通过共享一个请求队列进行任务交互  
http_conn：HTTP请求处理任务类，内部使用状态机模式进行HTTP请求分析，并根据分析结果构造HTTP应答返回给客户端  

## Usage
cd src/  
make  
