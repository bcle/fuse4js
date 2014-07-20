/*
 * 
 * jsonFS.js
 * 
 * Copyright (c) 2012 - 2014 by VMware, Inc. All Rights Reserved.
 * http://www.vmware.com
 * Refer to LICENSE.txt for details of distribution and use.
 * 
 */
var f4js = require('fuse4js');
var fs = require('fs');
var obj = null;   // The JSON object we'll be exposing as a file system
var options = {};  // See parseArgs()


//---------------------------------------------------------------------------

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
      if (cur === undefined) {
        break;
      }
    }
  }
  return {node:cur, parent:previous, name:name};
}

//---------------------------------------------------------------------------

/*
 * Handler for the getattr() system call.
 * path: the path to the file
 * cb: a callback of the form cb(err, stat), where err is the Posix return code
 *     and stat is the result in the form of a stat structure (when err === 0)
 */
function getattr(path, cb) {	
  var stat = {};
  var err = 0; // assume success
  var info = lookup(obj, path);
  var node = info.node;

  switch (typeof node) {
  case 'undefined':
    err = -2; // -ENOENT
    break;
    
  case 'object': // directory
    stat.size = 4096;   // standard size of a directory
    stat.mode = 040777; // directory with 777 permissions
    break;
  
  case 'string': // file
    stat.size = node.length;
    stat.mode = 0100666; // file with 666 permissions
    break;
    
  default:
    break;
  }
  cb( err, stat );
};

//---------------------------------------------------------------------------

/*
 * Handler for the readdir() system call.
 * path: the path to the file
 * cb: a callback of the form cb(err, names), where err is the Posix return code
 *     and names is the result in the form of an array of file names (when err === 0).
 */
function readdir(path, cb) {
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

//---------------------------------------------------------------------------

/*
 * Handler for the open() system call.
 * path: the path to the file
 * flags: requested access flags as documented in open(2)
 * cb: a callback of the form cb(err, [fh]), where err is the Posix return code
 *     and fh is an optional numerical file handle, which is passed to subsequent
 *     read(), write(), and release() calls.
 */
function open(path, flags, cb) {
  var err = 0; // assume success
  var info = lookup(obj, path);
  
  if (typeof info.node === 'undefined') {
    err = -2; // -ENOENT
  }
  cb(err); // we don't return a file handle, so fuse4js will initialize it to 0
}

//---------------------------------------------------------------------------

/*
 * Handler for the read() system call.
 * path: the path to the file
 * offset: the file offset to read from
 * len: the number of bytes to read
 * buf: the Buffer to write the data to
 * fh:  the optional file handle originally returned by open(), or 0 if it wasn't
 * cb: a callback of the form cb(err), where err is the Posix return code.
 *     A positive value represents the number of bytes actually read.
 */
function read(path, offset, len, buf, fh, cb) {
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

//---------------------------------------------------------------------------

/*
 * Handler for the write() system call.
 * path: the path to the file
 * offset: the file offset to write to
 * len: the number of bytes to write
 * buf: the Buffer to read data from
 * fh:  the optional file handle originally returned by open(), or 0 if it wasn't
 * cb: a callback of the form cb(err), where err is the Posix return code.
 *     A positive value represents the number of bytes actually written.
 */
function write(path, offset, len, buf, fh, cb) {
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

//---------------------------------------------------------------------------

/*
 * Handler for the release() system call.
 * path: the path to the file
 * fh:  the optional file handle originally returned by open(), or 0 if it wasn't
 * cb: a callback of the form cb(err), where err is the Posix return code.
 */
function release(path, fh, cb) {
  cb(0);
}

//---------------------------------------------------------------------------

/*
 * Handler for the create() system call.
 * path: the path of the new file
 * mode: the desired permissions of the new file
 * cb: a callback of the form cb(err, [fh]), where err is the Posix return code
 *     and fh is an optional numerical file handle, which is passed to subsequent
 *     read(), write(), and release() calls (it's set to 0 if fh is unspecified)
 */
function create (path, mode, cb) {
  var err = 0; // assume success
  var info = lookup(obj, path);
  
  switch (typeof info.node) {
  case 'undefined':
    if (info.parent !== null) {
      info.parent[info.name] = '';
    } else {
      err = -2; // -ENOENT      
    }
    break;

  case 'string': // existing file
  case 'object': // existing directory
    err = -17; // -EEXIST
    break;
      
  default:
    break;
  }
  cb(err);
}

//---------------------------------------------------------------------------

/*
 * Handler for the unlink() system call.
 * path: the path to the file
 * cb: a callback of the form cb(err), where err is the Posix return code.
 */
function unlink(path, cb) {
  var err = 0; // assume success
  var info = lookup(obj, path);
  
  switch (typeof info.node) {
  case 'undefined':
    err = -2; // -ENOENT      
    break;

  case 'object': // existing directory
    err = -1; // -EPERM
    break;

  case 'string': // existing file
    delete info.parent[info.name];
    break;
    
  default:
    break;
  }
  cb(err);
}

//---------------------------------------------------------------------------

/*
 * Handler for the rename() system call.
 * src: the path of the file or directory to rename
 * dst: the new path
 * cb: a callback of the form cb(err), where err is the Posix return code.
 */
function rename(src, dst, cb) {
  var err = -2; // -ENOENT assume failure
  var source = lookup(obj, src), dest;
  
  if (typeof source.node !== 'undefined') { // existing file or directory
    dest = lookup(obj, dst);
    if (typeof dest.node === 'undefined' && dest.parent !== null) {
      dest.parent[dest.name] = source.node;
      delete source.parent[source.name];
      err = 0;
    } else {
      err = -17; // -EEXIST
    }
  }   
  cb(err);
}

//---------------------------------------------------------------------------

/*
 * Handler for the mkdir() system call.
 * path: the path of the new directory
 * mode: the desired permissions of the new directory
 * cb: a callback of the form cb(err), where err is the Posix return code.
 */
function mkdir(path, mode, cb) {
  var err = -2; // -ENOENT assume failure
  var dst = lookup(obj, path), dest;
  if (typeof dst.node === 'undefined' && dst.parent != null) {
    dst.parent[dst.name] = {};
    err = 0;
  }
  cb(err);
}

//---------------------------------------------------------------------------

/*
 * Handler for the rmdir() system call.
 * path: the path of the directory to remove
 * cb: a callback of the form cb(err), where err is the Posix return code.
 */
function rmdir(path, cb) {
  var err = -2; // -ENOENT assume failure
  var dst = lookup(obj, path), dest;
  if (typeof dst.node === 'object' && dst.parent != null) {
    delete dst.parent[dst.name];
    err = 0;
  }
  cb(err);
}

//---------------------------------------------------------------------------

/*
 * Handler for the init() FUSE hook. You can initialize your file system here.
 * cb: a callback to call when you're done initializing. It takes no arguments.
 */
function init(cb) {
  console.log("File system started at " + options.mountPoint);
  console.log("To stop it, type this in another shell: fusermount -u " + options.mountPoint);
  cb();
}

//---------------------------------------------------------------------------

/*
 * Handler for the setxattr() FUSE hook. 
 * The arguments differ between different operating systems.
 * Darwin(Mac OSX):
 *  * a = position
 *  * b = options
 *  * c = cmd
 * Other:
 *  * a = flags
 *  * b = cmd
 *  * c = undefined
 */
function setxattr(path, name, value, size, a, b, c) {
  console.log("Setxattr called:", path, name, value, size, a, b, c)
  cb(0);
}

//---------------------------------------------------------------------------

/*
 * Handler for the statfs() FUSE hook. 
 * cb: a callback of the form cb(err, stat), where err is the Posix return code
 *     and stat is the result in the form of a statvfs structure (when err === 0)
 */
function statfs(cb) {
  cb(0, {
      bsize: 1000000,
      frsize: 1000000,
      blocks: 1000000,
      bfree: 1000000,
      bavail: 1000000,
      files: 1000000,
      ffree: 1000000,
      favail: 1000000,
      fsid: 1000000,
      flag: 1000000,
      namemax: 1000000
  });
}

//---------------------------------------------------------------------------

/*
 * Handler for the destroy() FUSE hook. You can perform clean up tasks here.
 * cb: a callback to call when you're done. It takes no arguments.
 */
function destroy(cb) {
  if (options.outJson) {
    try {
      fs.writeFileSync(options.outJson, JSON.stringify(obj, null, '  '), 'utf8');
    } catch (e) {
      console.log("Exception when writing file: " + e);
    }
  }
  console.log("File system stopped");      
  cb();
}

//---------------------------------------------------------------------------

var handlers = {
  getattr: getattr,
  readdir: readdir,
  open: open,
  read: read,
  write: write,
  release: release,
  create: create,
  unlink: unlink,
  rename: rename,
  mkdir: mkdir,
  rmdir: rmdir,
  init: init,
  destroy: destroy,
  setxattr: setxattr,
  statfs: statfs
};

//---------------------------------------------------------------------------

function usage() {
  console.log();
  console.log("Usage: node jsonFS.js [options] inputJsonFile mountPoint");
  console.log("(Ensure the mount point is empty and you have wrx permissions to it)\n")
  console.log("Options:");
  console.log("-o outputJsonFile  : save modified data to new JSON file. Input file is never modified.");
  console.log("-d                 : make FUSE print debug statements.");
  console.log("-a                 : add allow_other option to mount (might need user_allow_other in system fuse config file).");
  console.log();
  console.log("Example:");
  console.log("node example/jsonFS.fs -d -o /tmp/output.json example/sample.json /tmp/mnt");
  console.log();
}

//---------------------------------------------------------------------------

function parseArgs() {
  var i, remaining;
  var args = process.argv;
  if (args.length < 4) {
    return false;
  }
  options.mountPoint = args[args.length - 1];
  options.inJson = args[args.length - 2];
  remaining = args.length - 4;
  i = 2;
  while (remaining--) {
    if (args[i] === '-d') {
      options.debugFuse = true;
      ++i;
    } else if (args[i] === '-o') {
      if (remaining) {
        options.outJson = args[i+1];
        i += 2;
        --remaining;
      } else return false;
    } else if (args[i] === '-a') {
      options.allowOthers = true;
      ++i;
    } else return false;
  }
  return true;
}

//---------------------------------------------------------------------------

(function main() {
  if (parseArgs()) {
    console.log("\nInput file: " + options.inJson);
    console.log("Mount point: " + options.mountPoint);
    if (options.outJson)
      console.log("Output file: " + options.outJson);
    if (options.debugFuse)
      console.log("FUSE debugging enabled");
    content = fs.readFileSync(options.inJson, 'utf8');
    obj = JSON.parse(content);
    try {
      var opts = [];
      if (options.allowOthers) {
        opts.push('-o');
        opts.push('allow_other');
      }
      f4js.start(options.mountPoint, handlers, options.debugFuse, opts);
    } catch (e) {
      console.log("Exception when starting file system: " + e);
    }
  } else {
    usage();
  }
})();
