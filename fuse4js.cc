#include <node.h>
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
// #include <pthread.h>
#include <semaphore.h>
//#include <math.h>
#include <string>

/*
#include <vector>
*/

using namespace v8;

// ---------------------------------------------------------------------------

static struct {
  uv_async_t async;
  sem_t sem;
  pthread_t fuse_thread;
  char root[256];
  Persistent<Object> handlers; 
} f4js;

enum fuseop_t {  
  OP_EXIT = 0,
  OP_GETATTR = 1,
  OP_READDIR = 2
};

/*
typedef struct {
  std::string name;
  mode_t mode;
} file_name_and_mode_t;
*/

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
      // std::vector<std::string> dirents;
    } readdir;
  } u;
  int retval;
} f4js_cmd;

static int getattr(const char *path, struct stat *stbuf)
{
  f4js_cmd.op = OP_GETATTR;
  f4js_cmd.in_path = path;
  f4js_cmd.u.getattr.stbuf = stbuf;
  uv_async_send(&f4js.async);
  sem_wait(&f4js.sem);
  return f4js_cmd.retval;
}

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
  
  /*
  DIR *dp;
  struct dirent *de;
  
  (void) offset;
  (void) fi;
  
  dp = opendir(path);
  if (dp == NULL)
    return -errno;
  
  while ((de = readdir(dp)) != NULL) {
    struct stat st;
    memset(&st, 0, sizeof(st));
    st.st_ino = de->d_ino;
    st.st_mode = de->d_type << 12;
    if (filler(buf, de->d_name, &st, 0))
      break;
  }
  
  closedir(dp);
  */
  return 0;
}

void *fuse_thread(void *)
{
  struct fuse_operations oper = { 0 };
  oper.getattr = getattr;
  oper.readdir = readdir;
  char *argv[] = { "dummy", "-s", "-d", f4js.root };
  fuse_main(4, argv, &oper, NULL);
  f4js_cmd.op = OP_EXIT;
  uv_async_send(&f4js.async);
  return NULL;
}

// ---------------------------------------------------------------------------

Handle<Value> GetAttrCb(const Arguments& args)
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

Handle<Value> ReadDirCb(const Arguments& args)
{
  HandleScope scope;
  if (args.Length() >= 1 && args[0]->IsNumber()) {
    Local<Number> retval = Local<Number>::Cast(args[0]);
    f4js_cmd.retval = (int)retval->Value();
    if (f4js_cmd.retval == 0 && args.Length() >= 2 && args[1]->IsArray()) {
      Handle<Array> ar = Handle<Array>::Cast(args[1]);
      for (uint32_t i = 0; i < ar->Length(); i++) {
        Handle<Value> el = ar->Get(i);
        if (!el->IsUndefined() && el->IsObject()) {
          Handle<Object> dirent = Handle<Object>::Cast(el);
          Local<Value> prop = dirent->Get(String::NewSymbol("name"));
          if (!prop->IsUndefined() && prop->IsString()) {
            Local<String> name = Local<String>::Cast(prop);
            String::AsciiValue av(name);
            
            struct stat st;
            memset(&st, 0, sizeof(st));
            st.st_ino = 0;
            st.st_mode = 33261;
            if (f4js_cmd.u.readdir.filler(f4js_cmd.u.readdir.buf, *av, &st, 0))
              break;            
          }
        }
      }
    }
  }
  sem_post(&f4js.sem);  
  return scope.Close(Undefined());    
}

// ---------------------------------------------------------------------------

// Called from the main thread.
static void F4jsAsyncCb(uv_async_t* handle, int status) {
  HandleScope scope;
  std::string symName;
  Local<FunctionTemplate> tpl;
  f4js_cmd.retval = -EINVAL;
  
  switch (f4js_cmd.op) {
  case OP_EXIT:
    pthread_join(f4js.fuse_thread, NULL);
    uv_unref((uv_handle_t*) &f4js.async);
    return;
    
  case OP_GETATTR:
    symName = "getattr";
    tpl = FunctionTemplate::New(GetAttrCb);
    break;
  
  case OP_READDIR:
    symName = "readdir";
    tpl = FunctionTemplate::New(ReadDirCb);
    break;
  
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
  std::string cbName = symName + "Cb";
  cb->SetName(String::NewSymbol(cbName.c_str()));
  Local<String> path = String::New(f4js_cmd.in_path);
  Local<Value> argv[] = { path, cb };
  handler->Call(Context::GetCurrent()->Global(), 2, argv);  
}

Handle<Value> Method(const Arguments& args) {
  HandleScope scope;
  return scope.Close(String::New("world"));
}

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
  
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_create(&f4js.fuse_thread, &attr, fuse_thread, NULL);
  return scope.Close(String::New("dummy"));
}


void init(Handle<Object> target) {
  target->Set(String::NewSymbol("hello"), FunctionTemplate::New(Method)->GetFunction());
  target->Set(String::NewSymbol("start"), FunctionTemplate::New(Start)->GetFunction());
  sem_init(&f4js.sem, 0, 0);
  uv_async_init(uv_default_loop(), &f4js.async, F4jsAsyncCb);
}


NODE_MODULE(fuse4js, init)
