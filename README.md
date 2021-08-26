## SOL-Project
Repository for the final project for the Operating Systems course (year 2020 - 2021) at the University of Pisa.\
The deliverable is a file caching server that saves files in main memory, and provides an interface to interact with it on an `AF_UNIX` socket.\
The `ClientAPI` library then provides the client-side functions to interact with the server.

To run the project with the provided tests:
```bash
$ make all
$ make test1
```
