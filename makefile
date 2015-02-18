all: agent router
agent: agent.c
	gcc -g -Wall -o agent agent.c -l pthread
router: router.c
	gcc -g -Wall -o router router.c -l pthread
