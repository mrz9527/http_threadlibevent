#include "http_conn.h"

/**** HTTP响应内容 ****/
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";
// 资源根目录
const char* doc_root = "/home/bochen";

/**
 * 设置为非阻塞
*/
int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

/**** 初始化静态变量 ****/
int http_conn::m_user_count = 0;
struct event_base* http_conn::base = nullptr;

void http_conn::close_conn()
{
    if(m_sockfd != -1)
    {
        m_sockfd = -1;
        m_user_count--;
        // 释放资源，关闭连接
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
        close(m_sockfd);
    }
    init();
}

/**
 * 初始化http_conn
 * sockfd：处理的客户端socket
 * addr：客户端地址
 * rev：读事件处理器
 * wev：可写事件处理器
*/
void http_conn::init(int sockfd, const sockaddr_in& addr, struct event* rev, struct event* wev)
{
    m_sockfd = sockfd;
    m_address = addr;
    // 记录事件处理器
    if (read_ev != nullptr)
    {
        event_free(read_ev);
        read_ev = nullptr;
    }
    read_ev = rev;
    if (write_ev != nullptr)
    {
        event_free(write_ev);
        write_ev = nullptr;
    }
    write_ev = wev;

    int error = 0;
    socklen_t len = sizeof(error);
    getsockopt(m_sockfd, SOL_SOCKET, SO_ERROR, &error, &len);

    // 注册读事件处理器
    event_add(read_ev, NULL);
    // 设置为非阻塞
    setnonblocking(m_sockfd);
    m_user_count++;

    init();
}

void http_conn::init()
{
    // 初始状态为解析请求行
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;

    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

/**
 * 从状态机，解析得到一行数据
*/
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for ( ; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        // '\r'和'\n'紧接
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r')
        {
            if ((m_checked_idx + 1) == m_read_idx)
            {
                return LINE_OPEN;
            }
            else if (m_read_buf[m_checked_idx + 1] == '\n') // 行数据完整
            {
                // 换行符置'\0'，后续可根据'\0'快速找到行尾
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }

            return LINE_BAD;
        }
        else if (temp == '\n')
        {
            if ((m_checked_idx > 1) && (m_read_buf[m_checked_idx - 1] == '\r'))
            {
                m_read_buf[m_checked_idx-1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }

    // 行数据尚不完整
    return LINE_OPEN;
}

bool http_conn::read()
{
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }

    int bytes_read = 0;
    while (true)    // 非阻塞读
    {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)     // TCP读缓冲区为空，等待下一次可读事件
            {
                break;
            }
            // 其他错误，读取失败
            return false;
        }
        else if (bytes_read == 0)   // 对方关闭连接
        {
            return false;
        }

        m_read_idx += bytes_read;
    }
    return true;
}

/**
 * 解析HTTP请求行，获得请求方法、目标URI、HTTP版本号
*/
http_conn::HTTP_CODE http_conn::parse_request_line(char* text)
{
    // 找到第一个空格，其分隔请求方法和URI
    m_url = strpbrk(text, " \t");
    if (!m_url)
    {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';

    char* method = text;
    // 本服务器目前只支持"GET"方法
    if (strcasecmp(method, "GET") == 0)
    {
        m_method = GET;
    }
    else
    {
        return BAD_REQUEST;
    }

    // 跳过剩余空格
    m_url += strspn(m_url, " \t");

    // 后续解析类似上面
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
    {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }

    /*if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }*/

    if (!m_url || m_url[0] != '/')
    {
        return BAD_REQUEST;
    }

    // 下一状态为解析头部
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

/**
 * 解析HTTP请求的一个头部信息
*/
http_conn::HTTP_CODE http_conn::parse_headers(char* text)
{
    if (text[0] == '\0')    // 空行
    {
        if (m_content_length != 0)    // 有正文
        {
            // 下一状态为解析正文
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 没有正文，解析完成
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0) // 解析Connection选项
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    }
    else if (strncasecmp(text, "Content-Length:", 15) == 0) // 解析Content-Length选项
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0)    // 解析Host选项
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else    // 其他头部选项不解析
    {
        printf("unknow header %s\n", text);
    }

    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_content(char* text)
{
    // 没有真正解析HTTP请求的消息体，只是判断它是否被完整地读入
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }

    return NO_REQUEST;
}

/**
 * 主状态机，解析HTTP请求
*/
http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;

    while (((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK))
                || ((line_status = parse_line()) == LINE_OK))
    {
        // 获取要解析的行
        text = get_line();
        m_start_line = m_checked_idx;
        printf("got 1 http line: %s\n", text);

        // 根据解析状态分别处理
        switch (m_check_state)
        {
            case CHECK_STATE_REQUESTLINE:
            {
                ret = parse_request_line(text);
                if (ret == BAD_REQUEST)
                {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:
            {
                ret = parse_headers(text);
                if (ret == BAD_REQUEST)
                {
                    return BAD_REQUEST;
                }
                else if (ret == GET_REQUEST)    // 没有正文，分析目标文件
                {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                ret = parse_content(text);
                if (ret == GET_REQUEST)     // 得到完整的HTTP请求，分析目标文件
                {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default:
            {
                return INTERNAL_ERROR;
            }
        }
    }

    return NO_REQUEST;
}

/**
 * 当得到一个完整、正确的HTTP请求时，分析目标文件的属性
*/
http_conn::HTTP_CODE http_conn::do_request()
{
    // 构造完整路径
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    // 获得文件属性
    if (stat(m_real_file, &m_file_stat) < 0)    // 文件不存在
    {
        return NO_RESOURCE;
    }

    if (!(m_file_stat.st_mode & S_IROTH))   // 没有可读权限
    {
        return FORBIDDEN_REQUEST;
    }

    if (S_ISDIR(m_file_stat.st_mode))   // 请求的是文件夹
    {
        return BAD_REQUEST;
    }

    // 目标文件存在且合法，使用mmap将其映射到内存地址m_file_address处
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = nullptr;
    }
}

bool http_conn::write()
{
    int temp = 0;
    int bytes_have_send = 0;
    int bytes_to_send = m_write_idx;
    if (bytes_to_send == 0)
    {
        // 清除状态
        init();
        // 注销可写事件，重新注册读事件
        event_del(write_ev);
        event_add(read_ev, NULL);
        return true;
    }

    // 非阻塞写，使用writev，仍未处理只写一半的情况
    while (true)
    {
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp <= -1)
        {
            if (errno == EAGAIN)     // TCP写缓存区已满，等待下一次可写事件
            {
                return true;
            }
            // 其他错误，写失败
            unmap();
            return false;
        }

        // bytes_to_send -= temp;
        bytes_have_send += temp;
        if (bytes_to_send <= bytes_have_send)     // 发送HTTP相应成功
        {
            unmap();
            if (m_linger)   // 长连接
            {
                // 清除状态
                init();
                // 注销可写事件，重新注册读事件
                event_del(write_ev);
                event_add(read_ev, NULL);
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}

bool http_conn::add_response(const char* format, ...)
{
    if (m_write_idx >= WRITE_BUFFER_SIZE)
    {
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    // 将可变参数格式化输出到一个字符数组
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool http_conn::add_status_line(int status, const char* title)
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
    return add_response("Content-Length: %d\r\n", content_len);
}

bool http_conn::add_linger()
{
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

bool http_conn::add_content(const char* content)
{
    return add_response("%s", content);
}

/**
 * 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
*/
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
        case INTERNAL_ERROR:    // 内部错误
        {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form))
            {
                return false;
            }
            break;
        }
        case BAD_REQUEST:   // 请求格式有错
        {
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if (!add_content(error_400_form))
            {
                return false;
            }
            break;
        }
        case NO_RESOURCE:   // 请求资源不存在
        {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form))
            {
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST:     // 请求资源禁止访问
        {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form))
            {
                return false;
            }
            break;
        }
        case FILE_REQUEST:  // 请求资源合法
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
                return true;
            }
            else
            {
                const char* ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string))
                {
                    return false;
                }
            }
        }
        default:
        {
            return false;
        }
    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

/**
 * 有线程池中的工作线程调用，这是处理HTTP请求的入口函数
*/
void http_conn::process()
{
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST)   // 没有读到完整行，返回等待剩余数据
    {
        return;
    }

    // 构造HTTP回复报文
    bool write_ret = process_write(read_ret);
    if (!write_ret)     // 构建失败，关闭连接，释放资源
    {
        close_conn();
        return;
    }

    // 注销读事件，注册可写事件
    event_del(read_ev);
    event_add(write_ev, NULL);
}

