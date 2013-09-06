#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/poll.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <malloc.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <fcntl.h>
#include <pthread.h>
#include "tcp_server_framework.h"

/* marco definitions */
#define LISTEN_SOCKET_BACKLOG_NUM    (512)
#define MAX_FD_NUM                   (FD_SETSIZE)
#define RECV_BUF_SIZE                (4096)

#define QUIT_COMMAND                 'Q'
#define DONE_COMMAND                 'D'

/* enum definitions */
typedef enum
{
	FALSE = 0,
	TRUE = 1
} BOOL;

/* structure or function pointer definitions */
typedef struct _fd_list_node
{
	int fd;
	struct _fd_list_node *p_next_node;
} fd_list_node;


typedef struct
{
	BOOL tcp_server_exit;
	
	int manage_socket_fd[2];

	int listen_addr_num;
	listen_addr *p_listen_addr;
	int *p_listen_socket_fd;
	
	struct pollfd *p_poll_fds;
	
	pthread_mutex_t mutex;
	fd_list_node *p_conn_socket_list;
} tcp_server_context;

typedef struct
{
	int fd;
	int index;
	tcp_server_context *p_tcp_server_context;
} recv_data_thread_context;

/* variable definitions */

/* function declarations */
static tcp_server_context *init_tcp_server_context(listen_addr *p_listen_addr, int listen_addr_num);
static void *tcp_server_thread(void *p_param);
static int create_listen_socket(listen_addr *p_listen_addr);
static void *recv_data_thread(void *p_param);

/* function definitions */
TCP_SERVER_HANDLE start_tcp_server(listen_addr *p_listen_addr, int listen_addr_num)
{
	/* local variables definitions */
	tcp_server_context *p_tcp_server_context = NULL;
	pthread_t thread_id = 0;
	
	/* code body */
	p_tcp_server_context = init_tcp_server_context(p_listen_addr, listen_addr_num);
	if ((NULL != p_tcp_server_context) &&
		(0 == pthread_create(&thread_id, NULL, tcp_server_thread, p_tcp_server_context)))
	{
		pthread_detach(thread_id);
		return (TCP_SERVER_HANDLE)p_tcp_server_context;
	}
	else
	{
		return (TCP_SERVER_HANDLE)NULL;
	}
}


void stop_tcp_server(TCP_SERVER_HANDLE handle)
{
	/* local variables definitions */
	tcp_server_context *p_tcp_server_context = (tcp_server_context*)handle;
	int n = 0;
	char data = QUIT_COMMAND;
	
	/* code body */
	if (NULL != p_tcp_server_context)
	{
		n = send(p_tcp_server_context->manage_socket_fd[0], &data, sizeof(data), 0);
		if (n > 0)
		{
			recv(p_tcp_server_context->manage_socket_fd[0], &data, sizeof(data), 0);
		}
	}

	pthread_mutex_destroy(&(p_tcp_server_context->mutex));
	
	close(p_tcp_server_context->manage_socket_fd[0]);
	close(p_tcp_server_context->manage_socket_fd[1]);

	free(p_tcp_server_context->p_listen_addr);
	free(p_tcp_server_context->p_listen_socket_fd);
	free(p_tcp_server_context);
}


static tcp_server_context *init_tcp_server_context(listen_addr *p_listen_addr, int listen_addr_num)
{
	/* local variables definitions */
	int i = 0;
	tcp_server_context *p_tcp_server_context = NULL;

	/* code body */
	p_tcp_server_context = calloc(1, sizeof(*p_tcp_server_context));
	if (NULL == p_tcp_server_context)
	{
		goto INIT_TCP_SERVER_CONTEXT_ERR_END;
	}
	
	p_tcp_server_context->tcp_server_exit = FALSE;
	
	p_tcp_server_context->p_listen_socket_fd = calloc(listen_addr_num, sizeof(int));
	if (NULL == p_tcp_server_context->p_listen_socket_fd)
	{
		goto INIT_TCP_SERVER_CONTEXT_ERR_END;
	}

	p_tcp_server_context->p_listen_addr = calloc(listen_addr_num, sizeof(*p_listen_addr));
	if (NULL == p_tcp_server_context->p_listen_addr)
	{
		goto INIT_TCP_SERVER_CONTEXT_ERR_END;
	}

	p_tcp_server_context->p_poll_fds = calloc((listen_addr_num + 1), sizeof(*p_tcp_server_context->p_poll_fds));
	if (NULL == p_tcp_server_context->p_poll_fds)
	{
		goto INIT_TCP_SERVER_CONTEXT_ERR_END;
	}

	p_tcp_server_context->listen_addr_num = listen_addr_num;

	memcpy(p_tcp_server_context->p_listen_addr, p_listen_addr, sizeof(*p_listen_addr) * listen_addr_num);
	if (NULL == p_tcp_server_context->p_listen_addr)
	{
		goto INIT_TCP_SERVER_CONTEXT_ERR_END;
	}

	for (i = 0; i < p_tcp_server_context->listen_addr_num; i++)
	{
		p_tcp_server_context->p_listen_socket_fd[i] = create_listen_socket(&(p_tcp_server_context->p_listen_addr[i]));
		if (-1 == p_tcp_server_context->p_listen_socket_fd[i])
		{
			goto INIT_TCP_SERVER_CONTEXT_ERR_END;
		}

		p_tcp_server_context->p_poll_fds[i].fd = p_tcp_server_context->p_listen_socket_fd[i];
		p_tcp_server_context->p_poll_fds[i].events = POLLIN;
	}

	if (-1 == socketpair(AF_UNIX, SOCK_STREAM, 0, p_tcp_server_context->manage_socket_fd))
	{
		goto INIT_TCP_SERVER_CONTEXT_ERR_END;
	}
	else
	{
		p_tcp_server_context->p_poll_fds[p_tcp_server_context->listen_addr_num].fd = p_tcp_server_context->manage_socket_fd[1];
		p_tcp_server_context->p_poll_fds[p_tcp_server_context->listen_addr_num].events = POLLIN;
	}
	
	if (0 != pthread_mutex_init(&(p_tcp_server_context->mutex), NULL))
	{
		goto INIT_TCP_SERVER_CONTEXT_ERR_END;
	}
	return p_tcp_server_context;
INIT_TCP_SERVER_CONTEXT_ERR_END:
	return NULL;
}


static void *tcp_server_thread(void *p_param)
{
	/* local variables definitions */
	int j = 0;
	int n = 0;
	int ret_val = 0;
	char data = 0;
	fd_list_node *p_fd_list_node = NULL;
	pthread_t thread_id = 0;
	recv_data_thread_context *p_recv_data_thread_context = NULL;
	tcp_server_context *p_tcp_server_context = (tcp_server_context*)p_param;
	
	/* code body */
	while (1)
	{
		ret_val = poll(
					p_tcp_server_context->p_poll_fds,
					(p_tcp_server_context->listen_addr_num + 1),
					(-1));
		if (ret_val > 0)
		{
			for (j = 0; j < p_tcp_server_context->listen_addr_num; j++)
			{
				if (p_tcp_server_context->p_poll_fds[j].revents & POLLIN)
				{
					p_fd_list_node = calloc(1, sizeof(*p_fd_list_node));
					p_fd_list_node->fd = accept(p_tcp_server_context->p_listen_socket_fd[j], NULL, NULL);

					if (0 != pthread_mutex_lock(&(p_tcp_server_context->mutex)))
					{
					}
					else
					{
						p_fd_list_node->p_next_node = p_tcp_server_context->p_conn_socket_list;
						p_tcp_server_context->p_conn_socket_list = p_fd_list_node;

						p_recv_data_thread_context = calloc(1, sizeof(*p_recv_data_thread_context));
						if (NULL == p_recv_data_thread_context)
						{
						}
						else
						{
							p_recv_data_thread_context->fd = p_fd_list_node->fd;
							p_recv_data_thread_context->index = j;
							p_recv_data_thread_context->p_tcp_server_context = p_tcp_server_context;
							if (0 == pthread_create(&thread_id, NULL, recv_data_thread, p_recv_data_thread_context))
							{
								pthread_detach(thread_id);
							}
							else
							{
							}
						}
						pthread_mutex_unlock(&(p_tcp_server_context->mutex));
					}
				}
			}
			if (p_tcp_server_context->p_poll_fds[p_tcp_server_context->listen_addr_num].revents & POLLIN)
			{
				n = recv(p_tcp_server_context->manage_socket_fd[1], &data, sizeof(data), 0);
				break;
			}
		}
		else
		{
			break;
		}
	}

	for (j = 0; j < p_tcp_server_context->listen_addr_num; j++)
	{
		close(p_tcp_server_context->p_listen_socket_fd[j]);
	}
	
	if (0 != pthread_mutex_lock(&(p_tcp_server_context->mutex)))
	{
	}
	else
	{
		p_tcp_server_context->tcp_server_exit = TRUE;

		p_fd_list_node = p_tcp_server_context->p_conn_socket_list;
		
		/* There is no receive data thread */
		if (NULL == p_fd_list_node)
		{
			data = DONE_COMMAND;
			send(p_tcp_server_context->manage_socket_fd[1], &data, sizeof(data), 0);
		}
		else
		{
			while (NULL != p_fd_list_node)
			{
				shutdown(p_fd_list_node->fd, SHUT_RD);
				p_fd_list_node = p_fd_list_node->p_next_node;
			}
		}
		
		pthread_mutex_unlock(&(p_tcp_server_context->mutex));
	}
	
	return NULL;
}


static int create_listen_socket(listen_addr *p_listen_addr)
{
	/* local variables definitions */
	int socket_fd = -1;
	int resue_addr = 1;
	struct sockaddr_in addr = {0};

	/* code body */
	socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (-1 == socket_fd)
	{
		goto CREATE_LISTEN_SOCKET_ERR_END;
	}

	if (-1 == setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &resue_addr, sizeof(resue_addr)))
	{
		goto CREATE_LISTEN_SOCKET_ERR_END;
	}

	addr.sin_family = AF_INET;
	addr.sin_port = htons(p_listen_addr->port);
	if (0 != p_listen_addr->ip_addr[0])
	{
		if (inet_aton(p_listen_addr->ip_addr, &(addr.sin_addr)) == 0)
		{
			goto CREATE_LISTEN_SOCKET_ERR_END;
		}
	}
	else
	{
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
	}

	if (-1 == bind(socket_fd, (struct sockaddr*)(&addr), sizeof(addr)))
	{
		goto CREATE_LISTEN_SOCKET_ERR_END;
	}

	if (-1 == listen(socket_fd, LISTEN_SOCKET_BACKLOG_NUM))
	{
		goto CREATE_LISTEN_SOCKET_ERR_END;
	}

	return socket_fd;
	
CREATE_LISTEN_SOCKET_ERR_END:
	return -1;
}


static void *recv_data_thread(void *p_param)
{
	/* local variables definitions */
	recv_data_thread_context *p_recv_data_thread_context = (recv_data_thread_context*)p_param;
	int recv_num = 0;
	int fd = p_recv_data_thread_context->fd;
	tcp_server_context *p_tcp_server_context = p_recv_data_thread_context->p_tcp_server_context;
	fd_list_node *p_pre_fd_list_node = NULL;
	fd_list_node *p_fd_list_node = NULL;
	char recv_buff[RECV_BUF_SIZE] = {0};
	char data = 0;

	/* code body */
	while ((recv_num = recv(fd, recv_buff, sizeof(recv_buff), 0)) > 0)
	{
	}
	
	if (0 != pthread_mutex_lock(&(p_tcp_server_context->mutex)))
	{
	}
	else
	{
		p_pre_fd_list_node = p_tcp_server_context->p_conn_socket_list;
		p_fd_list_node = p_tcp_server_context->p_conn_socket_list;
		
		if (NULL == p_fd_list_node)
		{
			/* error case */
		}
		else
		{
			while (NULL != p_fd_list_node)
			{
				if (p_fd_list_node->fd == fd)
				{
					close(p_fd_list_node->fd);
					if (p_pre_fd_list_node == p_fd_list_node)
					{
						p_tcp_server_context->p_conn_socket_list = p_pre_fd_list_node->p_next_node;
					}
					else
					{
						p_pre_fd_list_node->p_next_node = p_fd_list_node->p_next_node;
					}
					free(p_fd_list_node);
					break;
				}
				else
				{
					p_pre_fd_list_node = p_fd_list_node;
					p_fd_list_node = p_fd_list_node->p_next_node;
				}
			}

			if ((TRUE == p_tcp_server_context->tcp_server_exit) &&
				(NULL == p_tcp_server_context->p_conn_socket_list))
			{
				data = DONE_COMMAND;
				send(p_tcp_server_context->manage_socket_fd[1], &data, sizeof(data), 0);
			}
		}
		pthread_mutex_unlock(&(p_tcp_server_context->mutex));
	}
	return NULL;
}
