/*
 * 
 * fuse4js.cc
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

using namespace v8;

// ---------------------------------------------------------------------------

static struct {
  bool enableFuseDebug;
  uv_async_t async;
  sem_t *psem;
  pthread_t fuse_thread;
  std::string root;
  Persistent<Object> handlers;
  Persistent<Object> nodeBuffer;  
} f4js;

enum fuseop_t {  
  OP_GETATTR = 0,
  OP_READDIR,
  OP_READLINK,
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
      char *dstBuf;
      size_t len;
    } readlink;
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

std::string f4js_semaphore_path()
{
   return std::string("/fuse4js") + f4js.root;
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
  return (void*)f4js_rpc(OP_INIT, "");
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
  if (fuse_main(4, argv, &ops, NULL)) {
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
  Local<Value> prop = stat->Get(String::NewSymbol(name.c_str()));
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

void ProcessReturnValue(const Arguments& args)
{
  if (args.Length() >= 1 && args[0]->IsNumber()) {
    Local<Number> retval = Local<Number>::Cast(args[0]);
    f4js_cmd.retval = (int)retval->Value();
  }  
}

// ---------------------------------------------------------------------------

Handle<Value> GetAttrCompletion(const Arguments& args)
{
  HandleScope scope;
  ProcessReturnValue(args);
  if (f4js_cmd.retval == 0 && args.Length() >= 2 && args[1]->IsObject()) {
    memset(f4js_cmd.u.getattr.stbuf, 0, sizeof(*f4js_cmd.u.getattr.stbuf));
    Handle<Object> stat = Handle<Object>::Cast(args[1]);
    
    Local<Value> prop = stat->Get(String::NewSymbol("size"));
    if (!prop->IsUndefined() && prop->IsNumber()) {
      Local<Number> num = Local<Number>::Cast(prop);
      f4js_cmd.u.getattr.stbuf->st_size = (off_t)num->Value();
    }
    
    prop = stat->Get(String::NewSymbol("mode"));
    if (!prop->IsUndefined() && prop->IsNumber()) {
      Local<Number> num = Local<Number>::Cast(prop);
      f4js_cmd.u.getattr.stbuf->st_mode = (mode_t)num->Value();
    }
    
    prop = stat->Get(String::NewSymbol("uid"));
    if (!prop->IsUndefined() && prop->IsNumber()) {
      Local<Number> num = Local<Number>::Cast(prop);
      f4js_cmd.u.getattr.stbuf->st_uid = (uid_t)num->Value();
    }

    prop = stat->Get(String::NewSymbol("gid"));
    if (!prop->IsUndefined() && prop->IsNumber()) {
      Local<Number> num = Local<Number>::Cast(prop);
      f4js_cmd.u.getattr.stbuf->st_gid = (gid_t)num->Value();
    }

    //ConvertDate(stat, "mtime", &f4js_cmd.u.getattr.stbuf->st_mtim);
    //ConvertDate(stat, "ctime", &f4js_cmd.u.getattr.stbuf->st_ctim);
    //ConvertDate(stat, "atime", &f4js_cmd.u.getattr.stbuf->st_atim);
  }
  sem_post(f4js.psem);  
  return scope.Close(Undefined());    
}

// ---------------------------------------------------------------------------

Handle<Value> ReadDirCompletion(const Arguments& args)
{
  HandleScope scope;
  ProcessReturnValue(args);
  if (f4js_cmd.retval == 0 && args.Length() >= 2 && args[1]->IsArray()) {
    Handle<Array> ar = Handle<Array>::Cast(args[1]);
    for (uint32_t i = 0; i < ar->Length(); i++) {
      Local<Value> el = ar->Get(i);
      if (!el->IsUndefined() && el->IsString()) {
        Local<String> name = Local<String>::Cast(el);
        String::AsciiValue av(name);  
        struct stat st;
        memset(&st, 0, sizeof(st)); // structure not used. Zero everything.
        if (f4js_cmd.u.readdir.filler(f4js_cmd.u.readdir.buf, *av, &st, 0))
          break;            
      }
    }
  }
  sem_post(f4js.psem);  
  return scope.Close(Undefined());    
}

// ---------------------------------------------------------------------------

Handle<Value> ReadLinkCompletion(const Arguments& args)
{
  HandleScope scope;
  ProcessReturnValue(args);
  if (f4js_cmd.retval == 0 && args.Length() >= 2 && args[1]->IsString()) {
    String::AsciiValue av(args[1]);
    size_t len = std::min((size_t)av.length() + 1, f4js_cmd.u.readlink.len);
    strncpy(f4js_cmd.u.readlink.dstBuf, *av, len);
  }
  sem_post(f4js.psem);  
  return scope.Close(Undefined());    
}

// ---------------------------------------------------------------------------

Handle<Value> GenericCompletion(const Arguments& args)
{
  HandleScope scope;
  bool exiting = (f4js_cmd.op == OP_DESTROY);
  
  ProcessReturnValue(args);
  sem_post(f4js.psem);  
  if (exiting) {
    pthread_join(f4js.fuse_thread, NULL);
    uv_unref((uv_handle_t*) &f4js.async);
    sem_close(f4js.psem);
    sem_unlink(f4js_semaphore_path().c_str());    
  }
  return scope.Close(Undefined());    
}

// ---------------------------------------------------------------------------

Handle<Value> OpenCreateCompletion(const Arguments& args)
{
  HandleScope scope;
  ProcessReturnValue(args);
  if (f4js_cmd.retval == 0 && args.Length() >= 2 && args[1]->IsNumber()) {
    Local<Number> fileHandle = Local<Number>::Cast(args[1]);
    f4js_cmd.info->fh = (uint64_t)fileHandle->Value(); // save the file handle
  } else {
    f4js_cmd.info->fh = 0;
  }
  sem_post(f4js.psem);  
  return scope.Close(Undefined());    
}

// ---------------------------------------------------------------------------

Handle<Value> ReadCompletion(const Arguments& args)
{
  HandleScope scope;
  ProcessReturnValue(args);    
  if (f4js_cmd.retval >= 0) {
    char *buffer_data = node::Buffer::Data(f4js.nodeBuffer);
    if ((size_t)f4js_cmd.retval > f4js_cmd.u.rw.len) {
      f4js_cmd.retval = f4js_cmd.u.rw.len;
    }
    memcpy(f4js_cmd.u.rw.dstBuf, buffer_data, f4js_cmd.retval);
  }
  f4js.nodeBuffer.Dispose();
  sem_post(f4js.psem);  
  return scope.Close(Undefined());    
}

// ---------------------------------------------------------------------------

Handle<Value> WriteCompletion(const Arguments& args)
{
  HandleScope scope;
  ProcessReturnValue(args);
  f4js.nodeBuffer.Dispose();
  sem_post(f4js.psem);  
  return scope.Close(Undefined());    
}

// ---------------------------------------------------------------------------

// Called from the main thread.
static void DispatchOp(uv_async_t* handle, int status)
{
  HandleScope scope;
  std::string symName(fuseop_names[f4js_cmd.op]);
  Local<FunctionTemplate> tpl = FunctionTemplate::New(GenericCompletion); // default
  f4js_cmd.retval = -EPERM;
  int argc = 0;
  Handle<Value> argv[6]; 
  Local<String> path = String::New(f4js_cmd.in_path); 
  argv[argc++] = path;
  node::Buffer* buffer = NULL; // used for read/write operations
  bool passInfo = false;
  
  switch (f4js_cmd.op) {
  
  case OP_INIT:
  case OP_DESTROY:
    f4js_cmd.retval = 0; // Will be used as the return value of OP_INIT.
    --argc;              // Ugly. Remove the first argument (path) because not needed.
    break;
    
  case OP_GETATTR:
    tpl = FunctionTemplate::New(GetAttrCompletion);
    break;
  
  case OP_READDIR:
    tpl = FunctionTemplate::New(ReadDirCompletion);
    break;
  
  case OP_READLINK:
    tpl = FunctionTemplate::New(ReadLinkCompletion);
    break;
  
  case OP_RENAME:
    argv[argc++] = String::New(f4js_cmd.u.rename.dst);
    break;

  case OP_OPEN:
    tpl = FunctionTemplate::New(OpenCreateCompletion);
    argv[argc++] = Number::New((double)f4js_cmd.info->flags);      
    break;
    
  case OP_CREATE:
    tpl = FunctionTemplate::New(OpenCreateCompletion);
    argv[argc++] = Number::New((double)f4js_cmd.u.create_mkdir.mode);      
    break;
  
  case OP_MKDIR:
    argv[argc++] = Number::New((double)f4js_cmd.u.create_mkdir.mode);      
    break;
    
  case OP_READ:
    tpl = FunctionTemplate::New(ReadCompletion);
    buffer = node::Buffer::New(f4js_cmd.u.rw.len);
    passInfo = true;
    break;
    
  case OP_WRITE:
    tpl = FunctionTemplate::New(WriteCompletion);   
    buffer = node::Buffer::New((char*)f4js_cmd.u.rw.srcBuf, f4js_cmd.u.rw.len);
    passInfo = true;
    break;
    
  case OP_RELEASE:
    passInfo = true;
    break;
    
  default:
    break;
  }
  
  // Additional args for read/write operations
  if (buffer) { 
    // FIXME: 64-bit off_t cannot always fit in a JS number 
    argv[argc++] = Number::New((double)f4js_cmd.u.rw.offset);  
    argv[argc++] = Number::New((double)f4js_cmd.u.rw.len);
    f4js.nodeBuffer = Persistent<Object>::New(buffer->handle_);   
    argv[argc++] = f4js.nodeBuffer;
  }
  if (passInfo) {
    argv[argc++] = Number::New((double)f4js_cmd.info->fh); // optional file handle returned by open()
  }
  Local<Function> handler = Local<Function>::Cast(f4js.handlers->Get(String::NewSymbol(symName.c_str())));
  if (handler->IsUndefined()) {
    sem_post(f4js.psem);
    return;
  }
  Local<Function> cb = tpl->GetFunction();
  std::string cbName = symName + "Completion";
  cb->SetName(String::NewSymbol(cbName.c_str()));
  argv[argc++] = cb;
  handler->Call(Context::GetCurrent()->Global(), argc, argv);  
}

// ---------------------------------------------------------------------------

Handle<Value> Start(const Arguments& args)
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

  String::AsciiValue av(args[0]);
  char *root = *av;
  if (root == NULL) {
    ThrowException(Exception::TypeError(String::New("Path is incorrect")));
    return scope.Close(Undefined());
  }
  
  f4js.enableFuseDebug = false;
  if (args.Length() >= 3) {
    Local <Boolean> debug = args[2]->ToBoolean();
    f4js.enableFuseDebug = debug->BooleanValue();
  }
  
  f4js.root = root;
  f4js.handlers = Persistent<Object>::New(Local<Object>::Cast(args[1]));
  f4js.psem = sem_open(f4js_semaphore_path().c_str(), O_CREAT, S_IRUSR | S_IWUSR, 0);
  if (f4js.psem == SEM_FAILED)
  {
     std::cerr << "Error: semaphore creation failed - " << strerror(errno) << "\n";
     exit(-1);
  }
 
  uv_async_init(uv_default_loop(), &f4js.async, DispatchOp);

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_create(&f4js.fuse_thread, &attr, fuse_thread, NULL);
  return scope.Close(String::New("dummy"));
}

// ---------------------------------------------------------------------------

void init(Handle<Object> target)
{
  target->Set(String::NewSymbol("start"), FunctionTemplate::New(Start)->GetFunction());
}

// ---------------------------------------------------------------------------

NODE_MODULE(fuse4js, init)
