/*
 * 
 * ll_jsonFS.js
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
var obj = null;   // The JSON object we'll be exposing as a file system
var options = {};  // See parseArgs()


const SUCCESS   =  0;
const EPERM     = -1;
const ENOENT    = -2;
const EIO       = -5;
const EEXIST    = -17;
const ENOTDIR   = -20;
const EISDIR    = -21;
const ENOTEMPTY = -39

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
 * Handler for the destroy() FUSE hook. You can perform clean up tasks here.
 * cb: a callback to call when you're done. It takes no arguments.
 */
function destroy(cb) {
    console.log("File system stopped");      
    cb();
}

//---------------------------------------------------------------------------

function debuglog(str) {
    console.log(str);
}

//---------------------------------------------------------------------------

function randomInt (low, high) {
    return Math.floor(Math.random() * (high - low) + low);
}

//---------------------------------------------------------------------------

function randomInode() {
    return randomInt(0, Math.pow(2, 32));
}

//---------------------------------------------------------------------------

/*
 * Handler for the lookup() system call.
 * parent: the inode of the parent
 * name: the object to lookup
 * cb: a callback of the form cb(err, entry, stat), where err is the Posix return code
 *     and entry is the result in the form of a fuse_entry_param structure (when err == 0)
 *     and stat is the result in the form of a stat structure (when err == 0)
 */
function lookup(parent, name, cb) {	
    var entry = {};
    var stat = {};
    var err = SUCCESS; // assume success

    debuglog("lookup parent " + parent + " name " + name);

    try {
	content = fs.readFileSync(options.inoDir + "/" + parent + ".json", 'utf8');
    } catch (e) {
	console.log("Exception when reading file: " + e);
	cb(EIO, entry, stat);
	return ;
    }

    parent_node = JSON.parse(content);

    entries = parent_node["entries"];
    obj_entry = entries[name];
    if (obj_entry == undefined) {
	err = ENOENT;
    } else {
	entry.ino = obj_entry.ino;

	try {
	    obj_content = fs.readFileSync(options.inoDir + "/" + obj_entry.ino + ".json", 'utf8');
	} catch (e) {
	    console.log("Exception when reading file: " + e);
	    cb(EIO, entry, stat);
	    return ;
	}
	obj_node = JSON.parse(obj_content);
	stat = obj_node.stat;
    }
    cb(err, entry, stat);
};

//---------------------------------------------------------------------------

/*
 * Handler for the getattr() system call.
 * ino: the inode of the file
 * cb: a callback of the form cb(err, stat), where err is the Posix return code
 *     and stat is the result in the form of a stat structure (when err === 0)
 */
function getattr(ino, cb) {	
    var stat = {};
    var err = SUCCESS; // assume success

    debuglog("getattr ino " + ino);

    try {
	content = fs.readFileSync(options.inoDir + "/" + ino + ".json", 'utf8');
    } catch (e) {
	console.log("Exception when reading file: " + e);
	cb(EIO, stat);
	return ;
    }

    obj_node = JSON.parse(content);

    cb(err, obj_node.stat);
};

//---------------------------------------------------------------------------

/*
 * Handler for the setattr() system call.
 * ino: the inode of the file
 * istat: the new stat
 * to_set: the mask of attributes to set
 * cb: a callback of the form cb(err, stat), where err is the Posix return code
 *     and stat is the result in the form of a stat structure (when err === 0)
 */
function setattr(ino, istat, to_set, cb) {	
    var stat = {}
    var err = SUCCESS; // assume success

    debuglog("setattr ino " + ino + " size=" + istat.size + " to_set=" + to_set);

    try {
	content = fs.readFileSync(options.inoDir + "/" + ino + ".json", 'utf8');
    } catch (e) {
	console.log("Exception when reading file: " + e);
	cb(EIO, stat);
	return ;
    }

    obj_node = JSON.parse(content);

    node_type = obj_node.stat.mode & 0170000;
    switch (node_type) {
    case 040000: // directory
	break;
	
    case 0100000: // file

	if (to_set == 1<<3 && istat.size == 0) {
	    //truncate
	    obj_node.content = "";
	}
	obj_node.stat.size = obj_node.content.length;
	break;
	
    default:
	break;
    }

    try {
	fs.writeFileSync(options.inoDir + "/" + ino + ".json", JSON.stringify(obj_node, null, '  '), 'utf8');
    } catch (e) {
	console.log("Exception when writing file: " + e);
	cb(EIO, stat);
	return ;
    }


    cb(err, obj_node.stat);
};

//---------------------------------------------------------------------------

/*
 * Handler for the readlink() system call.
 * ino: the ino to the file
 * buf: the Buffer to write the data to
 * cb: a callback of the form cb(err), where err is the Posix return code.
 *     A positive value represents the number of bytes actually read.
 */
function readlink(ino, offset, len, buf, cb) {
    var err = SUCCESS; // assume success

    debuglog("readlink ino " + ino);

    try {
	content = fs.readFileSync(options.inoDir + "/" + ino + ".json", 'utf8');
    } catch (e) {
	console.log("Exception when reading file: " + e);
	cb(EIO);
	return ;
    }

    file = JSON.parse(content);

    var maxBytes;
    var data;
    
    maxBytes = file.content.length;
    if (len > maxBytes) {
	len = maxBytes;
    }
    data = file.content.substring(offset, len);
    buf.write(data, 0, len, 'ascii'); //include zero
    err = len;

    cb(err);
}

//---------------------------------------------------------------------------

/*
 * Handler for the mknod() system call.
 * parent: the inode of the parent
 * name: the object to create
 * mode
 * rdev
 * cb: a callback of the form cb(err, entry, stat), where err is the Posix return code
 *     and entry is the result in the form of a fuse_entry_param structure (when err == 0)
 *     and stat is the result in the form of a stat structure (when err == 0)
 */
function mknod(parent, name, mode, rdev, cb) {	
    var entry = {};
    var stat = {};
    var err = SUCCESS; // assume success

    debuglog("mknod parent " + parent + " name " + name);

    try {
	content = fs.readFileSync(options.inoDir + "/" + parent + ".json", 'utf8');
    } catch (e) {
	console.log("Exception when reading file: " + e);
	cb(EIO, stat);
	return ;
    }

    parent_node = JSON.parse(content);

    entries = parent_node["entries"];
    obj_entry = entries[name];
    if (obj_entry != undefined) {
	err = EEXIST;
    } else {
	obj_node = {};
	obj_node.stat = {};
	entry.ino = obj_node.stat.ino = randomInode();
	obj_node.stat.mode = mode;
	obj_node.stat.rdev = rdev;
	obj_node.stat.size = 0;
	obj_node.stat.nlink = 1;
	stat = obj_node.stat;

	obj_node.content = "";

	try {
	    fs.writeFileSync(options.inoDir + "/" + entry.ino + ".json", JSON.stringify(obj_node, null, '  '), 'utf8');
	} catch (e) {
	    console.log("Exception when writing file: " + e);
	    cb(EIO, stat);
	    return ;
	}

	//update parent
	parent_node.entries[name] = {};
	parent_node.entries[name].ino = entry.ino;
	try {
	    fs.writeFileSync(options.inoDir + "/" + parent + ".json", JSON.stringify(parent_node, null, '  '), 'utf8');
	} catch (e) {
	    console.log("Exception when writing file: " + e);
	    cb(EIO, stat);
	    return ;
	}
    }
    cb(err, entry, stat);
};

//---------------------------------------------------------------------------

/*
 * Handler for the mkdir() system call.
 * parent: the inode of the parent
 * name: the object to create
 * mode
 * cb: a callback of the form cb(err, entry, stat), where err is the Posix return code
 *     and entry is the result in the form of a fuse_entry_param structure (when err == 0)
 *     and stat is the result in the form of a stat structure (when err == 0)
 */
function mkdir(parent, name, mode, cb) {	
    var entry = {};
    var stat = {};
    var err = SUCCESS; // assume success

    debuglog("mkdir parent " + parent + " name " + name + " mode " + mode);

    try {
	content = fs.readFileSync(options.inoDir + "/" + parent + ".json", 'utf8');
    } catch (e) {
	console.log("Exception when reading file: " + e);
	cb(EIO, entry, stat);
	return ;
    }

    parent_node = JSON.parse(content);

    entries = parent_node["entries"];
    obj_entry = entries[name];
    if (obj_entry != undefined) {
	err = EEXIST;
    } else {
	obj_node = {};
	obj_node.stat = {};
	entry.ino = obj_node.stat.ino = randomInode();
	obj_node.stat.mode = mode | 040000;
	obj_node.stat.size = 4096; //standard size of a directory
	obj_node.stat.nlink = 2; //me + parent
	stat = obj_node.stat;

	obj_node.entries = {};
	obj_node.entries["."] = {};
	obj_node.entries["."].ino = entry.ino;
	obj_node.entries[".."] = {};
	obj_node.entries[".."].ino = parent;

	try {
	    fs.writeFileSync(options.inoDir + "/" + entry.ino + ".json", JSON.stringify(obj_node, null, '  '), 'utf8');
	} catch (e) {
	    console.log("Exception when writing file: " + e);
	    cb(EIO, entry, stat);
	    return ;
	}

	//update parent
	parent_node.entries[name] = {};
	parent_node.entries[name].ino = entry.ino;
	parent_node.stat.nlink++;
	try {
	    fs.writeFileSync(options.inoDir + "/" + parent + ".json", JSON.stringify(parent_node, null, '  '), 'utf8');
	} catch (e) {
	    console.log("Exception when writing file: " + e);
	    cb(EIO, entry, stat);
	    return ;
	}
    }
    cb(err, entry, stat);
};

//---------------------------------------------------------------------------

/*
 * Handler for the unlink() system call.
 * parent: the inode of the parent
 * name: the object to unlink
 * cb: a callback of the form cb(err), where err is the Posix return code
 */
function unlink(parent, name, cb) {	
    var err = SUCCESS; // assume success

    debuglog("unlink parent " + parent + " name " + name);

    try {
	content = fs.readFileSync(options.inoDir + "/" + parent + ".json", 'utf8');
    } catch (e) {
	console.log("Exception when reading file: " + e);
	cb(EIO);
	return ;
    }

    parent_node = JSON.parse(content);

    entries = parent_node["entries"];
    obj_entry = entries[name];
    if (obj_entry == undefined) {
	err = ENOENT;
    } else {

	try {
	    obj_content = fs.readFileSync(options.inoDir + "/" + obj_entry.ino + ".json", 'utf8');
	} catch (e) {
	    console.log("Exception when reading file: " + e);
	    cb(EIO);
	    return ;
	}

	obj_node = JSON.parse(obj_content);

	if ((obj_node.stat.mode & 0170000) == 040000) {
	    cb(EISDIR); //-EISDIR
	    return ;
	}

	obj_node.stat.nlink--;

	if (obj_node.stat.nlink > 0) {
	    //rewrite object
	    try {
		fs.writeFileSync(options.inoDir + "/" + obj_entry.ino + ".json", JSON.stringify(obj_node, null, '  '), 'utf8');
	    } catch (e) {
		console.log("Exception when writing file: " + e);
		cb(EIO, entry, stat);
		return ;
	    }

	} else {
	    //unlink object
	    try {
		fs.unlinkSync(options.inoDir + "/" + obj_entry.ino + ".json");
	    } catch (e) {
		console.log("Exception when deleting file: " + e);
		cb(EIO);
		return ;
	    }
	}
	
	//update parent
	delete parent_node.entries[name];
	try {
	    fs.writeFileSync(options.inoDir + "/" + parent + ".json", JSON.stringify(parent_node, null, '  '), 'utf8');
	} catch (e) {
	    console.log("Exception when writing file: " + e);
	    cb(EIO);
	    return ;
	}
    }
    cb(err);
};

//---------------------------------------------------------------------------

/*
 * Handler for the rmdir() system call.
 * parent: the inode of the parent
 * name: the object to unlink
 * cb: a callback of the form cb(err), where err is the Posix return code
 */
function rmdir(parent, name, cb) {	
    var err = SUCCESS; // assume success

    debuglog("rmdir parent " + parent + " name " + name);

    try {
	content = fs.readFileSync(options.inoDir + "/" + parent + ".json", 'utf8');
    } catch (e) {
	console.log("Exception when reading file: " + e);
	cb(EIO);
	return ;
    }

    parent_node = JSON.parse(content);

    entries = parent_node["entries"];
    obj_entry = entries[name];
    if (obj_entry == undefined) {
	err = ENOENT;
    } else {

	try {
	    obj_content = fs.readFileSync(options.inoDir + "/" + obj_entry.ino + ".json", 'utf8');
	} catch (e) {
	    console.log("Exception when reading file: " + e);
	    cb(EIO);
	    return ;
	}

	obj_node = JSON.parse(obj_content);

	if (!((obj_node.stat.mode & 0170000) == 040000)) {
	    cb(ENOTDIR);
	    return ;
	}

	if (Object.keys(obj_node.entries).length > 2) {
	    err = ENOTEMPTY;
	} else {	
	    try {
		fs.unlinkSync(options.inoDir + "/" + obj_entry.ino + ".json");
	    } catch (e) {
		console.log("Exception when deleting file: " + e);
		cb(EIO);
		return ;
	    }
	    
	    //update parent
	    delete parent_node.entries[name];
	    parent_node.stat.nlink--;
	    try {
		fs.writeFileSync(options.inoDir + "/" + parent + ".json", JSON.stringify(parent_node, null, '  '), 'utf8');
	    } catch (e) {
		console.log("Exception when writing file: " + e);
		cb(EIO);
		return ;
	    }
	}
    }
    cb(err);
};

//---------------------------------------------------------------------------

/*
 * Handler for the symlink() system call.
 * parent: the inode of the parent
 * link: the content of the symlink
 * name: the name
 * cb: a callback of the form cb(err, entry, stat), where err is the Posix return code
 *     and entry is the result in the form of a fuse_entry_param structure (when err == 0)
 *     and stat is the result in the form of a stat structure (when err == 0)
 */
function symlink(parent, link, name, cb) {	
    var entry = {};
    var stat = {};
    var err = SUCCESS; // assume success

    debuglog("symlink link " + link + " parent " + parent + " name " + name);

    try  {
	content = fs.readFileSync(options.inoDir + "/" + parent + ".json", 'utf8');
    } catch (e) {
	console.log("Exception when reading file: " + e);
	cb(EIO, entry, stat );
	return ;
    }

    parent_node = JSON.parse(content);

    entries = parent_node["entries"];
    obj_entry = entries[name];
    if (obj_entry != undefined) {
	err = EEXIST;
    } else {
	obj_node = {};
	obj_node.stat = {};
	entry.ino = obj_node.stat.ino = randomInode();
	obj_node.stat.mode = 0120000;
	obj_node.stat.size = 0;
	obj_node.stat.nlink = 1;
	stat = obj_node.stat;

	obj_node.content = link;

	try {
	    fs.writeFileSync(options.inoDir + "/" + entry.ino + ".json", JSON.stringify(obj_node, null, '  '), 'utf8');
	} catch (e) {
	    console.log("Exception when writing file: " + e);
	    cb(EIO, stat);
	    return ;
	}

	//update parent
	parent_node.entries[name] = {};
	parent_node.entries[name].ino = entry.ino;
	try {
	    fs.writeFileSync(options.inoDir + "/" + parent + ".json", JSON.stringify(parent_node, null, '  '), 'utf8');
	} catch (e) {
	    console.log("Exception when writing file: " + e);
	    cb(EIO, stat);
	    return ;
	}

    }
    cb(err, entry, stat);
};

//---------------------------------------------------------------------------

/*
 * Handler for the rename() system call.
 * parent: the inode of the parent
 * name: the old name of the object
 * newparent: the inode of the new parent
 * newname: the new name
 * cb: a callback of the form cb(err), where err is the Posix return code
 */
function rename(parent, name, newparent, newname, cb) {	
    var entry = {};
    var stat = {};
    var err = SUCCESS; // assume success

    debuglog("rename parent " + parent + " name " +  name + " newparent " + newparent + " newname " + newname);

    //read old parent
    try  {
	content = fs.readFileSync(options.inoDir + "/" + parent + ".json", 'utf8');
    } catch (e) {
	console.log("Exception when reading file: " + e);
	cb(EIO);
	return ;
    }

    parent_node = JSON.parse(content);

    entries = parent_node["entries"];
    obj_entry = entries[name];
    if (obj_entry == undefined) {
	cb(ENOENT);
	return ;
    }
    
    if (newparent == parent) {
	new_obj_entry = entries[newname];
    } else {
	//read new parent
	try  {
	    content = fs.readFileSync(options.inoDir + "/" + newparent + ".json", 'utf8');
	} catch (e) {
	    console.log("Exception when reading file: " + e);
	    cb(EIO);
	    return ;
	}
	
	newparent_node = JSON.parse(content);
	
	new_entries = newparent_node["entries"];
	new_obj_entry = new_entries[newname];
    }
	    
    if (new_obj_entry != undefined) {
	cb(EEXIST);
	return ;
    }

    //read object
    try {
	obj_content = fs.readFileSync(options.inoDir + "/" + obj_entry.ino + ".json", 'utf8');
    } catch (e) {
	console.log("Exception when reading file: " + e);
	cb(EIO);
	return ;
    }
    
    obj_node = JSON.parse(obj_content);
    
    //if directory update parent
    if ((obj_node.stat.mode & 0170000) == 040000) {

	if (newparent != parent) {    
	    obj_node.entries[".."].ino = newparent;	
	    parent_node.stat.nlink--;
	    newparent_node.stat.nlink++;

	    //rewrite object
	    try {
		fs.writeFileSync(options.inoDir + "/" + obj_entry.ino + ".json", JSON.stringify(obj_node, null, '  '), 'utf8');
	    } catch (e) {
		console.log("Exception when writing file: " + e);
		cb(EIO);
		return ;
	    }
	}
    }

    //update parent
    delete parent_node.entries[name];

    if (newparent == parent) {    
	//update new parent
	parent_node.entries[newname] = {};
	parent_node.entries[newname].ino = obj_entry.ino;
	
    } else {
	//update new parent
	newparent_node.entries[newname] = {};
	newparent_node.entries[newname].ino = obj_entry.ino;
    }

    try {
	fs.writeFileSync(options.inoDir + "/" + parent + ".json", JSON.stringify(parent_node, null, '  '), 'utf8');
    } catch (e) {
	console.log("Exception when writing file: " + e);
	cb(EIO);
	return ;
    }
    
    if (newparent != parent) {    
	try {
	    fs.writeFileSync(options.inoDir + "/" + newparent + ".json", JSON.stringify(newparent_node, null, '  '), 'utf8');
	} catch (e) {
	    console.log("Exception when writing file: " + e);
	    cb(EIO);
	    return ;
	}
    }

    cb(err);
};

//---------------------------------------------------------------------------

/*
 * Handler for the link() system call.
 * ino: the inode of the object
 * newparent: the inode of the new parent
 * newname: the new name
 * cb: a callback of the form cb(err, entry, stat), where err is the Posix return code
 *     and entry is the result in the form of a fuse_entry_param structure (when err == 0)
 *     and stat is the result in the form of a stat structure (when err == 0)
 */
function link(ino, newparent, newname, cb) {	
    var entry = {};
    var stat = {};
    var err = SUCCESS; // assume success

    debuglog("link ino " + ino + " newparent " + newparent + " newname " + newname);

    try  {
	content = fs.readFileSync(options.inoDir + "/" + newparent + ".json", 'utf8');
    } catch (e) {
	console.log("Exception when reading file: " + e);
	cb(EIO, entry, stat );
	return ;
    }

    newparent_node = JSON.parse(content);

    entries = newparent_node["entries"];
    obj_entry = entries[newname];
    if (obj_entry != undefined) {
	err = EEXIST;
    } else {

	try {
	    obj_content = fs.readFileSync(options.inoDir + "/" + ino + ".json", 'utf8');
	} catch (e) {
	    console.log("Exception when reading file: " + e);
	    cb(EIO, entry, stat );
	    return ;
	}

	obj_node = JSON.parse(obj_content);

	if ((obj_node.stat.mode & 0170000) == 040000) {
	    cb(EISDIR, entry, stat); //-EISDIR
	    return ;
	}

	obj_node.stat.nlink++;
	stat = obj_node.stat;

	entry.ino = ino;

	//rewrite object
	try {
	    fs.writeFileSync(options.inoDir + "/" + ino + ".json", JSON.stringify(obj_node, null, '  '), 'utf8');
	} catch (e) {
	    console.log("Exception when writing file: " + e);
	    cb(EIO, entry, stat);
	    return ;
	}

	//update new parent
	newparent_node.entries[newname] = {};
	newparent_node.entries[newname].ino = ino;
	try {
	    fs.writeFileSync(options.inoDir + "/" + newparent + ".json", JSON.stringify(newparent_node, null, '  '), 'utf8');
	} catch (e) {
	    console.log("Exception when writing file: " + e);
	    cb(EIO, entry, stat);
	    return ;
	}
    }
    cb(err, entry, stat);
};

//---------------------------------------------------------------------------

/*
 * Handler for the open() system call.
 * ino: the inode of the file
 * cb: a callback of the form cb(err), where err is the Posix return code
 */
function open(ino, cb) {	
    var stat = {};
    var err = SUCCESS; // assume success

    debuglog("open ino " + ino);

    cb(err);
};

//---------------------------------------------------------------------------

/*
 * Handler for the read() system call.
 * ino: the ino to the file
 * offset: the file offset to read from
 * len: the number of bytes to read
 * buf: the Buffer to write the data to
 * fh:  the optional file handle originally returned by open(), or 0 if it wasn't
 * cb: a callback of the form cb(err), where err is the Posix return code.
 *     A positive value represents the number of bytes actually read.
 */
function read(ino, offset, len, buf, fh, cb) {
    var err = SUCCESS; // assume success

    debuglog("read ino " + ino + " len=" + len);

    try {
	content = fs.readFileSync(options.inoDir + "/" + ino + ".json", 'utf8');
    } catch (e) {
	console.log("Exception when reading file: " + e);
	cb(EIO);
	return ;
    }

    file = JSON.parse(content);

    var maxBytes;
    var data;
    
    if (offset < file.content.length) {
	maxBytes = file.content.length - offset;
	if (len > maxBytes) {
	    len = maxBytes;
	}
	data = file.content.substring(offset, len);
	buf.write(data, 0, len, 'ascii');
	err = len;
    }

    cb(err);
}

//---------------------------------------------------------------------------

/*
 * Handler for the write() system call.
 * ino: the ino to the file
 * offset: the file offset to write to
 * len: the number of bytes to write
 * buf: the Buffer to read data from
 * fh:  the optional file handle originally returned by open(), or 0 if it wasn't
 * cb: a callback of the form cb(err), where err is the Posix return code.
 *     A positive value represents the number of bytes actually written.
 */
function write(ino, offset, len, buf, fh, cb) {
    var err = SUCCESS; // assume success

    debuglog("write " + ino + " " + offset + " " + len + " " + buf);

    try {
	content = fs.readFileSync(options.inoDir + "/" + ino + ".json", 'utf8');
    } catch (e) {
	console.log("Exception when reading file: " + e);
	cb(EIO);
	return ;
    }

    file = JSON.parse(content);

    var beginning, blank = '', data, ending='', numBlankChars;
    
    data = buf.toString('ascii'); // read the new data
    if (offset < file.content.length) {
	beginning = file.content.substring(0, offset);
	if (offset + data.length < file.content.length) {
	    ending = file.content.substring(offset + data.length, file.content.length)
	}
    } else {
	beginning = file.content;
	numBlankChars = offset - file.content.length;
	while (numBlankChars--) blank += ' ';
    }

    file.content = beginning + blank + data + ending;
    file.stat.size = file.content.length; //reflect stat

    try {
	fs.writeFileSync(options.inoDir + "/" + ino + ".json", JSON.stringify(file, null, '  '), 'utf8');
    } catch (e) {
	console.log("Exception when writing file: " + e);
	cb(EIO);
	return ;
    }

    err = data.length;

    cb(err);
}

//---------------------------------------------------------------------------

/*
 * Handler for the release() system call.
 * ino: the ino to the file
 * fh:  the optional file handle originally returned by open(), or 0 if it wasn't
 * cb: a callback of the form cb(err), where err is the Posix return code.
 */
function release(ino, fh, cb) {
    cb(SUCCESS);
}

//---------------------------------------------------------------------------

/*
 * Handler for the opendir() system call.
 * ino: the inode of the file
 * cb: a callback of the form cb(err), where err is the Posix return code
 */
function opendir(ino, cb) {	
    var stat = {};
    var err = SUCCESS; // assume success

    debuglog("opendir ino " + ino);

    cb(err);
};

//---------------------------------------------------------------------------

/*
 * Handler for the readdir() system call.
 * ino: the ino to the file
 * size:
 * off:
 * cb: a callback of the form cb(err, names, inodes), where err is the Posix return code
 *     and names is the result in the form of an array of names (when err == 0).
 *     and inodes is the result in the form of an array of inodes (when err == 0).
 */
function readdir(ino, size, off, cb) {
    var names = [];
    var inodes = [];
    var err = SUCCESS; // assume success

    debuglog("readdir ino " + ino + " " + size + " "+ off);
    
    try {
	content = fs.readFileSync(options.inoDir + "/" + ino + ".json", 'utf8');
    } catch (e) {
	console.log("Exception when reading file: " + e);
	cb(EIO, names, inodes);
	return ;
    }

    obj_node = JSON.parse(content);

    entries = obj_node["entries"];
    var i = 0;
    for (entry in entries) {
	entry_obj = entries[entry];
	names[i] = entry;
	inodes[i] = entry_obj.ino;
	i++;
    }
    
    cb(err, names, inodes);
}

//---------------------------------------------------------------------------

/*
 * Handler for the releasedir() system call.
 * ino: the inode of the file
 * cb: a callback of the form cb(err), where err is the Posix return code
 */
function releasedir(ino, cb) {	
    var stat = {};
    var err = SUCCESS; // assume success

    debuglog("releasedir ino " + ino);

    cb(err);
};

//---------------------------------------------------------------------------

/*
 * Handler for the access() system call.
 * ino: the inode of the file
 * cb: a callback of the form cb(err), where err is the Posix return code
 */
function access(ino, cb) {	
    var stat = {};
    var err = SUCCESS; // assume success

    debuglog("access ino " + ino);

    try {
	content = fs.readFileSync(options.inoDir + "/" + ino + ".json", 'utf8');
    } catch (e) {
	console.log("Exception when reading file: " + e);
	cb(EIO);
	return ;
    }

    obj_node = JSON.parse(content);

    //check mode

    cb(err);
};

//---------------------------------------------------------------------------

var handlers = {
    init: init,
    destroy: destroy,
    lookup: lookup,
    getattr: getattr,
    setattr: setattr,
    readlink: readlink,
    mknod: mknod,
    mkdir: mkdir,
    unlink: unlink,
    rmdir: rmdir,
    symlink: symlink,
    rename: rename,
    link: link,
    open: open,
    read: read,
    write: write,
    release: release,
    opendir: opendir,
    readdir: readdir,
    releasedir: releasedir,

    access: access
};

//---------------------------------------------------------------------------

function usage() {
    console.log();
    console.log("Usage: node ll_jsonFS.js [options] inoDir mountPoint");
    console.log("(Ensure the mount point is empty and you have wrx permissions to it)\n")
    console.log("Options:");
    console.log("-d                 : make FUSE print debug statements.");
    console.log("-a                 : add allow_other option to mount (might need user_allow_other in system fuse config file).");
    console.log();
    console.log("Example:");
    console.log("node example/jsonFS.fs -d example /tmp/mnt");
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
    options.inoDir = args[args.length - 2];
    remaining = args.length - 4;
    i = 2;
    while (remaining--) {
	if (args[i] === '-d') {
	    options.debugFuse = true;
	    ++i;
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
	console.log("\nInode directory: " + options.inoDir);
	console.log("Mount point: " + options.mountPoint);
	if (options.debugFuse)
	    console.log("FUSE debugging enabled");
	try {
	    var opts = [];
	    if (options.allowOthers) {
		opts.push('-o');
		opts.push('allow_other');
	    }
	    f4js.llstart(options.mountPoint, handlers, options.debugFuse, opts);
	} catch (e) {
	    console.log("Exception when starting file system: " + e);
	}
    } else {
	usage();
    }
})();
