#ifndef __TCP_SERVER_FRAMEWORK_H__
#define __TCP_SERVER_FRAMEWORK_H__

/* include system header files */
#include <stdio.h>

/* marco definitions */
#define IP_ADDR_LEN    (16)
#define INVALID_TCP_SERVER_HANDLE ((void*)NULL)

/* structure or function pointer definitions */
typedef void* TCP_SERVER_HANDLE;

typedef struct
{
	int fd;
	char *p_data;
	int data_len;
	void *p_user_data;
} recv_data_param;

typedef void (*process_recv_data_func)(recv_data_param *p_recv_data_param);

typedef struct
{
	char ip_addr[IP_ADDR_LEN];
	int port;
	process_recv_data_func process_func;
	void *p_user_data;
} listen_addr;


/* function declarations */
extern TCP_SERVER_HANDLE start_tcp_server(listen_addr *p_listen_addr, int listen_addr_num);
extern void stop_tcp_server(TCP_SERVER_HANDLE handle);

#endif /* __TCP_SERVER_FRAMEWORK_H__ */
