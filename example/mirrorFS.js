/*
 * 
 * mirrorFS.js
 * 
 * Copyright (c) 2012 VMware, Inc. All rights reserved.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; only version 2 of the License, and no
 * later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * 
 */

// var f4js = require('fuse4js');
var f4js = require('../build/Debug/fuse4js.node');
var fs = require('fs');
var pth = require('path');
var srcRoot = '/';   // The root of the file system we are mirroring
var options = {};  // See parseArgs()


//---------------------------------------------------------------------------

/*
 * Convert a node.js file system exception to a numerical errno value.
 * This is necessary because the err.errno property appears to be wrong.
 * On the other hand, the err.code string seems like a correct representation of the error,
 * so we use that instead.
 */

var errnoMap = {
    EPERM: 1,
    ENOENT: 2,
    EINVAL: 22,
    EACCESS: 13    
};

function errToErrNo(err) {
  errno = errnoMap[err.code];
  if (!errno)
    errno = 2; // default to ENOENT
  return errno;
}

//---------------------------------------------------------------------------

/*
 * Handler for the getattr() system call.
 * path: the path to the file
 * cb: a callback of the form cb(err, stat), where err is the Posix return code
 *     and stat is the result in the form of a stat structure (when err === 0)
 */
var getattr = function (path, cb) {	
  
  path = pth.join(srcRoot, path);
  return fs.lstat(path, function lstatCb(err, stats) {
    if (err)      
      return cb(-errToErrNo(err));
    var millisecs = stats.mtime.getTime();
    return cb(0, stats);
  });
};

//---------------------------------------------------------------------------

/*
 * Handler for the readdir() system call.
 * path: the path to the file
 * cb: a callback of the form cb(err, names), where err is the Posix return code
 *     and names is the result in the form of an array of file names (when err === 0).
 */
var readdir = function (path, cb) {

  path = pth.join(srcRoot, path);
  return fs.readdir(path, function readdirCb(err, files) {
    if (err)      
      return cb(-errToErrNo(err));
    return cb(0, files);
  });
}

//---------------------------------------------------------------------------

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

//---------------------------------------------------------------------------

/*
 * Handler for the read() system call.
 * path: the path to the file
 * offset: the file offset to read from
 * len: the number of bytes to read
 * buf: the Buffer to write the data to
 * cb: a callback of the form cb(err), where err is the Posix return code.
 *     A positive value represents the number of bytes actually read.
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

//---------------------------------------------------------------------------

/*
 * Handler for the write() system call.
 * path: the path to the file
 * offset: the file offset to write to
 * len: the number of bytes to write
 * buf: the Buffer to read data from
 * cb: a callback of the form cb(err), where err is the Posix return code.
 *     A positive value represents the number of bytes actually written.
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
 * Handler for the create() system call.
 * path: the path to the file
 * cb: a callback of the form cb(err), where err is the Posix return code.
 */
var create = function (path, cb) {
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
var unlink = function (path, cb) {
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
var rename = function (src, dst, cb) {
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
 * cb: a callback of the form cb(err), where err is the Posix return code.
 */
var mkdir = function (path, cb) {
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
var rmdir = function (path, cb) {
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
var init = function (cb) {
  console.log("File system started at " + options.mountPoint);
  console.log("To stop it, type this in another shell: fusermount -u " + options.mountPoint);
  cb();
}

//---------------------------------------------------------------------------

/*
 * Handler for the destroy() FUSE hook. You can perform clean up tasks here.
 * cb: a callback to call when you're done. It takes no arguments.
 */
var destroy = function (cb) {
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
  create: create,
  unlink: unlink,
  rename: rename,
  mkdir: mkdir,
  rmdir: rmdir,
  init: init,
  destroy: destroy
};

//---------------------------------------------------------------------------

function usage() {
  console.log();
  console.log("Usage: node mirrorFS.js [options] sourceMountPoint mountPoint");
  console.log("(Ensure the mount point is empty and you have wrx permissions to it)\n")
  console.log("Options:");
  console.log("-d                 : make FUSE print debug statements.");
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
  options.srcRoot = args[args.length - 2];
  remaining = args.length - 4;
  i = 2;
  while (remaining--) {
    if (args[i] === '-d') {
      options.debugFuse = true;
      ++i;
    } else return false;
  }
  return true;
}

//---------------------------------------------------------------------------

(function main() {
  if (parseArgs()) {
    console.log("\nSource root: " + options.srcRoot);
    console.log("Mount point: " + options.mountPoint);
    if (options.debugFuse)
      console.log("FUSE debugging enabled");
    srcRoot = options.srcRoot;
    try {
      f4js.start(options.mountPoint, handlers, options.debugFuse);
    } catch (e) {
      console.log("Exception when starting file system: " + e);
    }
  } else {
    usage();
  }
})();
