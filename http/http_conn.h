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
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"

class http_conn
{
public:
	//设置读取文件的名称m_real_file大小
    static const int FILENAME_LEN = 200;
	//设置读缓冲区m_read_buf大小
    static const int READ_BUFFER_SIZE = 2048;
	//设置写缓冲区m_write_buf大小
    static const int WRITE_BUFFER_SIZE = 1024;
	//报文的请求方法，本项目只用到GET和POST
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
	//主状态机的状态
    enum CHECK_STATE
    {
		//当前正在分析请求行
        CHECK_STATE_REQUESTLINE = 0,
		//当前正在分析请求头
        CHECK_STATE_HEADER,
		//当前正在分析请求正文
        CHECK_STATE_CONTENT
    };
	//报文解析的结果
    enum HTTP_CODE
    {
		//请求不完整，需要继续读取客户数据
        NO_REQUEST,
		//获得一个完整的客户请求
        GET_REQUEST,
		//客户请求有语法错误
        BAD_REQUEST,
		//无资源
        NO_RESOURCE,
		//客户对资源没有足够的访问权限
        FORBIDDEN_REQUEST,
		//文件请求
        FILE_REQUEST,
		//服务器内部出错
        INTERNAL_ERROR,
		//客户端已经关闭连接
        CLOSED_CONNECTION
    };
	//从状态机的状态
    enum LINE_STATUS
    {
		//读取到一个完整的行
        LINE_OK = 0,
		//行出错
        LINE_BAD,
		//行尚不完整
        LINE_OPEN
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
	//初始化套接字地址，函数内部会调用私有方法init
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname);
    //关闭http连接
	void close_conn(bool real_close = true);
    //主从状态机 报文解析
	void process();//这就是对数据的处理方式
    //读取浏览器端发来的全部数据
	bool read_once();
    //响应报文写入函数
	bool write();
    sockaddr_in *get_address()
    {
        return &m_address;
    }
	//同步线程初始化数据库读取表
    void initmysql_result(connection_pool *connPool);
    int timer_flag;
    int improv;


private:
    void init();
	//从m_read_buf读取，并处理请求报文
    HTTP_CODE process_read();
	//向m_write_buf写入响应报文数据
    bool process_write(HTTP_CODE ret);
	//从状态机，用于解析出一行内容
    HTTP_CODE parse_request_line(char *text);
	//解析头
    HTTP_CODE parse_headers(char *text);
	//解析正文
    HTTP_CODE parse_content(char *text);
	//请求
    HTTP_CODE do_request();
	//读取行
    char *get_line() { return m_read_buf + m_start_line; };
	//解析行
    LINE_STATUS parse_line();
    void unmap();
	//添加响应
    bool add_response(const char *format, ...);
	//添加正文
    bool add_content(const char *content);
	//添加状态行
    bool add_status_line(int status, const char *title);
	//添加头
    bool add_headers(int content_length);
	//添加正文类型
    bool add_content_type();
	//添加正文长度
    bool add_content_length(int content_length);
	//选择长连接还是短连接
    bool add_linger();
	//添加空行
    bool add_blank_line();

public:
    static int m_epollfd;
    static int m_user_count;
    MYSQL *mysql;
    int m_state;  //读为0, 写为1

private:
    int m_sockfd;//该HTTP连接的socket
    sockaddr_in m_address;//通信的socket地址
	
	
	//读缓冲区，存储读取的请求报文数据
    char m_read_buf[READ_BUFFER_SIZE];
	
	//缓冲区中m_read_buf中数据的最后一个字节的下一个位置
    int m_read_idx;
	//当前正在分析的字符在读缓冲区的位置
	int m_checked_idx;
	//当前正在解析的行的起始位置
    int m_start_line;
	
	//存储发出的响应报文数据
	char m_write_buf[WRITE_BUFFER_SIZE];
	
	//指示buffer中的长度
	int m_write_idx;

	//主状态机当前所处的状态
    CHECK_STATE m_check_state;
	//请求方法
    METHOD m_method;

	//以下为解析请求报文中对应的6个变量
	//存储读取文件的名称
    char m_real_file[FILENAME_LEN];
    char *m_url;      //请求目标文件的文件名
    char *m_version;  //协议版本，只支持HTTP1.1
    char *m_host;
    int m_content_length;//HTTP请求的消息总长度
    bool m_linger;    //HTTP请求是否要保持连接
    char *m_file_address;//读取服务器上的文件地址
    struct stat m_file_stat;

	//因为我们写的服务器响应请求体和其他三者在两块内存中，所以定义大小为2的数组
    struct iovec m_iv[2];//io向量机制iovec
    int m_iv_count;
    int cgi;        //是否启用的POST
    char *m_string; //存储请求头数据
    int bytes_to_send;//剩余发送字节数，也就是将要发送的字节
    int bytes_have_send;//已发送的字节数
    char *doc_root;//请求资源的根路径，也就是项目的根路径(不是服务器项目，是网站项目)

    map<string, string> m_users;
    int m_TRIGMode;
    int m_close_log;

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};

#endif
