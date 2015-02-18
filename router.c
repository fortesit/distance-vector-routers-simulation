#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "common.h"

#define INFINITY 10000

/* Global Variables Declaration */
Router *router;
int **DV_table;
int *my_DV_backup;
int *edge;
int *reverse_ID_map;

int DV_RECEIVED_SO_FAR = 0;

R2Amsg_type R2A_msg;
A2Rmsg_type A2R_msg;
R2Rmsg_type out_R2R_msg;
R2Rmsg_type in_R2R_msg;
int my_ID;

pthread_mutex_t stupid_show_welcome_message = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t protect = PTHREAD_MUTEX_INITIALIZER;

int ID_map(int real_ID)
{
	int i;
	for(i = 1; i <= MAX_ROUTER; i++)
		if(real_ID == reverse_ID_map[i])
			return i;
	return -1;
}

void safe_quit()
{
	int i;
	printf("\n\nFree-ing router table... ");
	free(router);
	printf("done!\n");
	printf("Free-ing DV_table... ");
	for(i = 0; i <= MAX_ROUTER; i++)
		free(DV_table[i]);
	free(DV_table);
	printf("done!\n");
	printf("Free-ing edge table... ");
	free(edge);
	printf("done!\n");
	printf("Thank you!\n");
	exit(0);
}

int next_hop_with_min_cost(int destination)
{
	int min_next_hop = destination;
	int min_cost = edge[destination];
	int i;

	// printf("edge[%d] = %d\n", destination, edge[destination]);
	for(i = 1; i <= MAX_ROUTER; i++)
	{
		if ( reverse_ID_map[i]!=-1 && i!=ID_map(my_ID) && edge[i]!=-1 && i!=destination )
		{
			/* Originally cannot access destination */
			if( min_cost==-1 )
			{
				if( DV_table[i][destination]!=-1 && DV_table[i][destination]<INFINITY)
				{
					min_cost = edge[i]+DV_table[i][destination];
					min_next_hop = i;
					// printf("DV_table[%d][%d] = %d, edge[%d] = %d\n",i,destination, DV_table[i][destination],i,edge[i]);
				}
			}
			/* Can now access destination, and really finding min */
			else
			{
				if( DV_table[i][destination]!=-1 && DV_table[i][destination]<INFINITY && DV_table[i][destination]+edge[i]<min_cost )
				{
					min_cost = DV_table[i][destination]+edge[i];
					min_next_hop = i;
					// printf("DV_table[%d][%d] = %d, edge[%d] = %d\n",i,destination, DV_table[i][destination],i,edge[i]);
				}
			}
		}
	}
	return min_next_hop;
}

void send_DVs_out()
{
		int router_socket;
		struct sockaddr_in servaddr;
		struct hostent *ht;
		int i;

	for(i = 1; i <= MAX_ROUTER; i++)
		if ( reverse_ID_map[i]!=-1 && edge[router[i].link_ID]==-1 )
		{
			DV_table[ID_map(my_ID)][i] = -1;
		}

		/* Prepare Message */
		out_R2R_msg.my_ID = htonl( my_ID );
		for(i = 0; i <= MAX_ROUTER; i++)
			out_R2R_msg.DV[i] = htonl( DV_table[ID_map(my_ID)][i] );

		for(i = 1; i <= MAX_ROUTER; i++)
		{
			if ( reverse_ID_map[i]!=-1 && edge[i]!=-1 && i!=ID_map(my_ID) )
			{
				/* Prepare Socket */
				if( router_socket = socket(AF_INET, SOCK_STREAM, 0), router_socket == -1 )
				{
					print_error("socket() failed");
					continue;
				}

				/* send out DV to each active neightbour */
				memset(&servaddr, 0, sizeof(servaddr));
				servaddr.sin_family = AF_INET;
				servaddr.sin_port = htons(router[i].router_port);
				if( ht = gethostbyname(router[i].router_IP), ht == NULL )
				{
					print_error("Unable to resolve target router's DNS");
					continue;
				}
				memcpy(&servaddr.sin_addr, ht->h_addr, ht->h_length);

				/* Establish Connection */
				if (connect(router_socket, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1)
				{
					print_error("connect() failed");
					continue;
				}

				/* send Message */
				sendn(router_socket, (char*)&out_R2R_msg, sizeof(R2Rmsg_type), 0);

				/* Close Socket */
				close(router_socket);
			}
		}
}

void* listening_thread_for_agent(void *message)
{
	int i;

	int servsd = -1;
	struct sockaddr_in servaddr;
	char one;

	struct sockaddr_in cliaddr;
	int clisd = -1;
	unsigned int cliaddrlen;

	if ((servsd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		print_error("socket() failed! (Agent Listening Thread)");
		exit(-1);
	}
	setsockopt(servsd, SOL_SOCKET, SO_REUSEADDR, (char*)&one, sizeof(one));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(router[ID_map(my_ID)].command_port);
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(servsd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < -1) {
		print_error("bind() failed! (Agent Listening Thread)");
		exit(-1);
	}
	if (listen(servsd, 512) == -1)
	{
		print_error("listen() failed! (Agent Listening Thread)");
		exit(-1);
	}
	cliaddrlen = sizeof(cliaddr);

	#if ENABLE_COLOUR
		printf("\033[1;32mRouter #%d: \033[1;30mListening on port %d for Agent-to-Router Communication\033[0m\n", my_ID, router[ID_map(my_ID)].command_port);
	#else
		printf("Router #%d: Listening on port %d for Agent-to-Router Communication\n", my_ID, router[ID_map(my_ID)].command_port);
	#endif

	pthread_mutex_unlock (&stupid_show_welcome_message);
	while(1)
	{
		if( clisd = accept(servsd, (struct sockaddr *)&cliaddr, &cliaddrlen), clisd != -1 )
		{
			#if ENABLE_COLOUR
				printf("\033[1;32mAgent\033[0m \"%s\" at port %u\n", inet_ntoa(cliaddr.sin_addr), cliaddr.sin_port);
			#else
				printf("Agent \"%s\" at port %u\n", inet_ntoa(cliaddr.sin_addr), cliaddr.sin_port);
			#endif
		}
		else
			print_error("accept() failed (Agent Listening Thread)");

		/* Receive Message */
		recvn(clisd, (char *)&A2R_msg, sizeof(A2Rmsg_type), 0);
		A2R_msg.type = ntohl( A2R_msg.type );
		A2R_msg.destination = ntohl( A2R_msg.destination );
		A2R_msg.weight = ntohl( A2R_msg.weight );

		//printf("Type: %d | Destination: %d | Weight: %d \n", A2R_msg.type, A2R_msg.destination, A2R_msg.weight);
		switch(A2R_msg.type)
		{
			/* dv */
			case 1:
					pthread_mutex_lock(&protect);
					/* Update DV table */
					for(i = 1; i <= MAX_ROUTER; i++)
					{
						if( reverse_ID_map[i]!=-1 && i!=ID_map(my_ID) )
						{
							router[i].link_ID = next_hop_with_min_cost(i);
							if( router[i].link_ID == ID_map(my_ID) )
								DV_table[ID_map(my_ID)][i] = edge[i];
							else
								DV_table[ID_map(my_ID)][i] = edge[router[i].link_ID]+DV_table[router[i].link_ID][i];
							// printf("min_next_hop(%d) = %d, cost = %d\n", i, router[i].link_ID, DV_table[ID_map(my_ID)][i]);
						}
					}
					send_DVs_out();
					pthread_mutex_unlock(&protect);
					break;
			/* update */
			case 2:
					edge[A2R_msg.destination] = A2R_msg.weight;
					break;
			/* show */
			case 3:
					R2A_msg.type = htonl( 3 );
					R2A_msg.end = htonl( 0 );
					for(i = 1; i <= MAX_ROUTER; i++)
					{
						if( DV_table[ID_map(my_ID)][i]!=-1 && i!=ID_map(my_ID) )
						{
							R2A_msg.destination = htonl( reverse_ID_map[ i ] );
							R2A_msg.next_hop = htonl( reverse_ID_map[ router[i].link_ID ] );
							R2A_msg.path_cost = htonl( DV_table[ID_map(my_ID)][i] );
							sendn(clisd, (char *)&R2A_msg, sizeof(R2Amsg_type), 0);
						}
					}
					R2A_msg.end = htonl( 1 );
					R2A_msg.destination = htonl( 0 );
					R2A_msg.next_hop = htonl( 0 );
					R2A_msg.path_cost = DV_RECEIVED_SO_FAR;
					sendn(clisd, (char *)&R2A_msg, sizeof(R2Amsg_type), 0);
					break;
			/* route */
			case 4:
					R2A_msg.type = htonl( 4 );
					R2A_msg.end = htonl( 0 );
					R2A_msg.path_cost = htonl( DV_table[ID_map(my_ID)][A2R_msg.destination] );
					R2A_msg.next_hop = htonl( reverse_ID_map[ router[A2R_msg.destination].link_ID ] );
					sendn(clisd, (char *)&R2A_msg, sizeof(R2Amsg_type), 0);
					break;
			default:
					print_error("Unknown A2R_msg.type Received");
					break;
		}

		/* Close Socket */
		close(clisd);
	}

	close(servsd);
	pthread_exit((void*) NULL);
}

int main(int argc, char* argv[])
{
	FILE *router_location_file, *topology_config_file;
	char *token;
	int i,j;
	char *thread_safety;
	char file_line[MAX_STRLEN];
	int FOUND_CHANGE;

	char temp_router_IP[64];
	int temp_command_port, temp_router_port, temp_router_ID;

	int temp_source_ID, temp_destination_ID, temp_cost;

	pthread_t agent_thread;
	pthread_attr_t agent_thread_attr;

	int servsd = -1;
	struct sockaddr_in servaddr;
	char one;

	struct sockaddr_in cliaddr;
	int clisd = -1;
	unsigned int cliaddrlen;


	/* Check Program Argument */
	if( argc != 4 )
	{
		printf("\nUsage: %s ROUTER_LOCATION_FILE TOPOLOGY_CONFIGURATION_FILE ROUTER_ID\n\n", argv[0]);
		exit(0);
	}

	if(my_ID = atoi(argv[3]), my_ID<=0)
	{
		print_error("Invalid Router ID in Input, program terminated.");
		exit(0);
	}

	/* Parser: Router Location File (Given: Always Valid) */
		router = malloc( sizeof(Router)*(MAX_ROUTER+1) );
		reverse_ID_map = malloc( sizeof(int)*(MAX_ROUTER+1) );

		for(i = 0; i <= MAX_ROUTER; i++)
			reverse_ID_map[i] = -1;

		for(i = 0; i <= MAX_ROUTER; i++)
		{
			router[i].link_ID = 0;
		}
		if( router_location_file = fopen(argv[1],"r"), router_location_file == NULL )
		{
			print_error("fopen() failed, program terminated.");
			exit(0);
		}
		printf("\nParsing \"%s\"... ",argv[1]);
		i = 1;
		while( fgets(file_line, MAX_STRLEN, router_location_file) && strlen(file_line)>2 )
		{
			/* Read From File */
			token = strtok_r(file_line,",\n", &thread_safety);
			strcpy(temp_router_IP, token);
			token = strtok_r(NULL,",\n", &thread_safety);
			temp_command_port = atoi(token);
			token = strtok_r(NULL,",\n", &thread_safety);
			temp_router_port = atoi(token);
			token = strtok_r(NULL,",\n", &thread_safety);
			temp_router_ID = atoi(token);

			reverse_ID_map[i] = temp_router_ID;

			/* Parse into Sparse router array */
			strcpy(router[ID_map(temp_router_ID)].router_IP, temp_router_IP);
			router[ID_map(temp_router_ID)].command_port = temp_command_port;
			router[ID_map(temp_router_ID)].router_port = temp_router_port;

			i++;
		}
		fclose(router_location_file);
		printf("done!\n");
		if(ID_map(my_ID) == -1)
		{
			print_error("Specified Router ID does not exist in router location file, program terminated.");
			exit(0);
		}
		router[ID_map(my_ID)].link_ID = ID_map(my_ID);

	/* Parser: Topology Configuration */
		DV_table = malloc( sizeof(int*)*(MAX_ROUTER+1) );
		my_DV_backup = malloc( sizeof(int)*(MAX_ROUTER+1) );
		edge = malloc( sizeof(int)*(MAX_ROUTER+1) );

		for(i = 0; i <= MAX_ROUTER; i++)
			DV_table[i] = malloc( sizeof(int)*(MAX_ROUTER+1) );
		for(i = 0; i <= MAX_ROUTER; i++)
		{
			for(j = 0; j <= MAX_ROUTER; j++)
				if( i == j )
					DV_table[i][j] = 0;
				else
					DV_table[i][j] = -1;
			if(i == ID_map(my_ID))
				edge[i] = 0;
			else
				edge[i] = -1;
		}

		if( topology_config_file = fopen(argv[2],"r"), topology_config_file == NULL )
		{
			print_error("fopen() failed, program terminated.");
			exit(0);
		}
		printf("Parsing \"%s\"... ",argv[2]);
		while( fgets(file_line, MAX_STRLEN, topology_config_file) && strlen(file_line)>2 )
		{
			/* Read From File */
			token = strtok_r(file_line,",\n", &thread_safety);
			temp_source_ID = atoi(token);
			token = strtok_r(NULL,",\n", &thread_safety);
			temp_destination_ID = atoi(token);
			token = strtok_r(NULL,",\n", &thread_safety);
			temp_cost = atoi(token);

			/* Parse into DV table */
			if( temp_source_ID==my_ID )
			{
				router[ID_map(temp_destination_ID)].link_ID = ID_map(temp_destination_ID);
				edge[ID_map(temp_destination_ID)] = temp_cost;
			}
		}
		fclose(topology_config_file);
		printf("done!\n");
	signal( SIGINT, safe_quit );

	/* Create Thread for Agent-to-Router Communication */
		pthread_attr_init(&agent_thread_attr);
		pthread_attr_setdetachstate(&agent_thread_attr, PTHREAD_CREATE_JOINABLE);

		pthread_mutex_lock (&stupid_show_welcome_message);
		pthread_create(&agent_thread, &agent_thread_attr, listening_thread_for_agent, NULL);

	/* Routines for Router-to-Router Communication */
		if ((servsd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
		{
			print_error("socket() failed! (Router Listening Thread)");
			exit(-1);
		}
		setsockopt(servsd, SOL_SOCKET, SO_REUSEADDR, (char*)&one, sizeof(one));
		servaddr.sin_family = AF_INET;
		servaddr.sin_port = htons(router[ID_map(my_ID)].router_port);
		servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
		if (bind(servsd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < -1) {
			print_error("bind() failed! (Router Listening Thread)");
			exit(-1);
		}
		if (listen(servsd, 512) == -1)
		{
			print_error("listen() failed! (Router Listening Thread)");
			exit(-1);
		}
		#if ENABLE_COLOUR
			printf("\033[1;32mRouter #%d: \033[1;30mListening on port %d for Router-to-Router Communication\033[0m\n", my_ID, router[ID_map(my_ID)].router_port);
		#else
			printf("Router #%d: Listening on port %d for Router-to-Router Communication\n", my_ID, router[ID_map(my_ID)].router_port);
		#endif

		cliaddrlen = sizeof(cliaddr);

		/* Prompt Welcome Message */
		pthread_mutex_lock (&stupid_show_welcome_message);
		#if ENABLE_COLOUR
			 printf("\033[1;35m");
		#endif
		printf("########################\n");
		printf("# Router Program Ready #\n");
		printf("########################\n");
		#if ENABLE_COLOUR
			 printf("\033[0m\n");
		#endif
		pthread_mutex_unlock (&stupid_show_welcome_message);

		while(1)
		{
			if( clisd = accept(servsd, (struct sockaddr *)&cliaddr, &cliaddrlen), clisd == -1 )
				print_error("accept() failed (Router Listening Thread)");

			/* Receive Message */
			recvn(clisd, (char *)&in_R2R_msg, sizeof(R2Rmsg_type), 0);
			in_R2R_msg.my_ID = ntohl( in_R2R_msg.my_ID );
			for(j = 1; j <= MAX_ROUTER; j++)
				in_R2R_msg.DV[j] = ntohl( in_R2R_msg.DV[j] );

			DV_RECEIVED_SO_FAR++;

			#if ENABLE_COLOUR
				printf("\033[1;32mRouter #%d \033[0m \"%s\" at port %u\n", in_R2R_msg.my_ID, inet_ntoa(cliaddr.sin_addr), cliaddr.sin_port);
			#else
				printf("Router #%d \"%s\" at port %u\n", in_R2R_msg.my_ID, inet_ntoa(cliaddr.sin_addr), cliaddr.sin_port);
			#endif

			pthread_mutex_lock(&protect);
			/* Update DV Table*/
			for(i = 1; i <= MAX_ROUTER; i++)
			{
				if( in_R2R_msg.DV[i] > INFINITY )
					DV_table[ID_map(in_R2R_msg.my_ID)][i] = -1;
				else
					DV_table[ID_map(in_R2R_msg.my_ID)][i] = in_R2R_msg.DV[i];
			}

			/* Check if DVs should be sent out according to DV algorithm */
				/* Backup DV table */
				for(i = 1; i <= MAX_ROUTER; i++)
					my_DV_backup[i] = DV_table[ID_map(my_ID)][i];
				/* Update DV table */
				for(i = 1; i <= MAX_ROUTER; i++)
				{
					if( reverse_ID_map[i]!=-1 && i!=ID_map(my_ID) )
					{
						router[i].link_ID = next_hop_with_min_cost(i);
						if( router[i].link_ID == ID_map(my_ID) )
							DV_table[ID_map(my_ID)][i] = edge[i];
						else
							DV_table[ID_map(my_ID)][i] = edge[router[i].link_ID]+DV_table[router[i].link_ID][i];
						// printf("min_next_hop(%d) = %d, cost = %d\n", i, router[i].link_ID, DV_table[ID_map(my_ID)][i]);
					}
				}
				for(i = 1, FOUND_CHANGE = 0; i <= MAX_ROUTER; i++)
					if(DV_table[ID_map(my_ID)][i]!=my_DV_backup[i])
					{
						FOUND_CHANGE = 1;
						break;
					}
				if( FOUND_CHANGE )
					send_DVs_out();
			pthread_mutex_unlock(&protect);

			/* Close Socket */
			close(clisd);
		}
		close(servsd);

	return 0;
}
