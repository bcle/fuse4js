
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
  var cur = null, previous = null, name = '';
  if (path === '/') {
    return { node:root, parent:null, name:'' };
  }
  comps = path.split('/');
  for (i = 0; i < comps.length; ++i) {
    previous = cur;
    if (i == 0) {
      cur = root;
    } else if (cur !== undefined ){
      name = comps[i];
      cur = cur[name];
    }
  }
  return {node:cur, parent:previous, name:name};
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
  var info = lookup(obj, path);
  var node = info.node;

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
  var info = lookup(obj, path);

  switch (typeof info.node) {
  case 'undefined':
    err = -2; // -ENOENT
    break;
    
  case 'string': // file
    err = -22; // -EINVAL
    break;
  
  case 'object': // directory
    var i = 0;
    for (key in info.node)
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
  var info = lookup(obj, path);
  
  if (typeof info.node === 'undefined') {
    err = -ENOENT;
  }
  cb(err);
}

/*
 * Handler for the read() system call.
 * path: the path to the file
 * offset: the file offset to read from
 * len: the number of bytes to read
 * buf: the Buffer to write the data to
 * cb: a callback of the form cb(err), where err is the Posix return code.
 *     A positive value represents the number of bytes read.
 */
var read = function (path, offset, len, buf, cb) {
  var err = 0; // assume success
  var info = lookup(obj, path);
  var file = info.node;
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

/*
 * Handler for the write() system call.
 * path: the path to the file
 * offset: the file offset to write to
 * len: the number of bytes to write
 * buf: the Buffer to read data from
 * cb: a callback of the form cb(err), where err is the Posix return code.
 *     A positive value represents the number of bytes written.
 */
var write = function (path, offset, len, buf, cb) {
  var err = 0; // assume success
  var info = lookup(obj, path);
  var file = info.node;
  var name = info.name;
  var parent = info.parent;
  var beginning, blank = '', data, ending='', numBlankChars;
  
  switch (typeof file) {
  case 'undefined':
    err = -2; // -ENOENT
    break;

  case 'object': // directory
    err = -1; // -EPERM
    break;
      
  case 'string': // a string treated as ASCII characters
    data = buf.toString('ascii'); // read the new data
    if (offset < file.length) {
      beginning = file.substring(0, offset);
      if (offset + data.length < file.length) {
        ending = file.substring(offset + data.length, file.length)
      }
    } else {
      beginning = file;
      numBlankChars = offset - file.length;
      console.log("numBlankChars = " + numBlankChars);
      while (numBlankChars--) blank += ' ';
    }
    delete parent[name];
    parent[name] = beginning + blank + data + ending;
    err = data.length;
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
  read: read,
  write: write
};

f4js.start("/devel/mnt", handlers);


