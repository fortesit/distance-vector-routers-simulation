#define ENABLE_COLOUR 1
#define MAX_STRLEN 256
#define MAX_ROUTER 100

#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <dirent.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

#include <fcntl.h>
#include <pthread.h>

/* Router Information Data Structure */
typedef struct
{
	int link_ID; /* in order to reach that router, we go to router[link_ID] */
				/* Not directly connected = 0 */
	char router_IP[64];
	int command_port;
	int router_port;
} Router;

/* Message Structures */
#pragma pack(push,1)
typedef struct
{
	int type;
	int destination;
	int weight;
} A2Rmsg_type;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct
{
	int type;
	int end;
	int destination;
	int next_hop;
	int path_cost;
} R2Amsg_type;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct
{
	int my_ID;
	int DV[MAX_ROUTER+1];
} R2Rmsg_type;
#pragma pack(pop)

/* Common Functions */
void print_error(char str[])
{
	#if ENABLE_COLOUR
		fprintf(stdout,"\033[1;31mERROR: \033[1;30m%s\n\033[0m",str);
	#else
		fprintf(stdout,"ERROR: %s\n",str);
	#endif
}

      int sendn(int sd, const void* buf, int buf_len, int useless)
        {
                int n_left = buf_len;         // actual data bytes sent
                int n;
                while (n_left > 0)
                {
                        if ((n = send(sd, buf + (buf_len - n_left), n_left, 0)) < 0)
                        {
                                if (errno == EINTR)
                                        n = 0;
                                else
                                        return -1;
                        }
                        else if (n == 0)
                        {
                                return 0;
                        }
                        n_left -= n;
                }
                return buf_len;
        }


        int recvn(int sd, void* buf, int buf_len, int useless)
        {
                int n_left = buf_len;
                int n = 0;
                while (n_left > 0)
                {
                        if ((n = recv(sd, buf + (buf_len - n_left), n_left, 0)) < 0)
                        {
                                if (errno == EINTR)
                                        n = 0;
                                else
                                        return -1;
                        }
                        else if (n == 0)
                        {
                                        return 0;
                        }
                                        n_left -= n;
                }
                return buf_len;
        }
