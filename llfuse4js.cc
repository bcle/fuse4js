/*
 * 
 * llfuse4js.cc
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

#include <node.h>
#include <node_buffer.h>
#include <v8.h>

#define FUSE_USE_VERSION 26

#define DEBUG

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <fuse.h>
#include <fuse/fuse_lowlevel.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

#include <fuse.h>
#include <semaphore.h>
#include <string>
#include <iostream>
#include <sstream>
#include <stdlib.h>

using namespace v8;

extern void ConvertDate(Handle<Object> &stat,
			std::string name,
			struct timespec *out);

// ---------------------------------------------------------------------------

static struct {
  bool enableFuseDebug;
  char **extraArgv;
  size_t extraArgc;
  uv_async_t async;
  sem_t *psem;
  pthread_t fuse_thread;
  std::string root;
  Persistent<Object> handlers;
  Persistent<Object> nodeBuffer;  
} llf4js;

enum llfuseop_t {  
  OP_INIT = 0,
  OP_DESTROY,
  OP_LOOKUP,
  OP_FORGET,
  OP_GETATTR,
  OP_SETATTR, /* 5 */
  OP_READLINK,
  OP_MKNOD,
  OP_MKDIR,
  OP_UNLINK,
  OP_RMDIR,   /* 10 */
  OP_SYMLINK,
  OP_RENAME,
  OP_LINK,
  OP_OPEN,
  OP_READ,    /* 15 */
  OP_WRITE,
  OP_FLUSH,
  OP_RELEASE,
  OP_FSYNC,
  OP_OPENDIR, /* 20 */
  OP_READDIR,
  OP_RELEASEDIR,
  OP_FSYNCDIR,
  OP_STATFS,
  OP_SETXATTR,/* 25 */
  OP_GETXATTR,
  OP_LISTXATTR,
  OP_REMOVEXATTR,
  OP_ACCESS,
  OP_CREATE   /* 30 */
};

const char* llfuseop_names[] = {
  "init",
  "destroy",
  "lookup",
  "forget",
  "getattr",
  "setattr",
  "readlink",
  "mknod",
  "mkdir",
  "unlink",
  "rmdir",
  "symlink",
  "rename",
  "link",
  "open",
  "read",
  "write",
  "flush",
  "release",
  "fsync",
  "opendir",
  "readdir",
  "releasedir",
  "fsyncdir",
  "statfs",
  "setxattr",
  "getxattr",
  "listxattr",
  "removexattr",
  "access",
  "create",
};

static struct {
  enum llfuseop_t op;
  fuse_req_t req;
  fuse_ino_t ino;
  struct fuse_file_info *info;
  union {
    struct {
      const char *name;
      struct fuse_entry_param entry;
    } lookup;
    struct {
      struct stat stbuf;
    } getattr;
    struct {
      const char *name;
      mode_t mode;
      dev_t rdev;
    } mknod;
    struct {
      const char *name;
      mode_t mode;
    } mkdir;
    struct {
      const char *name;
    } unlink;
    struct {
      const char *name;
    } rmdir;
    struct {
      const char *link;
      const char *name;
    } symlink;
    struct {
      const char *name;
      ino_t newparent;
      const char *newname;
    } rename;
    struct {
      ino_t newparent;
      const char *newname;
    } link;
    struct {
      struct stat srcStbuf;
      struct stat dstStbuf;
      int to_set;
    } setattr;
    struct {
      int mask;
    } access;
    struct {
      size_t size;
      off_t offset;
    } readdir;
   struct {
     off_t offset;
     size_t len;
     const char *srcBuf;
    } rw;
  } u;
  int retval;
} llf4js_cmd;

// ---------------------------------------------------------------------------

std::string llf4js_semaphore_name()
{
   std::ostringstream o;
   o << "llfuse4js" << getpid();
   return o.str();
}

// ---------------------------------------------------------------------------

static int llf4js_rpc(fuse_req_t req, enum llfuseop_t op)
{
  llf4js_cmd.req = req;
  llf4js_cmd.op = op;
  uv_async_send(&llf4js.async);
  sem_wait(llf4js.psem);
  return llf4js_cmd.retval;  
}

// ---------------------------------------------------------------------------


void llf4js_init(void *user_data, struct fuse_conn_info *conn)
{
  // We currently always return NULL
  llf4js_rpc(NULL, OP_INIT);
}

// ---------------------------------------------------------------------------

void llf4js_destroy(void *user_data)
{
  // We currently ignore the data pointer, which init() always sets to NULL
  llf4js_rpc(NULL, OP_DESTROY);
}

// ---------------------------------------------------------------------------

static void llf4js_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
  llf4js_cmd.ino = parent;
  llf4js_cmd.u.lookup.name = name;
  llf4js_rpc(req, OP_LOOKUP);
}

// ---------------------------------------------------------------------------

static void llf4js_forget(fuse_req_t req, fuse_ino_t ino, unsigned long nlookup)
{
  llf4js_rpc(req, OP_FORGET);
}

// ---------------------------------------------------------------------------

static void llf4js_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
  llf4js_cmd.ino = ino;
  llf4js_cmd.info = fi;
  llf4js_rpc(req, OP_GETATTR);
}

// ---------------------------------------------------------------------------

void llf4js_setattr(fuse_req_t req, fuse_ino_t ino, struct stat *attr,
		    int to_set, struct fuse_file_info *fi)
{
  llf4js_cmd.ino = ino;
  llf4js_cmd.info = fi;
  llf4js_cmd.u.setattr.srcStbuf = *attr;
  llf4js_cmd.u.setattr.to_set = to_set;
  llf4js_rpc(req, OP_SETATTR);
}

// ---------------------------------------------------------------------------

void llf4js_readlink(fuse_req_t req, fuse_ino_t ino)
{
  llf4js_cmd.ino = ino;
  llf4js_cmd.u.rw.offset = 0;
  llf4js_cmd.u.rw.len = 4096;
  llf4js_cmd.u.rw.srcBuf = NULL;
  llf4js_rpc(req, OP_READLINK);
}

// ---------------------------------------------------------------------------

void llf4js_mknod(fuse_req_t req, fuse_ino_t parent, const char *name,
		  mode_t mode, dev_t rdev)
{
  llf4js_cmd.ino = parent;
  llf4js_cmd.u.mknod.name = name;
  llf4js_cmd.u.mknod.mode = mode;
  llf4js_cmd.u.mknod.rdev = rdev;
  llf4js_rpc(req, OP_MKNOD);
}

// ---------------------------------------------------------------------------

void llf4js_mkdir(fuse_req_t req, fuse_ino_t parent, const char *name,
		  mode_t mode)
{
  llf4js_cmd.ino = parent;
  llf4js_cmd.u.mkdir.name = name;
  llf4js_cmd.u.mkdir.mode = mode;
  llf4js_rpc(req, OP_MKDIR);
}

// ---------------------------------------------------------------------------

void llf4js_unlink(fuse_req_t req, fuse_ino_t parent, const char *name)
{
  llf4js_cmd.ino = parent;
  llf4js_cmd.u.unlink.name = name;
  llf4js_rpc(req, OP_UNLINK);
}

// ---------------------------------------------------------------------------

void llf4js_rmdir(fuse_req_t req, fuse_ino_t parent, const char *name)
{
  llf4js_cmd.ino = parent;
  llf4js_cmd.u.rmdir.name = name;
  llf4js_rpc(req, OP_RMDIR);
}

// ---------------------------------------------------------------------------

void llf4js_symlink(fuse_req_t req, const char *link, fuse_ino_t parent,
		    const char *name)
{
  llf4js_cmd.ino = parent;
  llf4js_cmd.u.symlink.link = link;
  llf4js_cmd.u.symlink.name = name;
  llf4js_rpc(req, OP_SYMLINK);
}

// ---------------------------------------------------------------------------

void llf4js_rename(fuse_req_t req, fuse_ino_t parent, const char *name,
		   fuse_ino_t newparent, const char *newname)
{
  llf4js_cmd.ino = parent;
  llf4js_cmd.u.rename.name = name;
  llf4js_cmd.u.rename.newparent = newparent;
  llf4js_cmd.u.rename.newname = newname;
  llf4js_rpc(req, OP_RENAME);
}

// ---------------------------------------------------------------------------

void llf4js_link(fuse_req_t req, fuse_ino_t ino, fuse_ino_t newparent,
		 const char *newname)
{
  llf4js_cmd.ino = ino;
  llf4js_cmd.u.link.newparent = newparent;
  llf4js_cmd.u.link.newname = newname;
  llf4js_rpc(req, OP_LINK);
}

// ---------------------------------------------------------------------------

void llf4js_open(fuse_req_t req, fuse_ino_t ino,
		 struct fuse_file_info *fi)
{
  llf4js_cmd.ino = ino;
  llf4js_cmd.info = fi;
  llf4js_rpc(req, OP_OPEN);
}

// ---------------------------------------------------------------------------

void llf4js_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
		 struct fuse_file_info *fi)
{
  llf4js_cmd.ino = ino;
  llf4js_cmd.info = fi;
  llf4js_cmd.u.rw.offset = off;
  llf4js_cmd.u.rw.len = size;
  llf4js_cmd.u.rw.srcBuf = NULL;
  llf4js_rpc(req, OP_READ);
}

// ---------------------------------------------------------------------------

void llf4js_write(fuse_req_t req, fuse_ino_t ino, const char *buf,
		  size_t size, off_t off, struct fuse_file_info *fi)
{
  llf4js_cmd.ino = ino;
  llf4js_cmd.info = fi;
  llf4js_cmd.u.rw.offset = off;
  llf4js_cmd.u.rw.len = size;
  llf4js_cmd.u.rw.srcBuf = buf;
  llf4js_rpc(req, OP_WRITE);
}

// ---------------------------------------------------------------------------

void llf4js_flush(fuse_req_t req, fuse_ino_t ino,
		  struct fuse_file_info *fi)
{
  llf4js_cmd.ino = ino;
  llf4js_cmd.info = fi;
  llf4js_rpc(req, OP_FLUSH);
}

// ---------------------------------------------------------------------------

void llf4js_release(fuse_req_t req, fuse_ino_t ino,
		    struct fuse_file_info *fi)
{
  llf4js_cmd.ino = ino;
  llf4js_cmd.info = fi;
  llf4js_rpc(req, OP_RELEASE);
}

// ---------------------------------------------------------------------------

void llf4js_fsync(fuse_req_t req, fuse_ino_t ino, int datasync,
		  struct fuse_file_info *fi)
{
  llf4js_cmd.ino = ino;
  llf4js_cmd.info = fi;
  llf4js_rpc(req, OP_FSYNC);
}

// ---------------------------------------------------------------------------

void llf4js_opendir(fuse_req_t req, fuse_ino_t ino,
		    struct fuse_file_info *fi)
{
  llf4js_cmd.ino = ino;
  llf4js_cmd.info = fi;
  llf4js_rpc(req, OP_OPENDIR);
}

// ---------------------------------------------------------------------------

void llf4js_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
		    struct fuse_file_info *fi)
{
  llf4js_cmd.ino = ino;
  llf4js_cmd.info = fi;
  llf4js_cmd.u.readdir.size = size;
  llf4js_cmd.u.readdir.offset = off;
  llf4js_rpc(req, OP_READDIR);
}

// ---------------------------------------------------------------------------

void llf4js_releasedir(fuse_req_t req, fuse_ino_t ino,
		       struct fuse_file_info *fi)
{
  llf4js_cmd.ino = ino;
  llf4js_cmd.info = fi;
  llf4js_rpc(req, OP_RELEASEDIR);
}

// ---------------------------------------------------------------------------

void llf4js_fsyncdir(fuse_req_t req, fuse_ino_t ino, int datasync,
		     struct fuse_file_info *fi)
{
  llf4js_rpc(req, OP_FSYNCDIR);
}

// ---------------------------------------------------------------------------

void llf4js_statfs(fuse_req_t req, fuse_ino_t ino)
{
  llf4js_rpc(req, OP_STATFS);
}

// ---------------------------------------------------------------------------

void llf4js_setxattr(fuse_req_t req, fuse_ino_t ino, const char *name,
		     const char *value, size_t size, int flags)
{
  llf4js_rpc(req, OP_SETXATTR);
}

// ---------------------------------------------------------------------------

void llf4js_getxattr(fuse_req_t req, fuse_ino_t ino, const char *name,
		     size_t size)
{
  llf4js_rpc(req, OP_GETXATTR);
}

// ---------------------------------------------------------------------------

void llf4js_listxattr(fuse_req_t req, fuse_ino_t ino, size_t size)
{
  llf4js_rpc(req, OP_LISTXATTR);
}

// ---------------------------------------------------------------------------

void llf4js_removexattr(fuse_req_t req, fuse_ino_t ino, const char *name)
{
  llf4js_rpc(req, OP_REMOVEXATTR);
}

// ---------------------------------------------------------------------------

void llf4js_access(fuse_req_t req, fuse_ino_t ino, int mask)
{
  llf4js_cmd.ino = ino;
  llf4js_cmd.u.access.mask = mask;
  llf4js_rpc(req, OP_ACCESS);
}

// ---------------------------------------------------------------------------

void llf4js_create(fuse_req_t req, fuse_ino_t parent, const char *name,
		   mode_t mode, struct fuse_file_info *fi)
{
  llf4js_rpc(req, OP_CREATE);
}

// ---------------------------------------------------------------------------

void *llfuse_thread(void *)
{
  struct fuse_lowlevel_ops ops = { 0 };
  ops.init = llf4js_init;
  ops.destroy = llf4js_destroy;
  ops.lookup = llf4js_lookup;
  //ops.forget = llf4js_forget;
  ops.getattr = llf4js_getattr;
  ops.setattr = llf4js_setattr;
  ops.readlink = llf4js_readlink;
  ops.mknod = llf4js_mknod;
  ops.mkdir = llf4js_mkdir;
  ops.unlink = llf4js_unlink;
  ops.rmdir = llf4js_rmdir;
  ops.symlink = llf4js_symlink;
  ops.rename = llf4js_rename;
  ops.link = llf4js_link;
  ops.open = llf4js_open;
  ops.read = llf4js_read;
  ops.write = llf4js_write;
  //ops.flush = llf4js_flush;
  ops.release = llf4js_release;
  ops.fsync = llf4js_fsync;
  ops.opendir = llf4js_opendir;
  ops.readdir = llf4js_readdir;
  ops.releasedir = llf4js_releasedir;
  ops.fsyncdir = llf4js_fsyncdir;
  //ops.statfs = llf4js_statfs;
  //ops.setxattr = llf4js_setxattr;
  //ops.getxattr = llf4js_getxattr;
  //ops.listxattr = llf4js_listxattr;
  //ops.removexattr = llf4js_removexattr;
  ops.access = llf4js_access;
  //ops.create = llf4js_create;

  char *argv_debug[] = { (char*)"dummy", (char*)"-d" };
  char *argv_nodebug[] = { (char*)"dummy" };
  char **argv;

  if (llf4js.enableFuseDebug)
    argv = argv_debug;
  else
    argv = argv_nodebug;

  int initialArgc = sizeof(argv) / sizeof(char*);
  char **argvIncludingExtraArgs = (char**)malloc(sizeof(char*) * (initialArgc + llf4js.extraArgc));
  memcpy(argvIncludingExtraArgs, argv, sizeof(argv));
  memcpy(argvIncludingExtraArgs + initialArgc, llf4js.extraArgv, sizeof(char*) * llf4js.extraArgc);

  struct fuse_args args = FUSE_ARGS_INIT(initialArgc + llf4js.extraArgc, argvIncludingExtraArgs);
  struct fuse_chan *ch;
  char *mountpoint = (char*)llf4js.root.c_str();
  int err = -1;

  if ((ch = fuse_mount(mountpoint, &args)) != NULL) {
    struct fuse_session *se;
    
    se = fuse_lowlevel_new(&args, &ops, sizeof (ops), NULL);
    if (se != NULL) {
      if (fuse_set_signal_handlers(se) != -1) {
	fuse_session_add_chan(se, ch);
	err = fuse_session_loop(se);
	fuse_remove_signal_handlers(se);
	fuse_session_remove_chan(ch);
      }
      fuse_session_destroy(se);
    }
    fuse_unmount(mountpoint, ch);
  }
  fuse_opt_free_args(&args);

  return NULL;
}

// ---------------------------------------------------------------------------

void LLProcessReturnValue(const Arguments& args)
{
  if (args.Length() >= 1 && args[0]->IsNumber()) {
    Local<Number> retval = Local<Number>::Cast(args[0]);
    llf4js_cmd.retval = (int)retval->Value();
    
#ifdef DEBUG
    std::cout << "retval " << llf4js_cmd.retval << "\n";
#endif
  }  
}

// ---------------------------------------------------------------------------

Handle<Value> LLGenericNoReplyCompletion(const Arguments& args)
{
  HandleScope scope;
  bool exiting = (llf4js_cmd.op == OP_DESTROY);

  LLProcessReturnValue(args);
  sem_post(llf4js.psem);  
  if (exiting) {
    pthread_join(llf4js.fuse_thread, NULL);
    uv_unref((uv_loop_t*) &llf4js.async);
    sem_close(llf4js.psem);
    sem_unlink(llf4js_semaphore_name().c_str());    
  }
  return scope.Close(Undefined());    
}

// ---------------------------------------------------------------------------

Handle<Value> LLGenericCompletion(const Arguments& args)
{
  HandleScope scope;

  LLProcessReturnValue(args);
  fuse_reply_err(llf4js_cmd.req, -llf4js_cmd.retval);
  sem_post(llf4js.psem);  
  return scope.Close(Undefined());    
}

// ---------------------------------------------------------------------------

void statGet(Handle<Object> stat, struct stat *stbuf)
{
  Local<Value> prop = stat->Get(String::NewSymbol("size"));
  if (!prop->IsUndefined() && prop->IsNumber()) {
    Local<Number> num = Local<Number>::Cast(prop);
    stbuf->st_size = (off_t)num->Value();
  }
  
  prop = stat->Get(String::NewSymbol("mode"));
  if (!prop->IsUndefined() && prop->IsNumber()) {
    Local<Number> num = Local<Number>::Cast(prop);
    stbuf->st_mode = (mode_t)num->Value();
  }
  
  prop = stat->Get(String::NewSymbol("nlink"));
  if (!prop->IsUndefined() && prop->IsNumber()) {
    Local<Number> num = Local<Number>::Cast(prop);
    stbuf->st_nlink = (mode_t)num->Value();
  }
  
  prop = stat->Get(String::NewSymbol("uid"));
  if (!prop->IsUndefined() && prop->IsNumber()) {
    Local<Number> num = Local<Number>::Cast(prop);
    stbuf->st_uid = (uid_t)num->Value();
  }
  
  prop = stat->Get(String::NewSymbol("gid"));
  if (!prop->IsUndefined() && prop->IsNumber()) {
    Local<Number> num = Local<Number>::Cast(prop);
    stbuf->st_gid = (gid_t)num->Value();
  }
  
#ifdef __APPLE__
  ConvertDate(stat, "mtime", &stbuf->st_mtimespec);
  ConvertDate(stat, "ctime", &stbuf->st_ctimespec);
  ConvertDate(stat, "atime", &stbuf->st_atimespec);
#else
  ConvertDate(stat, "mtime", &stbuf->st_mtim);
  ConvertDate(stat, "ctime", &stbuf->st_ctim);
  ConvertDate(stat, "atime", &stbuf->st_atim);
#endif
  
  prop = stat->Get(String::NewSymbol("ino"));
  if (!prop->IsUndefined() && prop->IsNumber()) {
    Local<Number> num = Local<Number>::Cast(prop);
    stbuf->st_ino = (ino_t)num->Value();
  }
}

static Local<Object> statSet(struct stat *stbuf)
{
  Local<Object> stat = Object::New();

  {
    Local<Number> num = Number::New((double)stbuf->st_size);
    stat->Set(String::NewSymbol("size"), num);
  }

  {
    Local<Number> num = Number::New((double)stbuf->st_mode);
    stat->Set(String::NewSymbol("mode"), num);
  }

  {
    Local<Number> num = Number::New((double)stbuf->st_nlink);
    stat->Set(String::NewSymbol("nlink"), num);
  }

  {
    Local<Number> num = Number::New((double)stbuf->st_uid);
    stat->Set(String::NewSymbol("uid"), num);
  }

  {
    Local<Number> num = Number::New((double)stbuf->st_gid);
    stat->Set(String::NewSymbol("gid"), num);
  }

#if 0  
#ifdef __APPLE__
  ConvertDateTo(stat, "mtime", &stbuf->st_mtimespec);
  ConvertDateTo(stat, "ctime", &stbuf->st_ctimespec);
  ConvertDateTo(stat, "atime", &stbuf->st_atimespec);
#else
  ConvertDateTo(stat, "mtime", &stbuf->st_mtim);
  ConvertDateTo(stat, "ctime", &stbuf->st_ctim);
  ConvertDateTo(stat, "atime", &stbuf->st_atim);
#endif
#endif
  
  {
    Local<Number> num = Number::New((double)stbuf->st_ino);
    stat->Set(String::NewSymbol("ino"), num);
  }

  return stat;
}

Handle<Value> LLLookupCompletion(const Arguments& args)
{
  HandleScope scope;
  LLProcessReturnValue(args);
 if (llf4js_cmd.retval == 0 && args.Length() >= 3 && args[1]->IsObject() && args[2]->IsObject()) {

    memset(&llf4js_cmd.u.lookup.entry, 0, sizeof(llf4js_cmd.u.lookup.entry));

    Handle<Object> entry = Handle<Object>::Cast(args[1]);

    Local<Value> prop = entry->Get(String::NewSymbol("ino"));
    if (!prop->IsUndefined() && prop->IsNumber()) {
      Local<Number> num = Local<Number>::Cast(prop);
      llf4js_cmd.u.lookup.entry.ino = (off_t)num->Value();
    }

    Handle<Object> stat = Handle<Object>::Cast(args[2]);
    statGet(stat, &llf4js_cmd.u.lookup.entry.attr); 

    fuse_reply_entry(llf4js_cmd.req, &llf4js_cmd.u.lookup.entry);
 } else {
   fuse_reply_err(llf4js_cmd.req, -llf4js_cmd.retval);
 }
 sem_post(llf4js.psem);  
 return scope.Close(Undefined());    
}

// ---------------------------------------------------------------------------

Handle<Value> LLGetAttrCompletion(const Arguments& args)
{
  HandleScope scope;
  LLProcessReturnValue(args);
  if (llf4js_cmd.retval == 0 && args.Length() >= 2 && args[1]->IsObject()) {
    memset(&llf4js_cmd.u.getattr.stbuf, 0, sizeof(llf4js_cmd.u.getattr.stbuf));
    Handle<Object> stat = Handle<Object>::Cast(args[1]);
    statGet(stat, &llf4js_cmd.u.getattr.stbuf);
    fuse_reply_attr(llf4js_cmd.req, &llf4js_cmd.u.getattr.stbuf, 10);
 } else {
   fuse_reply_err(llf4js_cmd.req, -llf4js_cmd.retval);
 }
  sem_post(llf4js.psem);  
  return scope.Close(Undefined());    
}

// ---------------------------------------------------------------------------

Handle<Value> LLSetAttrCompletion(const Arguments& args)
{
  HandleScope scope;
  LLProcessReturnValue(args);
  if (llf4js_cmd.retval == 0 && args.Length() >= 2 && args[1]->IsObject()) {
    memset(&llf4js_cmd.u.setattr.dstStbuf, 0, sizeof(llf4js_cmd.u.setattr.dstStbuf));
    Handle<Object> stat = Handle<Object>::Cast(args[1]);
    statGet(stat, &llf4js_cmd.u.setattr.dstStbuf);
    fuse_reply_attr(llf4js_cmd.req, &llf4js_cmd.u.setattr.dstStbuf, 10);
 } else {
   fuse_reply_err(llf4js_cmd.req, -llf4js_cmd.retval);
 }
  sem_post(llf4js.psem);  
  return scope.Close(Undefined());    
}

// ---------------------------------------------------------------------------

Handle<Value> LLOpenCompletion(const Arguments& args)
{
  HandleScope scope;

  LLProcessReturnValue(args);
  if (0 == llf4js_cmd.retval)
    fuse_reply_open(llf4js_cmd.req, llf4js_cmd.info);
  else
    fuse_reply_err(llf4js_cmd.req, -llf4js_cmd.retval);
  sem_post(llf4js.psem);  
  return scope.Close(Undefined());    
}

// ---------------------------------------------------------------------------

Handle<Value> LLReadCompletion(const Arguments& args)
{
  HandleScope scope;
  LLProcessReturnValue(args);    
  if (llf4js_cmd.retval >= 0) {
    char *buffer_data = node::Buffer::Data(llf4js.nodeBuffer);
    fuse_reply_buf(llf4js_cmd.req, buffer_data, llf4js_cmd.retval);
  } else {
    fuse_reply_err(llf4js_cmd.req, -llf4js_cmd.retval);
  }
  llf4js.nodeBuffer.Dispose();
  sem_post(llf4js.psem);  
  return scope.Close(Undefined());    
}

// ---------------------------------------------------------------------------

Handle<Value> LLReadlinkCompletion(const Arguments& args)
{
  HandleScope scope;
  LLProcessReturnValue(args);    
  if (llf4js_cmd.retval >= 0) {
    char *buffer_data = node::Buffer::Data(llf4js.nodeBuffer);
    buffer_data[llf4js_cmd.retval] = 0;
    fuse_reply_readlink(llf4js_cmd.req, buffer_data);
  } else {
    fuse_reply_err(llf4js_cmd.req, -llf4js_cmd.retval);
  }
  llf4js.nodeBuffer.Dispose();
  sem_post(llf4js.psem);  
  return scope.Close(Undefined());    
}

// ---------------------------------------------------------------------------

Handle<Value> LLWriteCompletion(const Arguments& args)
{
  HandleScope scope;
  LLProcessReturnValue(args);
  if (llf4js_cmd.retval >= 0)
    fuse_reply_write(llf4js_cmd.req, (size_t) llf4js_cmd.retval);
  else
    fuse_reply_err(llf4js_cmd.req, -llf4js_cmd.retval);
  llf4js.nodeBuffer.Dispose();
  sem_post(llf4js.psem);  
  return scope.Close(Undefined());    
}

// ---------------------------------------------------------------------------

Handle<Value> LLOpenDirCompletion(const Arguments& args)
{
  HandleScope scope;

  LLProcessReturnValue(args);
  if (0 == llf4js_cmd.retval)
    fuse_reply_open(llf4js_cmd.req, llf4js_cmd.info);
  else
    fuse_reply_err(llf4js_cmd.req, -llf4js_cmd.retval);
  sem_post(llf4js.psem);  
  return scope.Close(Undefined());    
}

// ---------------------------------------------------------------------------

Handle<Value> LLReadDirCompletion(const Arguments& args)
{
  HandleScope scope;

  LLProcessReturnValue(args);

  if (llf4js_cmd.retval == 0 && args.Length() >= 3 && args[1]->IsArray() && args[2]->IsArray()) {
    Handle<Array> ar = Handle<Array>::Cast(args[1]);
    Handle<Array> ar2 = Handle<Array>::Cast(args[2]);
    size_t cur_size = 0, size = llf4js_cmd.u.readdir.size, entry_size;
    char *buf = NULL;
    buf = (char *) alloca(size);

    if (llf4js_cmd.u.readdir.offset > 0) {
      //std::cout << "EOL\n";
      fuse_reply_buf(llf4js_cmd.req, NULL, 0);
      goto end;
    }

    for (uint32_t i = 0; i < ar->Length(); i++) {
      Local<Value> el = ar->Get(i);
      Local<Value> el2 = ar2->Get(i);
      
      if (!el->IsUndefined() && el->IsString() && !el2->IsUndefined() && el2->IsNumber()) {
        Local<String> name = Local<String>::Cast(el);
        Local<Number> ino = Local<Number>::Cast(el2);
        String::Utf8Value av(name);  
        struct stat st;

	entry_size = fuse_add_direntry(llf4js_cmd.req, NULL, 0, *av, NULL, 0);

	if ((cur_size + entry_size) > llf4js_cmd.u.readdir.size)
	  break ;

        memset(&st, 0, sizeof(st));
	st.st_ino = (ino_t)ino->Value();

	fuse_add_direntry(llf4js_cmd.req, buf + cur_size, size, *av, &st, cur_size + entry_size);
	size -= entry_size;
	cur_size += entry_size;

	//std::cout << "cur_size " << cur_size << " name " << *av << "\n";
      }
    }

    fuse_reply_buf(llf4js_cmd.req, buf, cur_size);

  } else {
    fuse_reply_err(llf4js_cmd.req, -llf4js_cmd.retval);
  }
 end:
  sem_post(llf4js.psem);  
  return scope.Close(Undefined());    
}

// ---------------------------------------------------------------------------

// Called from the main thread.
static void DispatchOp(uv_async_t* handle, int status)
{
  HandleScope scope;
  std::string symName(llfuseop_names[llf4js_cmd.op]);
  Local<FunctionTemplate> tpl = FunctionTemplate::New(LLGenericNoReplyCompletion); // default
  llf4js_cmd.retval = -EPERM;
  int argc = 0;
  Handle<Value> argv[6]; 
  Local<Number> ino = Number::New((double)llf4js_cmd.ino); 
  argv[argc++] = ino;
  node::Buffer* buffer = NULL; // used for read/write operations
  bool passHandle = false;

#ifdef DEBUG
  std::cout << "DispatchOp " << llfuseop_names[llf4js_cmd.op] << " ino " << llf4js_cmd.ino << "\n";
#endif

  switch (llf4js_cmd.op) {
  
  case OP_INIT:
  case OP_DESTROY:
    llf4js_cmd.retval = 0; // Will be used as the return value of OP_INIT.
    --argc;              // Ugly. Remove the first argument (path) because not needed.
    break;

  case OP_LOOKUP:
    tpl = FunctionTemplate::New(LLLookupCompletion);
    argv[argc++] = String::New(llf4js_cmd.u.lookup.name);      
    break;
    
  case OP_GETATTR:
    tpl = FunctionTemplate::New(LLGetAttrCompletion);
    break;

  case OP_SETATTR:
    tpl = FunctionTemplate::New(LLSetAttrCompletion);
    argv[argc++] = statSet(&llf4js_cmd.u.setattr.srcStbuf);
    argv[argc++] = Number::New((double)llf4js_cmd.u.setattr.to_set);      
    break;

  case OP_READLINK:
    tpl = FunctionTemplate::New(LLReadlinkCompletion);
    buffer = node::Buffer::New(llf4js_cmd.u.rw.len);
    break;

  case OP_MKNOD:
    tpl = FunctionTemplate::New(LLLookupCompletion);
    argv[argc++] = String::New(llf4js_cmd.u.mknod.name);      
    argv[argc++] = Number::New((double)llf4js_cmd.u.mknod.mode);
    argv[argc++] = Number::New((double)llf4js_cmd.u.mknod.rdev);      
    break;

  case OP_MKDIR:
    tpl = FunctionTemplate::New(LLLookupCompletion);
    argv[argc++] = String::New(llf4js_cmd.u.mkdir.name);      
    argv[argc++] = Number::New((double)llf4js_cmd.u.mkdir.mode);
    break;

  case OP_UNLINK:
    tpl = FunctionTemplate::New(LLGenericCompletion);
    argv[argc++] = String::New(llf4js_cmd.u.unlink.name);      
    break;

  case OP_RMDIR:
    tpl = FunctionTemplate::New(LLGenericCompletion);
    argv[argc++] = String::New(llf4js_cmd.u.rmdir.name);      
    break;

  case OP_SYMLINK:
    tpl = FunctionTemplate::New(LLLookupCompletion);
    argv[argc++] = String::New(llf4js_cmd.u.symlink.link);      
    argv[argc++] = String::New(llf4js_cmd.u.symlink.name);      
    break;

  case OP_RENAME:
    tpl = FunctionTemplate::New(LLGenericCompletion);
    argv[argc++] = String::New(llf4js_cmd.u.rename.name);      
    argv[argc++] = Number::New((double)llf4js_cmd.u.rename.newparent);      
    argv[argc++] = String::New(llf4js_cmd.u.rename.newname);      
    break;

  case OP_LINK:
    tpl = FunctionTemplate::New(LLLookupCompletion);
    argv[argc++] = Number::New((double)llf4js_cmd.u.link.newparent);      
    argv[argc++] = String::New(llf4js_cmd.u.link.newname);      
    break;

  case OP_OPEN:
    tpl = FunctionTemplate::New(LLOpenCompletion);
    break;

  case OP_READ:
    tpl = FunctionTemplate::New(LLReadCompletion);
    buffer = node::Buffer::New(llf4js_cmd.u.rw.len);
    passHandle = true;
    break;
    
  case OP_WRITE:
    tpl = FunctionTemplate::New(LLWriteCompletion);   
    buffer = node::Buffer::New((char*)llf4js_cmd.u.rw.srcBuf, llf4js_cmd.u.rw.len);
    passHandle = true;
    break;
    
  case OP_RELEASE:
    tpl = FunctionTemplate::New(LLGenericCompletion);
    passHandle = true;
    break;


  case OP_OPENDIR:
    tpl = FunctionTemplate::New(LLOpenDirCompletion);
    break;

  case OP_READDIR:
    tpl = FunctionTemplate::New(LLReadDirCompletion);
    argv[argc++] = Number::New((double) llf4js_cmd.u.readdir.size);
    argv[argc++] = Number::New((double) llf4js_cmd.u.readdir.offset);
    break;

  case OP_RELEASEDIR:
    tpl = FunctionTemplate::New(LLGenericCompletion);
    break;


  case OP_ACCESS:
    tpl = FunctionTemplate::New(LLGenericCompletion);
    break;
   
  default:
    break;
  }
  
  // Additional args for read/write operations
  if (buffer) { 
    // FIXME: 64-bit off_t cannot always fit in a JS number 
    argv[argc++] = Number::New((double)llf4js_cmd.u.rw.offset);  
    argv[argc++] = Number::New((double)llf4js_cmd.u.rw.len);
    llf4js.nodeBuffer = Persistent<Object>::New(buffer->handle_);   
    argv[argc++] = llf4js.nodeBuffer;
  }

  if (passHandle) {
    argv[argc++] = Number::New((double)llf4js_cmd.info->fh); // optional file handle returned by open()
  }

  Local<Function> handler = Local<Function>::Cast(llf4js.handlers->Get(String::NewSymbol(symName.c_str())));
  if (handler->IsUndefined()) {
    sem_post(llf4js.psem);
    return;
  }
  Local<Function> cb = tpl->GetFunction();
  std::string cbName = symName + "Completion";
  cb->SetName(String::NewSymbol(cbName.c_str()));
  argv[argc++] = cb;
  handler->Call(Context::GetCurrent()->Global(), argc, argv);  
}

// ---------------------------------------------------------------------------

Handle<Value> LLStart(const Arguments& args)
{
  HandleScope scope;
  if (args.Length() < 2) {
    ThrowException(Exception::TypeError(String::New("Wrong number of arguments")));
    return scope.Close(Undefined());
  }

  if (!args[0]->IsString() || !args[1]->IsObject()) {
    ThrowException(Exception::TypeError(String::New("Wrong argument types")));
    return scope.Close(Undefined());
  }

  String::Utf8Value av(args[0]);
  char *root = *av;
  if (root == NULL) {
    ThrowException(Exception::TypeError(String::New("Path is incorrect")));
    return scope.Close(Undefined());
  }
  
  llf4js.enableFuseDebug = false;
  if (args.Length() >= 3) {
    Local <Boolean> debug = args[2]->ToBoolean();
    llf4js.enableFuseDebug = debug->BooleanValue();
  }

  llf4js.extraArgc = 0;
  if (args.Length() >= 4) {
    if (!args[3]->IsArray()) {
        ThrowException(Exception::TypeError(String::New("Wrong argument types")));
        return scope.Close(Undefined());
    }

    Handle<Array> mountArgs = Handle<Array>::Cast(args[3]);
    llf4js.extraArgv = (char**)malloc(mountArgs->Length() * sizeof(char*));

    for (uint32_t i = 0; i < mountArgs->Length(); i++) {
      Local<Value> arg = mountArgs->Get(i);

      if (!arg->IsUndefined() && arg->IsString()) {
        Local<String> stringArg = Local<String>::Cast(arg);
        String::AsciiValue av(stringArg);  
        llf4js.extraArgv[llf4js.extraArgc] = (char*)malloc(sizeof(av));
        memcpy(llf4js.extraArgv[llf4js.extraArgc], *av, sizeof(av));
        llf4js.extraArgc++;
      }
    }
  }
  
  llf4js.root = root;
  llf4js.handlers = Persistent<Object>::New(Local<Object>::Cast(args[1]));
  llf4js.psem = sem_open(llf4js_semaphore_name().c_str(), O_CREAT, S_IRUSR | S_IWUSR, 0);
  if (llf4js.psem == SEM_FAILED)
  {
     std::cerr << "Error: semaphore creation failed - " << strerror(errno) << "\n";
     exit(-1);
  }
 
  uv_async_init(uv_default_loop(), &llf4js.async, DispatchOp);

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_create(&llf4js.fuse_thread, &attr, llfuse_thread, NULL);
  return scope.Close(String::New("dummy"));
}
