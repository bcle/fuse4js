/*
 * 
 * fuse4js.cc
 * 
 * Copyright (c) 2012 - 2014 by VMware, Inc. All Rights Reserved.
 * http://www.vmware.com
 * Refer to LICENSE.txt for details of distribution and use.
 * 
 */

/*
* Include nan for version compatibility
*/

#include <node.h>
#include <node_buffer.h>
#include <nan.h>

using v8::FunctionTemplate;
using v8::Handle;
using v8::Object;
using v8::String;

#define FUSE_USE_VERSION 26

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <fuse.h>
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
  Persistent<Function> GetAttrFunc;
  Persistent<Function> ReadDirFunc;
  Persistent<Function> ReadLinkFunc;
  Persistent<Function> StatfsFunc;
  Persistent<Function> OpenCreateFunc;
  Persistent<Function> ReadFunc;
  Persistent<Function> WriteFunc;
  Persistent<Function> GenericFunc;
} f4js;

enum fuseop_t {  
  OP_GETATTR = 0,
  OP_READDIR,
  OP_READLINK,
  OP_CHMOD,
  OP_SETXATTR,
  OP_STATFS,
  OP_OPEN,
  OP_READ,
  OP_WRITE,
  OP_RELEASE,
  OP_CREATE,
  OP_UNLINK,
  OP_RENAME,
  OP_MKDIR,
  OP_RMDIR,
  OP_INIT,
  OP_DESTROY
};

const char* fuseop_names[] = {
    "getattr",
    "readdir",
    "readlink",
    "chmod",
    "setxattr",
    "statfs",
    "open",
    "read",
    "write",
    "release",
    "create",
    "unlink",
    "rename",
    "mkdir",
    "rmdir",
    "init",
    "destroy"
};

static struct {
  enum fuseop_t op;
  const char *in_path;
  struct fuse_file_info *info;
  union {
    struct {
      struct stat *stbuf;
    } getattr;
    struct {
      void *buf;
      fuse_fill_dir_t filler;
    } readdir;
    struct {
      struct statvfs *buf;
    } statfs;
    struct {
      char *dstBuf;
      size_t len;
    } readlink;
    struct {
      mode_t mode;
    } chmod;
#ifdef __APPLE__
    struct {
      const char *name;
      const char *value;
      size_t size;
      int position;
      uint32_t options;
    } setxattr;
#else
    struct {
      const char *name;
      const char *value;
      size_t size;
      int flags;
    } setxattr;
#endif
   struct {
      off_t offset;
      size_t len;
      char *dstBuf;
      const char *srcBuf; 
    } rw;
    struct {
      const char *dst;
    } rename;
    struct {
      mode_t mode;
    } create_mkdir;
  } u;
  int retval;
} f4js_cmd;

// ---------------------------------------------------------------------------

std::string f4js_semaphore_name()
{
   std::ostringstream o;
   o << "fuse4js" << getpid();
   return o.str();
}

// ---------------------------------------------------------------------------

static int f4js_rpc(enum fuseop_t op, const char *path)
{
  f4js_cmd.op = op;
  f4js_cmd.in_path = path;
  uv_async_send(&f4js.async);
  sem_wait(f4js.psem);
  return f4js_cmd.retval;  
}

// ---------------------------------------------------------------------------

static int f4js_getattr(const char *path, struct stat *stbuf)
{
  f4js_cmd.u.getattr.stbuf = stbuf;
  return f4js_rpc(OP_GETATTR, path);
}

// ---------------------------------------------------------------------------

static int f4js_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		         off_t offset, struct fuse_file_info *fi)
{
  f4js_cmd.u.readdir.buf = buf;
  f4js_cmd.u.readdir.filler = filler;
  return f4js_rpc(OP_READDIR, path);
}

// ---------------------------------------------------------------------------

static int f4js_readlink(const char *path, char *buf, size_t len)
{

  f4js_cmd.u.readlink.dstBuf = buf;
  f4js_cmd.u.readlink.len = len;
  return f4js_rpc(OP_READLINK, path);
}

// ---------------------------------------------------------------------------

static int f4js_chmod(const char *path, mode_t mode)
{
  f4js_cmd.u.chmod.mode = mode;
  return f4js_rpc(OP_CHMOD, path);
}

// ---------------------------------------------------------------------------


#ifdef __APPLE__
static int f4js_setxattr(const char *path, const char* name, const char* value, size_t size, int position, uint32_t options)
{
  f4js_cmd.u.setxattr.name = name;
  f4js_cmd.u.setxattr.value = value;
  f4js_cmd.u.setxattr.size = size;
  f4js_cmd.u.setxattr.position = position;
  f4js_cmd.u.setxattr.options = options;
  return f4js_rpc(OP_SETXATTR, path);
}
#else
static int f4js_setxattr(const char *path, const char* name, const char* value, size_t size, int flags)
{
  f4js_cmd.u.setxattr.name = name;
  f4js_cmd.u.setxattr.value = value;
  f4js_cmd.u.setxattr.size = size;
  f4js_cmd.u.setxattr.flags = flags;
  return f4js_rpc(OP_SETXATTR, path);
}
#endif


// ---------------------------------------------------------------------------

static int f4js_statfs(const char *path, struct statvfs *buf)
{
  f4js_cmd.u.statfs.buf = buf;
  return f4js_rpc(OP_STATFS, path);
}

// ---------------------------------------------------------------------------

int f4js_open(const char *path, struct fuse_file_info *info)
{
  f4js_cmd.info = info;
  return f4js_rpc(OP_OPEN, path);
}

// ---------------------------------------------------------------------------

int f4js_read (const char *path,
               char *buf,
               size_t len,
               off_t offset,
               struct fuse_file_info *info)
{
  f4js_cmd.info = info;
  f4js_cmd.u.rw.offset = offset;
  f4js_cmd.u.rw.len = len;
  f4js_cmd.u.rw.dstBuf = buf;
  return f4js_rpc(OP_READ, path);
}

// ---------------------------------------------------------------------------

int f4js_write (const char *path,
                const char *buf,
                size_t len,
                off_t offset,
                struct fuse_file_info * info)
{
  f4js_cmd.info = info;
  f4js_cmd.u.rw.offset = offset;
  f4js_cmd.u.rw.len = len;
  f4js_cmd.u.rw.srcBuf = buf;
  return f4js_rpc(OP_WRITE, path);
}

// ---------------------------------------------------------------------------

int f4js_release (const char *path, struct fuse_file_info *info)
{
  f4js_cmd.info = info;
  return f4js_rpc(OP_RELEASE, path);
}

// ---------------------------------------------------------------------------

int f4js_create (const char *path,
                 mode_t mode,
                 struct fuse_file_info *info)
{
  f4js_cmd.info = info;
  f4js_cmd.u.create_mkdir.mode = mode;
  return f4js_rpc(OP_CREATE, path);
}

// ---------------------------------------------------------------------------

int f4js_utimens (const char *,
                  const struct timespec tv[2])
{
  return 0; // stub out for now to make "touch" command succeed
}

// ---------------------------------------------------------------------------

int f4js_unlink (const char *path)
{
  return f4js_rpc(OP_UNLINK, path);
}

// ---------------------------------------------------------------------------

int f4js_rename (const char *src, const char *dst)
{
  f4js_cmd.u.rename.dst = dst;
  return f4js_rpc(OP_RENAME, src);
}

// ---------------------------------------------------------------------------

int f4js_mkdir (const char *path, mode_t mode)
{
  f4js_cmd.u.create_mkdir.mode = mode;
  return f4js_rpc(OP_MKDIR, path);
}

// ---------------------------------------------------------------------------

int f4js_rmdir (const char *path)
{
  return f4js_rpc(OP_RMDIR, path);
}

// ---------------------------------------------------------------------------


void* f4js_init(struct fuse_conn_info *conn)
{
  // We currently always return NULL
  f4js_rpc(OP_INIT, "");
  return NULL;
}

// ---------------------------------------------------------------------------

void f4js_destroy (void *data)
{
  // We currently ignore the data pointer, which init() always sets to NULL
  f4js_rpc(OP_DESTROY, "");
}

// ---------------------------------------------------------------------------

void *fuse_thread(void *)
{
  struct fuse_operations ops = { 0 };
  ops.getattr = f4js_getattr;
  ops.readdir = f4js_readdir;
  ops.readlink = f4js_readlink;
  ops.chmod = f4js_chmod;
  ops.setxattr = f4js_setxattr;
  ops.statfs = f4js_statfs;
  ops.open = f4js_open;
  ops.read = f4js_read;
  ops.write = f4js_write;
  ops.release = f4js_release;
  ops.create = f4js_create;
  ops.utimens = f4js_utimens;
  ops.unlink = f4js_unlink;
  ops.rename = f4js_rename;
  ops.mkdir = f4js_mkdir;
  ops.rmdir = f4js_rmdir;
  ops.init = f4js_init;
  ops.destroy = f4js_destroy;
  const char* debugOption = f4js.enableFuseDebug? "-d":"-f";
  char *argv[] = { (char*)"dummy", (char*)"-s", (char*)debugOption, (char*)f4js.root.c_str() };

  int initialArgc = sizeof(argv) / sizeof(char*);
  char **argvIncludingExtraArgs = (char**)malloc(sizeof(char*) * (initialArgc + f4js.extraArgc));
  memcpy(argvIncludingExtraArgs, argv, sizeof(argv));
  memcpy(argvIncludingExtraArgs + initialArgc, f4js.extraArgv, sizeof(char*) * f4js.extraArgc);

  if (fuse_main((initialArgc + f4js.extraArgc), argvIncludingExtraArgs, &ops, NULL)) {
    // Error occured
    f4js_destroy(NULL);
  }
  return NULL;
}

// ---------------------------------------------------------------------------

void ConvertDate(Handle<Object> &stat,
                 std::string name,
                 struct timespec *out)
{
  Local<Value> prop = stat->Get(NanNew<String>(name.c_str()));
  if (!prop->IsUndefined() && prop->IsDate()) {
    Local<Date> date = Local<Date>::Cast(prop);
    double dateVal = date->NumberValue();              // total milliseconds
    time_t seconds = (time_t)(dateVal / 1000.0);
    time_t milliseconds = dateVal - (1000.0 * seconds); // remainder
    time_t nanoseconds = milliseconds * 1000000.0;
    out->tv_sec = seconds;
    out->tv_nsec = nanoseconds;
  }  
}


// ---------------------------------------------------------------------------

NAN_METHOD(ProcessReturnValue)
{
  if (args.Length() >= 1 && args[0]->IsNumber()) {
    Local<Number> retval = Local<Number>::Cast(args[0]);
    f4js_cmd.retval = (int)retval->Value();
  }  
}

// ---------------------------------------------------------------------------

NAN_METHOD(GetAttrCompletion)
{
  NanEscapableScope();
  ProcessReturnValue(args);
  if (f4js_cmd.retval == 0 && args.Length() >= 2 && args[1]->IsObject()) {
    memset(f4js_cmd.u.getattr.stbuf, 0, sizeof(*f4js_cmd.u.getattr.stbuf));
    Handle<Object> stat = Handle<Object>::Cast(args[1]);
    
    Local<Value> prop = stat->Get(NanNew<String>("size"));
    if (!prop->IsUndefined() && prop->IsNumber()) {
      Local<Number> num = Local<Number>::Cast(prop);
      f4js_cmd.u.getattr.stbuf->st_size = (off_t)num->Value();
    }
    
    prop = stat->Get(NanNew<String>("mode"));
    if (!prop->IsUndefined() && prop->IsNumber()) {
      Local<Number> num = Local<Number>::Cast(prop);
      f4js_cmd.u.getattr.stbuf->st_mode = (mode_t)num->Value();
    }

    prop = stat->Get(NanNew<String>("nlink"));
    if (!prop->IsUndefined() && prop->IsNumber()) {
      Local<Number> num = Local<Number>::Cast(prop);
      f4js_cmd.u.getattr.stbuf->st_nlink = (mode_t)num->Value();
    }
    
    prop = stat->Get(NanNew<String>("uid"));
    if (!prop->IsUndefined() && prop->IsNumber()) {
      Local<Number> num = Local<Number>::Cast(prop);
      f4js_cmd.u.getattr.stbuf->st_uid = (uid_t)num->Value();
    }

    prop = stat->Get(NanNew<String>("gid"));
    if (!prop->IsUndefined() && prop->IsNumber()) {
      Local<Number> num = Local<Number>::Cast(prop);
      f4js_cmd.u.getattr.stbuf->st_gid = (gid_t)num->Value();
    }

    struct stat *stbuf = f4js_cmd.u.getattr.stbuf;
#ifdef __APPLE__
    ConvertDate(stat, "mtime", &stbuf->st_mtimespec);
    ConvertDate(stat, "ctime", &stbuf->st_ctimespec);
    ConvertDate(stat, "atime", &stbuf->st_atimespec);
#else
    ConvertDate(stat, "mtime", &stbuf->st_mtim);
    ConvertDate(stat, "ctime", &stbuf->st_ctim);
    ConvertDate(stat, "atime", &stbuf->st_atim);
#endif

  }
  sem_post(f4js.psem);  
  NanEscapeScope(NanUndefined());    
}

// ---------------------------------------------------------------------------

NAN_METHOD(ReadDirCompletion)
{
  NanEscapableScope();
  ProcessReturnValue(args);
  if (f4js_cmd.retval == 0 && args.Length() >= 2 && args[1]->IsArray()) {
    Handle<Array> ar = Handle<Array>::Cast(args[1]);
    for (uint32_t i = 0; i < ar->Length(); i++) {
      Local<Value> el = ar->Get(i);
      if (!el->IsUndefined() && el->IsString()) {
        Local<String> name = Local<String>::Cast(el);
        String::Utf8Value av(name);  
        struct stat st;
        memset(&st, 0, sizeof(st)); // structure not used. Zero everything.
        if (f4js_cmd.u.readdir.filler(f4js_cmd.u.readdir.buf, *av, &st, 0))
          break;            
      }
    }
  }
  sem_post(f4js.psem);  
  NanEscapeScope(NanUndefined());    
}

// ---------------------------------------------------------------------------

NAN_METHOD( StatfsCompletion )
{
  NanEscapableScope();
  ProcessReturnValue(args);
  if (f4js_cmd.retval == 0 && args.Length() >= 2 && args[1]->IsObject()) {
    memset(f4js_cmd.u.statfs.buf, 0, sizeof(*f4js_cmd.u.statfs.buf));
    Handle<Object> stat = Handle<Object>::Cast(args[1]);

    Local<Value> prop = stat->Get(NanNew<String>("bsize"));
    if (!prop->IsUndefined() && prop->IsNumber()) {
      Local<Number> num = Local<Number>::Cast(prop);
      f4js_cmd.u.statfs.buf->f_bsize = (off_t)num->Value();
    }

    prop = stat->Get(NanNew<String>("frsize"));
    if (!prop->IsUndefined() && prop->IsNumber()) {
      Local<Number> num = Local<Number>::Cast(prop);
      f4js_cmd.u.statfs.buf->f_frsize = (off_t)num->Value();
    }

    prop = stat->Get(NanNew<String>("blocks"));
    if (!prop->IsUndefined() && prop->IsNumber()) {
      Local<Number> num = Local<Number>::Cast(prop);
      f4js_cmd.u.statfs.buf->f_blocks = (off_t)num->Value();
    }

    prop = stat->Get(NanNew<String>("bfree"));
    if (!prop->IsUndefined() && prop->IsNumber()) {
      Local<Number> num = Local<Number>::Cast(prop);
      f4js_cmd.u.statfs.buf->f_bfree = (off_t)num->Value();
    }

    prop = stat->Get(NanNew<String>("bavail"));
    if (!prop->IsUndefined() && prop->IsNumber()) {
      Local<Number> num = Local<Number>::Cast(prop);
      f4js_cmd.u.statfs.buf->f_bavail = (off_t)num->Value();
    }

    prop = stat->Get(NanNew<String>("files"));
    if (!prop->IsUndefined() && prop->IsNumber()) {
      Local<Number> num = Local<Number>::Cast(prop);
      f4js_cmd.u.statfs.buf->f_files = (off_t)num->Value();
    }

    prop = stat->Get(NanNew<String>("ffree"));
    if (!prop->IsUndefined() && prop->IsNumber()) {
      Local<Number> num = Local<Number>::Cast(prop);
      f4js_cmd.u.statfs.buf->f_ffree = (off_t)num->Value();
    }

    prop = stat->Get(NanNew<String>("favail"));
    if (!prop->IsUndefined() && prop->IsNumber()) {
      Local<Number> num = Local<Number>::Cast(prop);
      f4js_cmd.u.statfs.buf->f_favail = (off_t)num->Value();
    }

    prop = stat->Get(NanNew<String>("fsid"));
    if (!prop->IsUndefined() && prop->IsNumber()) {
      Local<Number> num = Local<Number>::Cast(prop);
      f4js_cmd.u.statfs.buf->f_fsid = (off_t)num->Value();
    }

    prop = stat->Get(NanNew<String>("flag"));
    if (!prop->IsUndefined() && prop->IsNumber()) {
      Local<Number> num = Local<Number>::Cast(prop);
      f4js_cmd.u.statfs.buf->f_flag = (off_t)num->Value();
    }

    prop = stat->Get(NanNew<String>("namemax"));
    if (!prop->IsUndefined() && prop->IsNumber()) {
      Local<Number> num = Local<Number>::Cast(prop);
      f4js_cmd.u.statfs.buf->f_namemax = (off_t)num->Value();
    }
  }
  sem_post(f4js.psem);  
  NanEscapeScope(NanUndefined()); 
}

// ---------------------------------------------------------------------------

NAN_METHOD( ReadLinkCompletion)
{
  NanEscapableScope();
  ProcessReturnValue(args);
  if (f4js_cmd.retval == 0 && args.Length() >= 2 && args[1]->IsString()) {
    String::Utf8Value av(args[1]);
    size_t len = std::min((size_t)av.length() + 1, f4js_cmd.u.readlink.len);
    strncpy(f4js_cmd.u.readlink.dstBuf, *av, len);
    // terminate string even when it is truncated
    f4js_cmd.u.readlink.dstBuf[f4js_cmd.u.readlink.len - 1] = '\0';
  }
  sem_post(f4js.psem);  
  NanEscapeScope(NanUndefined());    
}

// ---------------------------------------------------------------------------

NAN_METHOD(GenericCompletion)
{
  NanEscapableScope();
  bool exiting = (f4js_cmd.op == OP_DESTROY);
  
  ProcessReturnValue(args);
  sem_post(f4js.psem);  
  if (exiting) {
    pthread_join(f4js.fuse_thread, NULL);
    uv_unref((uv_handle_t*) &f4js.async);
    sem_close(f4js.psem);
    sem_unlink(f4js_semaphore_name().c_str());    
  }
  NanEscapeScope(NanUndefined());    
}

// ---------------------------------------------------------------------------

NAN_METHOD(OpenCreateCompletion)
{
  NanEscapableScope();
  ProcessReturnValue(args);
  if (f4js_cmd.retval == 0 && args.Length() >= 2 && args[1]->IsNumber()) {
    Local<Number> fileHandle = Local<Number>::Cast(args[1]);
    f4js_cmd.info->fh = (uint64_t)fileHandle->Value(); // save the file handle
  } else {
    f4js_cmd.info->fh = 0;
  }
  sem_post(f4js.psem);  
  NanEscapeScope(NanUndefined());    
}

// ---------------------------------------------------------------------------

NAN_METHOD(ReadCompletion)
{
  NanEscapableScope();
  ProcessReturnValue(args);    
  if (f4js_cmd.retval >= 0) {
    char *buffer_data = node::Buffer::Data(NanNew(f4js.nodeBuffer));
    if ((size_t)f4js_cmd.retval > f4js_cmd.u.rw.len) {
      f4js_cmd.retval = f4js_cmd.u.rw.len;
    }
    memcpy(f4js_cmd.u.rw.dstBuf, buffer_data, f4js_cmd.retval);
  }
  NanDisposePersistent(f4js.nodeBuffer);
  sem_post(f4js.psem);  
  NanEscapeScope(NanUndefined());    
}

// ---------------------------------------------------------------------------

NAN_METHOD(WriteCompletion)
{
  NanEscapableScope();
  ProcessReturnValue(args);
  NanDisposePersistent(f4js.nodeBuffer);
  sem_post(f4js.psem);  
  NanEscapeScope(NanUndefined());    
}

// ---------------------------------------------------------------------------

// Called from the main thread.
static void DispatchOp(uv_async_t* handle, int status)
{
  NanEscapableScope();
  std::string symName(fuseop_names[f4js_cmd.op]);
  f4js_cmd.retval = -EPERM;
  int argc = 0;
  Handle<Value> argv[6]; 
  Local<String> path = NanNew<String>(f4js_cmd.in_path); 
  argv[argc++] = path;
  switch (f4js_cmd.op) {
  
  case OP_INIT:
  case OP_DESTROY:
    f4js_cmd.retval = 0; // Will be used as the return value of OP_INIT.
    --argc;              // Ugly. Remove the first argument (path) because not needed.
    argv[argc++] = NanNew(f4js.GenericFunc);
    break;
    
  case OP_GETATTR:
    argv[argc++] = NanNew(f4js.GetAttrFunc);
    break;
  
  case OP_READDIR:
    argv[argc++] = NanNew(f4js.ReadDirFunc);
    break;
  
  case OP_READLINK:
    argv[argc++] = NanNew(f4js.ReadLinkFunc);
    break;

  case OP_CHMOD:
    argv[argc++] = NanNew<Number>((double)f4js_cmd.u.chmod.mode);
    break;

  case OP_SETXATTR:
    argv[argc++] = NanNew<String>(f4js_cmd.u.setxattr.name);
    argv[argc++] = NanNew<String>(f4js_cmd.u.setxattr.value);
    argv[argc++] = NanNew<Number>((double)f4js_cmd.u.setxattr.size);
#ifdef __APPLE__
    argv[argc++] = NanNew<Number>((double)f4js_cmd.u.setxattr.position);
    argv[argc++] = NanNew<Number>((double)f4js_cmd.u.setxattr.options);
#else
    argv[argc++] = NanNew<Number>((double)f4js_cmd.u.setxattr.flags);
#endif
    break;

  case OP_STATFS:
    --argc; // Ugly. Remove the first argument (path) because not needed.
    argv[argc++] = NanNew(f4js.StatfsFunc);
    break;
  
  case OP_RENAME:
    argv[argc++] = NanNew<String>(f4js_cmd.u.rename.dst);
    argv[argc++] = NanNew(f4js.GenericFunc);
    break;

  case OP_OPEN:
    argv[argc++] = NanNew<Number>((double)f4js_cmd.info->flags);
    argv[argc++] = NanNew(f4js.OpenCreateFunc);
    break;
    
  case OP_CREATE:
    argv[argc++] = NanNew<Number>((double)f4js_cmd.u.create_mkdir.mode);
    argv[argc++] = NanNew(f4js.OpenCreateFunc);
    break;
  
  case OP_MKDIR:
    argv[argc++] = NanNew<Number>((double)f4js_cmd.u.create_mkdir.mode);  
    argv[argc++] = NanNew(f4js.GenericFunc);
    
    break;
    
  case OP_READ:
    argv[argc++] = NanNew<Number>((double)f4js_cmd.u.rw.offset);  
    argv[argc++] = NanNew<Number>((double)f4js_cmd.u.rw.len);
    NanAssignPersistent(f4js.nodeBuffer, NanNewBufferHandle(f4js_cmd.u.rw.len));
    argv[argc++] = NanNew(f4js.nodeBuffer);
    argv[argc++] = NanNew<Number>((double)f4js_cmd.info->fh); // optional file handle returned by open()
    argv[argc++] = NanNew(f4js.ReadFunc);

    break;
    
  case OP_WRITE:
    argv[argc++] = NanNew<Number>((double)f4js_cmd.u.rw.offset);  
    argv[argc++] = NanNew<Number>((double)f4js_cmd.u.rw.len);
    NanAssignPersistent(f4js.nodeBuffer, NanNewBufferHandle((char*)f4js_cmd.u.rw.srcBuf, f4js_cmd.u.rw.len));
    argv[argc++] = NanNew(f4js.nodeBuffer);
    argv[argc++] = NanNew<Number>((double)f4js_cmd.info->fh); // optional file handle returned by open()
    argv[argc++] = NanNew(f4js.WriteFunc);
    break;
    
  case OP_RELEASE:
    argv[argc++] = NanNew<Number>((double)f4js_cmd.info->fh); // optional file handle returned by open()
    argv[argc++] = NanNew(f4js.GenericFunc);

    break;
    
  default:
    break;
  }
  
  Local<Function> handler = Local<Function>::Cast(
    NanNew(f4js.handlers)->Get(NanNew<String>(symName.c_str()))
  );
  if (handler->IsUndefined()) {
    sem_post(f4js.psem);
    return;
  }
  handler->Call(NanGetCurrentContext()->Global(), argc, argv);
}

// ---------------------------------------------------------------------------

NAN_METHOD(Start)
{
  NanEscapableScope();
  if (args.Length() < 2) {
    NanThrowTypeError("Wrong number of arguments");
    NanEscapeScope(NanUndefined());
  }

  if (!args[0]->IsString() || !args[1]->IsObject()) {
    NanThrowTypeError("Wrong argument types");
    NanEscapeScope(NanUndefined());
  }

  String::Utf8Value av(args[0]);
  char *root = *av;
  if (root == NULL) {
    NanThrowTypeError("Path is incorrect");
    NanEscapeScope(NanUndefined());
  }
  
  f4js.enableFuseDebug = false;
  if (args.Length() >= 3) {
    Local <Boolean> debug = args[2]->ToBoolean();
    f4js.enableFuseDebug = debug->BooleanValue();
  }

  f4js.extraArgc = 0;
  if (args.Length() >= 4) {
    if (!args[3]->IsArray()) {
        NanThrowTypeError("Wrong argument types");
        NanEscapeScope(NanUndefined());
    }

    Handle<Array> mountArgs = Handle<Array>::Cast(args[3]);
    f4js.extraArgv = (char**)malloc(mountArgs->Length() * sizeof(char*));
    int argLen = 0;
    int argSize = 0;
    for (uint32_t i = 0; i < mountArgs->Length(); i++) {
      Local<Value> arg = mountArgs->Get(i);

      if (!arg->IsUndefined() && arg->IsString()) {
        Local<String> stringArg = Local<String>::Cast(arg);

        char *handle = *NanAsciiString(stringArg);  
        argLen = std::strlen(handle)+1;
        argSize = argLen * sizeof(char);
        f4js.extraArgv[f4js.extraArgc] = (char*)malloc(argSize);
        memcpy(f4js.extraArgv[f4js.extraArgc], (void *) handle, argSize);
        f4js.extraArgc++;
      }
    }
  }
  
  f4js.root = root;
  NanAssignPersistent( f4js.handlers, Local<Object>::Cast(args[1]) );

  f4js.psem = sem_open(f4js_semaphore_name().c_str(), O_CREAT, S_IRUSR | S_IWUSR, 0);
  if (f4js.psem == SEM_FAILED)
  {
     std::cerr << "Error: semaphore creation failed - " << strerror(errno) << "\n";
     exit(-1);
  }

  // NanSetPrototypeTemplate()
  // f4js.GetAttrFunc = Persistent<Function>::New(FunctionTemplate::New(GetAttrCompletion)->GetFunction());
  NanAssignPersistent(f4js.GetAttrFunc, NanNew<FunctionTemplate>(GetAttrCompletion)->GetFunction() );
  NanAssignPersistent(f4js.ReadDirFunc, NanNew<FunctionTemplate>(ReadDirCompletion)->GetFunction());
  NanAssignPersistent(f4js.ReadLinkFunc, NanNew<FunctionTemplate>(ReadLinkCompletion)->GetFunction());
  NanAssignPersistent(f4js.StatfsFunc, NanNew<FunctionTemplate>(StatfsCompletion)->GetFunction());
  NanAssignPersistent(f4js.OpenCreateFunc, NanNew<FunctionTemplate>(OpenCreateCompletion)->GetFunction());
  NanAssignPersistent(f4js.ReadFunc, NanNew<FunctionTemplate>(ReadCompletion)->GetFunction());
  NanAssignPersistent(f4js.WriteFunc, NanNew<FunctionTemplate>(WriteCompletion)->GetFunction());
  NanAssignPersistent(f4js.GenericFunc, NanNew<FunctionTemplate>(GenericCompletion)->GetFunction());

  uv_async_init(uv_default_loop(), &f4js.async, (uv_async_cb) DispatchOp);

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_create(&f4js.fuse_thread, &attr, fuse_thread, NULL);
  NanEscapeScope(NanNew<String>("dummy"));
}

// ---------------------------------------------------------------------------

void init(Handle<Object> target)
{
  target->Set(NanNew<String>("start"), NanNew<FunctionTemplate>(Start)->GetFunction());
}

// ---------------------------------------------------------------------------

NODE_MODULE(fuse4js, init)
