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

using namespace v8;

// ---------------------------------------------------------------------------

static struct {
  uv_async_t async;
  sem_t sem;
  pthread_t fuse_thread;
  char root[256];
  Persistent<Object> handlers;
  Persistent<Object> nodeBuffer;  
} f4js;

enum fuseop_t {  
  OP_EXIT = 0,
  OP_GETATTR = 1,
  OP_READDIR = 2,
  OP_OPEN = 3,
  OP_READ = 4,
  OP_WRITE = 5
};

static struct {
  enum fuseop_t op;
  const char *in_path;
  union {
    struct {
      struct stat *stbuf;
    } getattr;
    struct {
      void *buf;
      fuse_fill_dir_t filler;
    } readdir;
    struct {
      off_t offset;
      size_t len;
      char *buf;
    } rw;
  } u;
  int retval;
} f4js_cmd;

// ---------------------------------------------------------------------------

static int getattr(const char *path, struct stat *stbuf)
{
  f4js_cmd.op = OP_GETATTR;
  f4js_cmd.in_path = path;
  f4js_cmd.u.getattr.stbuf = stbuf;
  uv_async_send(&f4js.async);
  sem_wait(&f4js.sem);
  return f4js_cmd.retval;
}

// ---------------------------------------------------------------------------

static int readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
  f4js_cmd.op = OP_READDIR;
  f4js_cmd.in_path = path;
  f4js_cmd.u.readdir.buf = buf;
  f4js_cmd.u.readdir.filler = filler;
  uv_async_send(&f4js.async);
  sem_wait(&f4js.sem);  
  return f4js_cmd.retval;  
}

// ---------------------------------------------------------------------------

int open(const char *path, struct fuse_file_info *)
{
  f4js_cmd.op = OP_OPEN;
  f4js_cmd.in_path = path;
  uv_async_send(&f4js.async);
  sem_wait(&f4js.sem);  
  return f4js_cmd.retval;   
}

// ---------------------------------------------------------------------------

int read (const char *path,
          char *buf,
          size_t len,
          off_t offset,
          struct fuse_file_info *)
{
  f4js_cmd.op = OP_READ;
  f4js_cmd.in_path = path;
  f4js_cmd.u.rw.offset = offset;
  f4js_cmd.u.rw.len = len;
  f4js_cmd.u.rw.buf = buf;
  uv_async_send(&f4js.async);
  sem_wait(&f4js.sem); 
  return f4js_cmd.retval;   
}

// ---------------------------------------------------------------------------

void *fuse_thread(void *)
{
  struct fuse_operations oper = { 0 };
  oper.getattr = getattr;
  oper.readdir = readdir;
  oper.open = open;
  oper.read = read;
  char *argv[] = { "dummy", "-s", "-d", f4js.root };
  fuse_main(4, argv, &oper, NULL);
  f4js_cmd.op = OP_EXIT;
  uv_async_send(&f4js.async);
  return NULL;
}

// ---------------------------------------------------------------------------

Handle<Value> GetAttrCompletion(const Arguments& args)
{
  HandleScope scope;
  if (args.Length() >= 1 && args[0]->IsNumber()) {
    Local<Number> retval = Local<Number>::Cast(args[0]);
    f4js_cmd.retval = (int)retval->Value();
    if (f4js_cmd.retval == 0 && args.Length() >= 2 && args[1]->IsObject()) {
      memset(f4js_cmd.u.getattr.stbuf, 0, sizeof(*f4js_cmd.u.getattr.stbuf));
      Handle<Object> stat = Handle<Object>::Cast(args[1]);
      
      Local<Value> prop = stat->Get(String::NewSymbol("st_size"));
      if (!prop->IsUndefined() && prop->IsNumber()) {
        Local<Number> num = Local<Number>::Cast(prop);
        f4js_cmd.u.getattr.stbuf->st_size = (off_t)num->Value();
      }
      
      prop = stat->Get(String::NewSymbol("st_mode"));
      if (!prop->IsUndefined() && prop->IsNumber()) {
        Local<Number> num = Local<Number>::Cast(prop);
        f4js_cmd.u.getattr.stbuf->st_mode = (mode_t)num->Value();
      }
      
    }
  }
  sem_post(&f4js.sem);  
  return scope.Close(Undefined());    
}

// ---------------------------------------------------------------------------

Handle<Value> ReadDirCompletion(const Arguments& args)
{
  HandleScope scope;
  if (args.Length() >= 1 && args[0]->IsNumber()) {
    Local<Number> retval = Local<Number>::Cast(args[0]);
    f4js_cmd.retval = (int)retval->Value();
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
  }
  sem_post(&f4js.sem);  
  return scope.Close(Undefined());    
}

// ---------------------------------------------------------------------------

Handle<Value> OpenCompletion(const Arguments& args)
{
  HandleScope scope;
  if (args.Length() >= 1 && args[0]->IsNumber()) {
    Local<Number> retval = Local<Number>::Cast(args[0]);
    f4js_cmd.retval = (int)retval->Value();    
  }
  sem_post(&f4js.sem);  
  return scope.Close(Undefined());    
}

// ---------------------------------------------------------------------------

Handle<Value> ReadCompletion(const Arguments& args)
{
  HandleScope scope;
  if (args.Length() >= 1 && args[0]->IsNumber()) {
    Local<Number> retval = Local<Number>::Cast(args[0]);
    f4js_cmd.retval = (int)retval->Value();
    if (f4js_cmd.retval >= 0) {
      char *buffer_data = node::Buffer::Data(f4js.nodeBuffer);
      if ((size_t)f4js_cmd.retval > f4js_cmd.u.rw.len) {
        f4js_cmd.retval = f4js_cmd.u.rw.len;
      }
      memcpy(f4js_cmd.u.rw.buf, buffer_data, f4js_cmd.retval);
    }
  }
  f4js.nodeBuffer.Dispose();
  sem_post(&f4js.sem);  
  return scope.Close(Undefined());    
}

// ---------------------------------------------------------------------------

static void DispatchRead()
{
  Local<FunctionTemplate> tpl = FunctionTemplate::New(ReadCompletion);
  Local<Function> handler = Local<Function>::Cast(f4js.handlers->Get(String::NewSymbol("read")));
  if (handler->IsUndefined()) {
    sem_post(&f4js.sem);
    return;
  }
  Local<Function> cb = tpl->GetFunction();
  cb->SetName(String::NewSymbol("readCompletion"));
  Local<String> path = String::New(f4js_cmd.in_path);
  
  node::Buffer* buffer = node::Buffer::New(f4js_cmd.u.rw.len);
  f4js.nodeBuffer = Persistent<Object>::New(buffer->handle_); 
  
  // FIXME: large 64-bit file offsets cannot be precisely stored in a JS number 
  Local<Number> offset = Number::New((double)f4js_cmd.u.rw.offset);
  
  Local<Number> len = Number::New((double)f4js_cmd.u.rw.len);
  Handle<Value> argv[] = { path, offset, len, f4js.nodeBuffer, cb };
  handler->Call(Context::GetCurrent()->Global(), 5, argv);  
}

// ---------------------------------------------------------------------------

// Called from the main thread.
static void DispatchOp(uv_async_t* handle, int status) {
  HandleScope scope;
  std::string symName;
  Local<FunctionTemplate> tpl;
  f4js_cmd.retval = -EPERM;
  
  switch (f4js_cmd.op) {
  case OP_EXIT:
    pthread_join(f4js.fuse_thread, NULL);
    uv_unref((uv_handle_t*) &f4js.async);
    return;
    
  case OP_GETATTR:
    symName = "getattr";
    tpl = FunctionTemplate::New(GetAttrCompletion);
    break;
  
  case OP_READDIR:
    symName = "readdir";
    tpl = FunctionTemplate::New(ReadDirCompletion);
    break;
  
  case OP_OPEN:
    symName = "open";
    tpl = FunctionTemplate::New(OpenCompletion);
    break;

  case OP_READ:
    DispatchRead();
    return;
    
  default:
    sem_post(&f4js.sem);
    return;
  }
  
  Local<Function> handler = Local<Function>::Cast(f4js.handlers->Get(String::NewSymbol(symName.c_str())));
  if (handler->IsUndefined()) {
    sem_post(&f4js.sem);
    return;
  }
  
  Local<Function> cb = tpl->GetFunction();
  std::string cbName = symName + "Completion";
  cb->SetName(String::NewSymbol(cbName.c_str()));
  Local<String> path = String::New(f4js_cmd.in_path);
  Local<Value> argv[] = { path, cb };
  handler->Call(Context::GetCurrent()->Global(), 2, argv);  
}

// ---------------------------------------------------------------------------

Handle<Value> Start(const Arguments& args) {
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
  strncpy(f4js.root, root, sizeof(f4js.root));
  f4js.handlers = Persistent<Object>::New(Local<Object>::Cast(args[1]));

  sem_init(&f4js.sem, 0, 0);
  uv_async_init(uv_default_loop(), &f4js.async, DispatchOp);

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_create(&f4js.fuse_thread, &attr, fuse_thread, NULL);
  return scope.Close(String::New("dummy"));
}

// ---------------------------------------------------------------------------

void init(Handle<Object> target) {
  target->Set(String::NewSymbol("start"), FunctionTemplate::New(Start)->GetFunction());
}

// ---------------------------------------------------------------------------

NODE_MODULE(fuse4js, init)
