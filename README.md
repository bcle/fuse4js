Fuse4js
=======

Fuse4js provides Javascript bindings to the [FUSE](http://fuse.sourceforge.net/) subsystem of Linux. It enables you to develop user-space file systems with node.js.

Requirements
------------
Linux:
* Fuse4js has been tested on Ubuntu 10.04, Ubuntu 12.04  and CentOS 5.x (all 32-bit).
* GNU Compiler toolchain, including gcc and g++
    * On Ubuntu: `sudo apt-get install g++`
* FUSE library and header files.
    * On Ubuntu: `sudo apt-get install libfuse-dev`
    * On CentOS / RedHat: `yum install fuse-devel`
* pkg-config tool (typically included out-of-the-box with the OS)
* node.js 0.8.7 or later

OSX:
* install [osxfuse](http://osxfuse.github.com/)

Tutorial
--------

This tutorial explains how to install and use fuse4js.

* Ensure your system meets the software requirements listed above.
* Create a /tmp/tutorial directory.
* cd to the directory you created.
* Create a mnt/ subdirectory. It will be used as the mount point for testing FUSE file systems.
* Download the fuse4js/ source directory:
`git clone git://github.com/vmware/fuse4js.git`
* Compile the source code to create the fuse4js add-on in a local node_modules/ subdirectory:  
`npm install fuse4js`
* Run the sample jsonFS file system:  
`node fuse4js/example/jsonFS.js fuse4js/example/sample.json /tmp/tutorial/mnt`  
This mounts the JSON file as a file system. In a another shell, you can browse and make changes to the file system under /tmp/tutorial/mnt. You can view, edit, create, and move files and directories.
* To dismount, make sure no processes remain under the file system path, and then type:
`fusermount -u /tmp/tutorial/mnt`
* Changes to the file system are discarded. If you want to save the modified data to a new JSON file, add the `-o outputJsonFilePath` option when starting the program.
* If you have trouble with the jsonFS example, consider setting "user_allow_other" in /etc/fuse.conf, as some linux distributions need this as a requirement for the option "allow_others" as set in the jsonFS example.

Once you are comfortable with the sample program, you can move on to the second example, *mirrorFS.js*. It is equivalent to the fusexmp.c sample program that ships with the FUSE source code. As its name indicates, it maps an existing file system subtree to a mount point of your choice. It demonstrates more advanced features such as file handles. The syntax is:
`node fuse4js/example/mirrorFS.js <file_system_directory> <new_mount_point>`


Global installation
-------------------
The tutorial used a local installation of fuse4js that is private to the /tmp/tutorial directory. To install fuse4js globally on your system:

* Log in as root, or use sudo
* Download the source code into a `fuse4js/` subdirectory
* Install by typing: `npm install -g --unsafe-perm fuse4js`  
The `--unsafe-perm` option seems to be necessary to work around an interference between the node-gyp compilation process and npm's downgrading of permissions when running a package's installation script (you may get an EACCES error without it)
* At this point, the add-on should be installed under `/usr/local/lib/node_modules`. To use it in your programs using a statement such as `fuse4js = require("fuse4js")`, include `/usr/local/lib/node_modules` in your NODE_PATH environment variable. By default, node.js doesn't look in there for some reason, despite the fact that npm uses that directory as the default global installation location. Example:
`export NODE_PATH=/usr/local/lib/node_modules`

Installation from NPM registry
-------------------
As of September 17, 2012, fuse4js has been added to the NPM registry, so you can skip the git download step and install it directly by typing:  
`npm install fuse4js`  
This will put the module under a local node_modules/ subdirectory.

API Documentation
----------------- 
Fuse4js currently implements a subset of all FUSE file operations. More will be added over time, but the initial set is sufficient to implement a basic read/write file system.

You implement a file system by registering Javascript handler functions with fuse4js. Each handler handles a particular FUSE operation. While the arguments passed to a handler vary depending on the requested operation, the last argument is always a callback function that you invoke when you are finished servicing the request. The arguments to the callback typically include an error code, followed by zero or more additional arguments depending on the FUSE operation. Following FUSE conventions, the error code is set to a negated 'errno' value to indicate error, and zero or a positive value (for read/write operations) to indicate success.

We currently don't have a separate document describing the Javascript interface corresponding to each FUSE operation. Instead, the API is documented in the comments for each handler in the `mirrorFS.js` sample program, so use that as the reference for now.

How it Works
------------
The FUSE event loop runs in its own thread, and communicates with the node.js main thread using an RPC mechanism based on a libuv async object and a semaphore. There are a couple of context switches per FUSE system call. Read/Write operations also involve a copy operation via a node.js Buffer object.

ToDo List
---------
* Automated tests
* Support for more FUSE operations, such as hard link management.
* Improve performance (try to reduce context switches and copy operations)


License
-------
Since fuse4js dynamically links with your system's FUSE library, which is a component of Linux, it is licensed under the GPL version 2. See the COPYING.txt file for details.

Contributing
------------
If you wish to submit code changes to this repository, please request a contribution agreement form from the maintainer (VMware legal requires it).

Contact
-------
Feel free to send bug reports and constructive feedback to the maintainer: fuse4js@vmware.com  

