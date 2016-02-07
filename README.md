+# memshare
 +A simple Linux IPC library using shared memory

Each process register to a control area with a unique name getting a key to shared memory.
Callbacks are registered to receive the data placed in each process shared memory.
A set of APIs are used to send data to other registered processes identifyed by their name.
The different type of APIs are:
signal1, sending an integer to another process.
signal2, sending two integers to another process.
signal3, sending three integers to another process.
data sending a byte block to another process.

memsend is a binary to send data to running processes.
memwatch is a binary to list all registered processes.

tlog, also included is an example implementation of how memshare can be used.