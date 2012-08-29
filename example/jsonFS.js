
f4js = require('../build/Debug/fuse4js.node')

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
 * Given the name space represented by the object 'root', locate
 * the sub-object corresponding to the specified path
 */
function lookup(root, path) {
  if (path === '/') {
    return root;
  }
  comps = path.split('/');
  var cur = null;
  for (i = 0; i < comps.length; ++i) {
    if (i == 0) {
      cur = root;
    } else if (cur !== undefined ){
      cur = cur[comps[i]];
    }
  }
  return cur;
}

/*
 * Handler for the getattr() system call.
 * path: the path to the file
 * cb: a callback of the form cb(err, stat), where err is the Posix return code
 *     and stat is the result in the form of a stat structure (when err === 0)
 */
var getattr = function (path, cb) {	
  var stat = {};
  var err = 0; // assume success
  var node = lookup(obj, path);

  switch (typeof node) {
  case 'undefined':
    err = -2; // -ENOENT
    break;
    
  case 'object': // directory
    stat.st_size = 4096;   // standard size of a directory
    stat.st_mode = 040644; // 16877; // 040755 = directory with 0755 permission
    break;
  
  case 'string': // file
    stat.st_size = node.length;
    stat.st_mode = 0100644; // 33261; // Octal 0100755 : file with 755 permissions
    break;
    
  default:
    break;
  }
  cb( err, stat );
};

/*
 * Handler for the readdir() system call.
 * path: the path to the file
 * cb: a callback of the form cb(err, names), where err is the Posix return code
 *     and names is the result in the form of an array of file names (when err === 0).
 */
var readdir = function (path, cb) {
  var names = [];
  var err = 0; // assume success
  var node = lookup(obj, path);

  switch (typeof node) {
  case 'undefined':
    err = -2; // -ENOENT
    break;
    
  case 'string': // file
    err = -22; // -EINVAL
    break;
  
  case 'object': // directory
    var i = 0;
    for (key in node)
      names[i++] = key;
    break;
    
  default:
    break;
  }
  cb( err, names );
}

/*
 * Handler for the open() system call.
 * path: the path to the file
 * cb: a callback of the form cb(err), where err is the Posix return code
 */
var open = function (path, cb) {
  var err = 0; // assume success
  var node = lookup(obj, path);
  
  if (typeof node === 'undefined') {
    err = -ENOENT;
  }
  cb(err);
}

/*
 * Handler for the read() system call.
 * path: the path to the file
 * offset: the file offset to read from
 * len: the number of bytes to read
 * buf: the Buffer to write to
 * cb: a callback of the form cb(err), where err is the Posix return code.
 *     A positive value represents the number of bytes read.
 */
var read = function (path, offset, len, buf, cb) {
  var err = 0; // assume success
  var file = lookup(obj, path);
  var maxBytes;
  var data;
  
  switch (typeof file) {
  case 'undefined':
    err = -2; // -ENOENT
    break;

  case 'object': // directory
    err = -1; // -EPERM
    break;
      
  case 'string': // a string treated as ASCII characters
    if (offset < file.length) {
      maxBytes = file.length - offset;
      if (len > maxBytes) {
        len = maxBytes;
      }
      data = file.substring(offset, len);
      buf.write(data, 0, len, 'ascii');
      err = len;
    }
    break;
  
  default:
    break;
  }
  cb(err);
}


var handlers = {
  getattr: getattr,
  readdir: readdir,
  open: open,
  read: read
};

f4js.start("/devel/mnt", handlers);


