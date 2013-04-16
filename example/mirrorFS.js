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

var f4js = require('fuse4js');
var fs = require('fs');
var pth = require('path');
var srcRoot = '/';   // The root of the file system we are mirroring
var options = {};  // See parseArgs()


//---------------------------------------------------------------------------

/*
 * Convert a node.js file system exception to a numerical errno value.
 * This is necessary because node's exc.errno property appears to have wrong
 * values based on experiments.
 * On the other hand, the exc.code string seems like a correct representation
 * of the error, so we use that instead.
 */

var errnoMap = {
    EPERM: 1,
    ENOENT: 2,
    EACCES: 13,    
    EINVAL: 22,
    ENOTEMPTY: 39
};

function excToErrno(exc) {
  var errno = errnoMap[exc.code];
  if (!errno)
    errno = errnoMap.EPERM; // default to EPERM
  return errno;
}

//---------------------------------------------------------------------------

/*
 * Handler for the getattr() system call.
 * path: the path to the file
 * cb: a callback of the form cb(err, stat), where err is the Posix return code
 *     and stat is the result in the form of a stat structure (when err === 0)
 */
function getattr(path, cb) {	  
  var path = pth.join(srcRoot, path);
  return fs.lstat(path, function lstatCb(err, stats) {
    if (err)      
      return cb(-excToErrno(err));
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
function readdir(path, cb) {
  var path = pth.join(srcRoot, path);
  return fs.readdir(path, function readdirCb(err, files) {
    if (err)      
      return cb(-excToErrno(err));
    return cb(0, files);
  });
}

//---------------------------------------------------------------------------

/*
 * Handler for the readlink() system call.
 * path: the path to the file
 * cb: a callback of the form cb(err, name), where err is the Posix return code
 *     and name is symlink target (when err === 0).
 */
function readlink(path, cb) {
  var path = pth.join(srcRoot, path);
  return fs.readlink(path, function readlinkCb(err, name) {
    if (err)      
      return cb(-excToErrno(err));
    return cb(0, name);
  });
}

//---------------------------------------------------------------------------

/*
 * Handler for the chmod() system call.
 * path: the path to the file
 * mode: the desired permissions
 * cb: a callback of the form cb(err), where err is the Posix return code.
 */
function chmod(path, mode, cb) {
  var path = pth.join(srcRoot, path);
  return fs.chmod(path, mode, function chmodCb(err) {
    if (err)
      return cb(-excToErrno(err));
    return cb(0);
  });
}

//---------------------------------------------------------------------------

/*
 * Converts numerical open() flags to node.js fs.open() 'flags' string.
 */
function convertOpenFlags(openFlags) {
  switch (openFlags & 3) {
  case 0:                    
    return 'r';              // O_RDONLY
  case 1:
    return 'w';              // O_WRONLY
  case 2:
    return 'r+';             // O_RDWR
  }
}

//---------------------------------------------------------------------------

/*
 * Handler for the open() system call.
 * path: the path to the file
 * flags: requested access flags as documented in open(2)
 * cb: a callback of the form cb(err, [fh]), where err is the Posix return code
 *     and fh is an optional numerical file handle, which is passed to subsequent
 *     read(), write(), and release() calls (set to 0 if fh is unspecified)
 */
function open(path, flags, cb) {
  var path = pth.join(srcRoot, path);
  var flags = convertOpenFlags(flags);
  fs.open(path, flags, 0666, function openCb(err, fd) {
    if (err)      
      return cb(-excToErrno(err));
    cb(0, fd);    
  });
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
  fs.read(fh, buf, 0, len, offset, function readCb(err, bytesRead, buffer) {
    if (err)      
      return cb(-excToErrno(err));
    cb(bytesRead);
  });
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
  fs.write(fh, buf, 0, len, offset, function writeCb(err, bytesWritten, buffer) {
    if (err)      
      return cb(-excToErrno(err));
    cb(bytesWritten);
  });
}

//---------------------------------------------------------------------------

/*
 * Handler for the release() system call.
 * path: the path to the file
 * fh:  the optional file handle originally returned by open(), or 0 if it wasn't
 * cb: a callback of the form cb(err), where err is the Posix return code.
 */
function release(path, fh, cb) {
  fs.close(fh, function closeCb(err) {
    if (err)      
      return cb(-excToErrno(err));
    cb(0);
  });
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
  var path = pth.join(srcRoot, path);
  fs.open(path, 'w', mode, function openCb(err, fd) {
    if (err)      
      return cb(-excToErrno(err));
    cb(0, fd);    
  });
}

//---------------------------------------------------------------------------

/*
 * Handler for the unlink() system call.
 * path: the path to the file
 * cb: a callback of the form cb(err), where err is the Posix return code.
 */
function unlink(path, cb) {
  var path = pth.join(srcRoot, path);
  fs.unlink(path, function unlinkCb(err) {
    if (err)      
      return cb(-excToErrno(err));
    cb(0);
  });
}

//---------------------------------------------------------------------------

/*
 * Handler for the rename() system call.
 * src: the path of the file or directory to rename
 * dst: the new path
 * cb: a callback of the form cb(err), where err is the Posix return code.
 */
function rename(src, dst, cb) {
  src = pth.join(srcRoot, src);
  dst = pth.join(srcRoot, dst);
  fs.rename(src, dst, function renameCb(err) {
    if (err)      
      return cb(-excToErrno(err));
    cb(0);
  });
}

//---------------------------------------------------------------------------

/*
 * Handler for the mkdir() system call.
 * path: the path of the new directory
 * mode: the desired permissions of the new directory
 * cb: a callback of the form cb(err), where err is the Posix return code.
 */
function mkdir(path, mode, cb) {
  var path = pth.join(srcRoot, path);
  fs.mkdir(path, mode, function mkdirCb(err) {
    if (err)      
      return cb(-excToErrno(err));
    cb(0);
  });
}

//---------------------------------------------------------------------------

/*
 * Handler for the rmdir() system call.
 * path: the path of the directory to remove
 * cb: a callback of the form cb(err), where err is the Posix return code.
 */
function rmdir(path, cb) {
  var path = pth.join(srcRoot, path);
  fs.rmdir(path, function rmdirCb(err) {
    if (err)      
      return cb(-excToErrno(err));
    cb(0);
  });

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
  readlink: readlink,
  chmod: chmod,
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
