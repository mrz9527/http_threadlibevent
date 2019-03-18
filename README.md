http_libevent_threadpool
=

## Introduction
基于libevent网络库和线程池实现的支持高并发的http服务器，提供对HTTP请求头部的解析并根据解析结果返回HTTP应答  

threadpool：使用模板实现的线程池类，进行线程管理。其实现与具体的业务无关，配合其他任务类可用于实现其他服务器  
http_conn：HTTP请求处理任务类，进行HTTP请求分析并构造返回HTTP应答  

## Usage
cd src/  
make  
