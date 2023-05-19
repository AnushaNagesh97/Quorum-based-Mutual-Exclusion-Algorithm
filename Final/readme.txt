Assignment 2
Name: Anusha Nagesh
NET ID: AXN220008


Compilation instructions:
- make clean before each run for 8 servers and 5 clients.
- Execute Makefile individually from:
  	-DC01 machine for server1,
	-DC02 machine for server2,
	-DC03 machine for server3,
	-DC04 machine for server4,
	-DC05 machine for server5,
	-DC06 machine for server6,
	-DC07 machine for server7,
	-DC08 machine for server0.
- Clients can be executed on any machine.

Result:
- Server0 will receive COMPLETION NOTIFICATION from all 5 clients and terminate.
- All clients will execute critical section 20 times (if deadlock is not encountered).