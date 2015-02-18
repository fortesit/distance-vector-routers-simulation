#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "common.h"

/* Global Variables Declaration */
Router *router;
int sending_to_ID;
int *reverse_ID_map;

A2Rmsg_type A2R_msg;
R2Amsg_type R2A_msg;

/* Utilities */
	int ID_map(int real_ID)
	{
		int i;
		for(i = 1; i <= MAX_ROUTER; i++)
			if(real_ID == reverse_ID_map[i])
				return i;
		return -1;
	}

	int is_number_string(char str[])
	{
		int i;
		int len = strlen(str);

		if( !(str[0]>='0'&&str[0]<='9') )
			if( !(str[0]=='-' && str[1]>='0'&&str[1]<='9') )
				return 0;
		for(i = 1; i < len; i++)
			if(str[i] < '0' || str[i] > '9')
				return 0;
		return 1;
	}

	int return_option(char str[])
	{
		if( strcasecmp(str,"dv") == 0 )	return 1;
		if( strcasecmp(str,"update") == 0 )	return 2;
		if( strcasecmp(str,"show") == 0 )		return 3;
		if( strcasecmp(str,"route") == 0 )	return 4;
		return 0;
	}

	void send_message()
	{
		int agent_socket;
		struct sockaddr_in servaddr;
		struct hostent *ht;

		/* Prepare Socket */
		if( agent_socket = socket(AF_INET, SOCK_STREAM, 0), agent_socket == -1 )
		{
			print_error("socket() failed");
			return;
		}
		memset(&servaddr, 0, sizeof(servaddr));
		servaddr.sin_family = AF_INET;
		servaddr.sin_port = htons(router[sending_to_ID].command_port);
		if( ht = gethostbyname(router[sending_to_ID].router_IP), ht == NULL )
		{
			print_error("Unable to resolve target router's DNS");
			return;
		}
		memcpy(&servaddr.sin_addr, ht->h_addr, ht->h_length);

		/* Establish Connection */
		if (connect(agent_socket, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1)
		{
			print_error("connect() failed");
			return;
		}

		/* Send Message */
		A2R_msg.type = htonl(A2R_msg.type);
		A2R_msg.weight = htonl( A2R_msg.weight );
		A2R_msg.destination = htonl( A2R_msg.destination );
		sendn(agent_socket, (char*)&A2R_msg, sizeof(A2Rmsg_type), 0);

		/* Receive Message (if any) */
			if( ntohl(A2R_msg.type) == 3 )
			{
				printf("Destination | Next hop | Path cost\n");
				printf("----------------------------------\n");
				while( recvn(agent_socket, (char*)&R2A_msg, sizeof(R2Amsg_type), 0),
						R2A_msg.type=ntohl(R2A_msg.type), R2A_msg.destination=ntohl(R2A_msg.destination),
						R2A_msg.next_hop=ntohl(R2A_msg.next_hop), R2A_msg.path_cost=ntohl(R2A_msg.path_cost),
						R2A_msg.end=ntohl(R2A_msg.end), R2A_msg.end==0 )
					printf("    %5d       %5d      %5d\n", R2A_msg.destination, R2A_msg.next_hop, R2A_msg.path_cost);
				R2A_msg.type=ntohl(R2A_msg.type);
				R2A_msg.destination=ntohl(R2A_msg.destination);
				R2A_msg.next_hop=ntohl(R2A_msg.next_hop);
				R2A_msg.path_cost=ntohl(R2A_msg.path_cost);
				R2A_msg.end=ntohl(R2A_msg.end);
				printf("Number of distance vectors received = %d\n", R2A_msg.path_cost);
			}
			else if ( ntohl(A2R_msg.type) == 4 )
			{
				recvn(agent_socket, (char*)&R2A_msg, sizeof(R2Amsg_type), 0);
						R2A_msg.type=ntohl(R2A_msg.type);
						R2A_msg.destination=ntohl(R2A_msg.destination);
						R2A_msg.next_hop=ntohl(R2A_msg.next_hop);
						R2A_msg.path_cost=ntohl(R2A_msg.path_cost);
						R2A_msg.end=ntohl(R2A_msg.end);
			}
		/* Close Socket */
		close(agent_socket);
}

/* main function */
int main(int argc, char* argv[])
{
	char agent_command[MAX_STRLEN], agent_command_backup[MAX_STRLEN];
	char router_rule[MAX_STRLEN];
	char *token;
	int temp_int;
	FILE *router_location_file;
	char *thread_safety;
	int NO_ROUTER = 1;

	int i;
	int temp_source_router, temp_destination_router, temp_target_router, temp_edge_weight;
	char temp_router_IP[64];
	int temp_command_port, temp_router_port, temp_router_ID;

	/* Check Program Argument */
		if( argc != 2 )
		{
			printf("\nUsage: %s ROUTER_LOCATION_FILE\n\n", argv[0]);
			exit(0);
		}

	/* Parser: Router Location File (Given: Always Valid) */
		printf("\nParsing \"%s\"... ",argv[1]);

		router = malloc( sizeof(Router)*(MAX_ROUTER+1) );
		reverse_ID_map = malloc( sizeof(int)*(MAX_ROUTER+1) );

		for(i = 0; i <= MAX_ROUTER; i++)
			reverse_ID_map[i] = -1;
		if( router_location_file = fopen(argv[1],"r"), router_location_file == NULL )
		{
			print_error("fopen() failed, program terminated.");
			exit(0);
		}
		i = 1;
		while( fgets(router_rule, MAX_STRLEN, router_location_file) && strlen(router_rule)>2 )
		{
			/* Read From File */
			token = strtok_r(router_rule,",\n", &thread_safety);
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

			NO_ROUTER = 0;
			i++;
		}
		fclose(router_location_file);
		printf("done!\n");
		if( NO_ROUTER )
		{
				print_error("No router detected in the file!");
				exit(0);
		}

	/* Command Prompt  */
	#if ENABLE_COLOUR
		 printf("\033[1;32m");
	#endif
	printf("#######################\n");
	printf("# Agent Program Ready #\n");
	printf("#######################\n");
	#if ENABLE_COLOUR
		 printf("\033[0m\n");
	#endif

	#if ENABLE_COLOUR
	while(	printf("\033[1;33mAgent>\033[0m "), fgets(agent_command, MAX_STRLEN, stdin), agent_command[strlen(agent_command)-1]='\0',
			strcpy(agent_command_backup,agent_command), strcasecmp(agent_command,"quit") != 0)
	#else
	while(	printf("Agent> "), fgets(agent_command, MAX_STRLEN, stdin), agent_command[strlen(agent_command)-1]='\0',
			strcpy(agent_command_backup,agent_command), strcasecmp(agent_command,"quit") != 0)
	#endif
	{
		if( token = strtok_r(agent_command,":,\n", &thread_safety), token == NULL || 
			(agent_command_backup[strlen(agent_command)]!=':' && strcasecmp(agent_command_backup,"dv")!=0 && strcasecmp(agent_command_backup,"ls")!=0) )
		{
			print_error("No valid command detected");
			continue;
		}
		/* Easter Egg */
		else if( strcasecmp(agent_command_backup,"ls") == 0 )
		{
			print_error("What? Are you crazy?");
			continue;
		}
		else
		{
			switch( return_option(token) )
			{
				case 1: /* "dv" */
					/* Parsing */
						if( token = strtok_r(NULL,":,\n", &thread_safety), token != NULL )
						{
							printf("Usage:\n\tdv\n");
							continue;
						}
					/* Preparing Agent-to-Router Message */
						/* sending_to_ID = ALL */
						#if ENABLE_COLOUR
							printf("\033[1;34mCalling all active routers to send out DV... ");
						#else
							printf("Calling all active routers to send out DV... ");
						#endif
						for(i = 1; i <= MAX_ROUTER; i++)
						{
							if( reverse_ID_map[i]!=-1 )
							{
								A2R_msg.type = 1;
								A2R_msg.destination = 0;
								A2R_msg.weight = 0;

								sending_to_ID = i;
								send_message();
							}
						}
						#if ENABLE_COLOUR
							printf("\033[1;33mFinished!\033[0m\n");
						#else
							printf("done!\n");
						#endif

					break;
				case 2: /* "update" */
					/* Parsing */
						/* SOURCE_ROUTER */
						if( token = strtok_r(NULL,":,\n", &thread_safety), token == NULL)
						{
							printf("Usage:\n\tupdate:SOURCE_ROUTER,DESTINATION_ROUTER,EDGE_WEIGHT\n");
							continue;
						}
						if(temp_int = atoi(token), !is_number_string(token) || temp_int<=0)
						{
							print_error("Invalid Interger or Command Format");
							continue;
						}
						temp_source_router = temp_int;

						/* DESTINATION_ROUTER */
						if( token = strtok_r(NULL,":,\n", &thread_safety), token == NULL )
						{
							printf("Usage:\n\tupdate:SOURCE_ROUTER,DESTINATION_ROUTER,EDGE_WEIGHT\n");
							continue;
						}
						if(temp_int = atoi(token), !is_number_string(token) || temp_int<=0 )
						{
							print_error("Invalid Interger or Command Format");
							continue;
						}
						temp_destination_router = temp_int;

						/* EDGE_WEIGHT */
						if( token = strtok_r(NULL,":,\n", &thread_safety), token == NULL )
						{
							printf("Usage:\n\tupdate:SOURCE_ROUTER,DESTINATION_ROUTER,EDGE_WEIGHT\n");
							continue;
						}
						if(temp_int = atoi(token), !is_number_string(token) || (temp_int!=-1 && temp_int<1) || (temp_int==-1 && strcmp(token,"-1")!=0) )
						{
							print_error("Invalid Interger or Command Format");
							continue;
						}
						temp_edge_weight = temp_int;

						/* Check Extra */
						if( token = strtok_r(NULL,":,\n", &thread_safety), token != NULL )
						{
							printf("Usage:\n\tupdate:SOURCE_ROUTER,DESTINATION_ROUTER,EDGE_WEIGHT\n");
							continue;
						}

						/* Verification */
						if( strchr(agent_command_backup,':') != strrchr(agent_command_backup,':') )
						{
							print_error("No valid command detected");
							continue;
						}
						if( ID_map(temp_source_router)==-1 )
						{
							print_error("Invalid source router ID");
							continue;
						}
						if( ID_map(temp_destination_router)==-1 )
						{
							print_error("Invalid destination router ID");
							continue;
						}

					/* Preparing Agent-to-Router Message */
						A2R_msg.type = 2;
						A2R_msg.destination = ID_map(temp_destination_router);
						A2R_msg.weight = temp_edge_weight;
						sending_to_ID = ID_map(temp_source_router);
						send_message();

					break;
				case 3: /* "show" */
					/* Parsing */
						/* TARGET_ROUTER */
						if( token = strtok_r(NULL,":,\n", &thread_safety), token == NULL )
						{
							printf("Usage:\n\tshow:TARGET_ROUTER\n");
							continue;
						}
						if(temp_int = atoi(token), !is_number_string(token) || temp_int<=0 )
						{
							print_error("Invalid Interger or Command Format");
							continue;
						}
						temp_target_router = temp_int;

						/* Check Extra */
						if( token = strtok_r(NULL,":,\n", &thread_safety), token != NULL )
						{
							printf("Usage:\n\tshow:TARGET_ROUTER\n");
							continue;
						}

						/* Verification */
						if( ID_map(temp_target_router)==-1 )
						{
							print_error("Invalid target router ID");
							continue;
						}

					/* Preparing Agent-to-Router Message */
						A2R_msg.type = 3 ;
						A2R_msg.destination = 0;
						A2R_msg.weight = 0;
						sending_to_ID = ID_map(temp_target_router);
						send_message();

					break;
				case 4: /* "route" */
					/* Parsing */
						/* SOURCE_ROUTER */
						if( token = strtok_r(NULL,":,\n", &thread_safety), token == NULL )
						{
							printf("Usage:\n\troute:SOURCE_ROUTER,DESTINATION_ROUTER\n");
							continue;
						}
						if(temp_int = atoi(token), !is_number_string(token) || temp_int<=0 )
						{
							print_error("Invalid Interger or Command Format");
							continue;
						}
						temp_source_router = temp_int;

						/* DESTINATION_ROUTER */
						if( token = strtok_r(NULL,":,\n", &thread_safety), token == NULL )
						{
							printf("Usage:\n\troute:SOURCE_ROUTER,DESTINATION_ROUTER\n");
							continue;
						}
						if(temp_int = atoi(token), !is_number_string(token) || temp_int<=0 )
						{
							print_error("Invalid Interger or Command Format");
							continue;
						}
						temp_destination_router = temp_int;

						/* Check Extra */
						if( token = strtok_r(NULL,":,\n", &thread_safety), token != NULL )
						{
							printf("Usage:\n\troute:SOURCE_ROUTER,DESTINATION_ROUTER\n");
							continue;
						}

						/* Verification */
						if( strchr(agent_command_backup,':') != strrchr(agent_command_backup,':') )
						{
							print_error("No valid command detected");
							continue;
						}
						if( ID_map(temp_source_router) == -1 )
						{
							print_error("Invalid source router ID");
							continue;
						}
						if( ID_map(temp_destination_router) == -1 )
						{
							print_error("Invalid destination router ID");
							continue;
						}

					/* Preparing Agent-to-Router Message */
						sending_to_ID = ID_map(temp_source_router);
						do
						{
							A2R_msg.type = 4;
							A2R_msg.weight = 0;
							A2R_msg.destination = ID_map(temp_destination_router);
							printf("%d -> ", reverse_ID_map[sending_to_ID]);
							send_message();
							sending_to_ID = ID_map(R2A_msg.next_hop);
							if( R2A_msg.path_cost==-1 )
								break;
						} while( sending_to_ID!=ntohl(A2R_msg.destination) );
							if( R2A_msg.path_cost==-1 )
								printf("Destination Unreachable\n");
							else
								printf("%d\n", reverse_ID_map[ntohl(A2R_msg.destination)] );
					break;
				case 0:
					/* otherwise */
					print_error("No valid command detected");
					break;
			}
		}
	}

	/* Free up Everything */
	printf("\nFree-ing up Resources... ");
	free(router);
	printf("done!\n");

	printf("Thank you.\n");
	return 0;
}
