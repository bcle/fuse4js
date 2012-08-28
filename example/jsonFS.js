
f4js = require('../build/Debug/fuse4js.node')

console.log("Hello " + f4js.hello());

var obj = {
  'hello.txt': "Hello world!\n",
  'dir1': {
    'welcome.txt': "Welcome to fuse4js\n",
    'dir2': {
      'dummy.txt': "Dummy\n",
      'dummy2.txt': "Dummy 2\n"
    },
    'bienvenue.txt': "Bienvenue!\n",    
  },
  'goodbye.txt': "Goodbye\n"
};

/*
 * Handler for the getattr() system call.
 * path: the path to the file
 * cb: a callback of the form cb(err, stat), where err is the Posix return code
 *     and stat is the result in the form of a stat structure (when err === 0)
 */
var getattr = function (path, cb) {	
  cb( 0,                   // success
      { st_size: 500,      // size in bytes
        st_mode: 16877     // 040755 in octal
      }
  );
};

/*
 * Handler for the readdir() system call.
 * path: the path to the file
 * cb: a callback of the form cb(err, dirents), where err is the Posix return code
 *     and dirents is the result in the form of an array of directory entry objects
 *     (when err === 0). Each directory entry object must have two fields: name, and st_mode
 */
var readdir = function (path, cb) {
  cb( 0,
      [ { name: 'hello.txt', st_mode: 33261},
        { name: 'dir1',      st_mode: 16877}
      ]
  );
}

var handlers = {
  getattr: getattr,
  readdir: readdir
};

f4js.start("/devel/mnt", handlers);

// console.log('Hello')

