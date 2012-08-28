
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

var handlers = {
  getattr: getattr,
  readdir: readdir
};

f4js.start("/devel/mnt", handlers);


