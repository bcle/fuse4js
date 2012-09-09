Fuse4js
=======

Fuse4js provides Javascript bindings to the [FUSE](http://fuse.sourceforge.net/) subsystem of Linux. It enables you to develop user-space file systems with node.js.

Requirements
------------
* Linux. Fuse4js has been tested on Ubuntu 10.04, CentOS 5.x, and Ubuntu 12.04.
* node.js 0.8.7 or later
* FUSE library and header files.
    * On Ubuntu: `sudo apt-get install libfuse-dev`
    * On CentOS / RedHat: `yum install fuse-devel`
* GNU Compiler toolchain
 
Tutorial
--------

This tutorial explains how to install and use Fuse4js.

* Create a /tmp/tutorial directory
* cd to the directory you created
* Create a mnt/ subdirectory. It will be used as the mount point for testing FUSE file systems.
* Download and expand the fuse4js source code into the fuse4js/ subdirectory.
* Type:  
`npm install fuse4js`  
This will compile the source code and create the fuse4js add-on in the node_modules/ subdirectory.
* Run the sample jsonFS file system:  
`node fuse4js/example/jsonFS.js fuse4js/example/sample.json /tmp/tutorial/mnt`  
This mounts the JSON file as a file system. In a another shell, you can browse and make changes to the file system under /tmp/tutorial/mnt. You can view, edit, create, and move files and directories.
* To dismount, make sure no processes remain under the file system path, and then type:  
`fusermount -u /tmp/tutorial/mnt`
* Changes to the file system are discarded. If you want to save the modified data to a new JSON file, add the `-o outputJsonFilePath` option when starting the file system.

API Documentation
-----------------

Fuse4js currently implements a subset of the FUSE system calls. More will be added over time, but the initial set is sufficient to implement a basic read/write file system. We currently don't have formal documentation for the Javascript interface corresponding to each FUSE system call, so the best way to learn it is to read the comments in the jsonFS.js sample program.

How it Works
------------
The FUSE event loop runs in its own thread, and communicates with the node.js main thread using an RPC mechanism based on a libuv async object and a semaphore. There are a couple of context switches per FUSE system call. Read/Write operations also involve a copy operation via the node.js Buffer object.

ToDo List
---------
* Support for more system calls, including file attributes, permissions, time stamps, and hard link operations.
* Improve performance (try to reduce context switches and copy operations)
* Mac Port?

Contact
-------
Constructive feedback is welcome by sending email to: bcle00@gmail.com  
Enjoy.

