#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <pthread.h>
using namespace std;

Log::Log()
{
    m_count = 0;
    m_is_async = false;
}

Log::~Log()
{
    if (m_fp != NULL)
    {
        fclose(m_fp);
    }
}
//异步需要设置阻塞队列的长度，同步不需要设置 ./log
bool Log::init(const char *file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size)
{
    //如果设置了max_queue_size,则设置为异步
    if (max_queue_size >= 1)
    {
        m_is_async = true;
        m_log_queue = new block_queue<string>(max_queue_size);
        pthread_t tid;
        //flush_log_thread为回调函数,这里表示创建线程异步写日志
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }
    
    m_close_log = close_log;//关闭日志
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);
    m_split_lines = split_lines;

    time_t t = time(NULL);//获取当前时间，时间戳
	/*localtime函数
         将时间数值变换成本地时间，考虑到本地时区和夏令时标志;
    原型: struct tm *localtime(const time_t * calptr); 
    头文件 <time.h>
　返回值:
        成功: struct tm *结构体, 原型如下:
            struct tm {
					int tm_sec;       秒 – 取值区间为[0,59] 
					int tm_min;       //分 - 取值区间为[0,59] 
					int tm_hour;      //时 - 取值区间为[0,23]
					int tm_mday;	  //一个月中的日期 - 取值区间为[1,31] 
					int tm_mon;       //月份（从一月开始，0代表一月） - 取值区间为[0,11] 
					int tm_year;      //年份，其值等于实际年份减去1900
					int tm_wday;      //星期 – 取值区间为[0,6]，其中0代表星期天，1代表星期一
					int tm_yday;      //从每年1月1日开始的天数– 取值区间[0,365]，其中0代表1月1日
					int tm_isdst;     //夏令时标识符，夏令时tm_isdst为正；不实行夏令时tm_isdst为0
			}; */
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

	/*char * strrchr(const char *str, int c);
	功能：strrchr() 将会找出 str 字符串中最后一次出现的字符 c 的地址，然后将该地址返回。
	*/
    const char *p = strrchr(file_name, '/');
    char log_full_name[256] = {0};
	/*若输入的文件名没有/，则直接将时间+文件名作为日志名*/
    if (p == NULL)
    {
		/*int snprintf(char *str, size_t size, const char *format, ...) 
		设将可变参数(...)按照 format 格式化成字符串，并将字符串复制到 str 中，
		size 为要写入的字符的最大数目，超过 size 会被截断。*/
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else
    {
		/*将/的位置向后移动一个位置，然后复制到logname中
		p - file_name + 1是文件所在路径文件夹的长度
		dirname相当于./    */
        strcpy(log_name, p + 1);//赋值日志的名字
        strncpy(dir_name, file_name, p - file_name + 1);//赋值路径的名字
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }   

    m_today = my_tm.tm_mday;//记录当前是哪一天
    
    m_fp = fopen(log_full_name, "a");
    if (m_fp == NULL)
    {
        return false;
    }

    return true;
}

void Log::write_log(int level, const char *format, ...)
{
    struct timeval now = {0, 0};
	//这个函数会把时间包装为一个结构体返回。包括秒，微妙
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};
    switch (level)
    {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[erro]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }
    //写入一个log，对m_count++, m_split_lines最大行数
    m_mutex.lock();
    m_count++;
	//日志不是今天或写入的日志行数是最大行的倍数，超行处理，超行可能导致创建多个文件
    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) //everyday log
    {
        
        char new_log[256] = {0};
        fflush(m_fp);
        fclose(m_fp);
        char tail[16] = {0};
       
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);
       
        if (m_today != my_tm.tm_mday)
        {
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }
        else
        {
			//超行的话创建的新文件名字为xxx1,xxx2,xxx3 ...
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a");
    }
 
    m_mutex.unlock();

    va_list valst;
    va_start(valst, format);//获取可变参数列表的第一个参数的地址
	//format就是print输出的一堆%什么玩意
    string log_str;
    m_mutex.lock();

    //写入的具体时间内容格式
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    
    int m = vsnprintf(m_buf + n, m_log_buf_size - 1, format, valst);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;

    m_mutex.unlock();
	//异步并且队列未满写入队列
    if (m_is_async && !m_log_queue->full())
    {
        m_log_queue->push(log_str);
    }
	//同步或者队列满了都直接写入文件
    else
    {
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }
	//清空valst
    va_end(valst);
}

void Log::flush(void)
{
    m_mutex.lock();
    //强制刷新写入流缓冲区
    fflush(m_fp);
    m_mutex.unlock();
}
