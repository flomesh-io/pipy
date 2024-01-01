/*
 *  Copyright (c) 2019 by flomesh.io
 *
 *  Unless prior written consent has been obtained from the copyright
 *  owner, the following shall not be allowed.
 *
 *  1. The distribution of any source codes, header files, make files,
 *     or libraries of the software.
 *
 *  2. Disclosure of any source codes pertaining to the software to any
 *     additional parties.
 *
 *  3. Alteration or removal of any notices in or on the software or
 *     within the documentation included within the software.
 *
 *  ALL SOURCE CODE AS WELL AS ALL DOCUMENTATION INCLUDED WITH THIS
 *  SOFTWARE IS PROVIDED IN AN “AS IS” CONDITION, WITHOUT WARRANTY OF ANY
 *  KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 *  OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 *  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 *  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 *  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "pipy.hpp"
#include "codebase.hpp"
#include "configuration.hpp"
#include "context.hpp"
#include "worker.hpp"
#include "worker-thread.hpp"
#include "status.hpp"
#include "net.hpp"
#include "outbound.hpp"
#include "utils.hpp"

#ifdef _WIN32
#include <Windows.h>
#include <fcntl.h>
#include <io.h>
#include <numeric>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

namespace pipy {

static std::function<void(int)> s_on_exit;

void Pipy::on_exit(const std::function<void(int)> &on_exit) {
  s_on_exit = on_exit;
}

static auto exec_args(const std::list<std::string> &args) -> Data* {
  thread_local static Data::Producer s_dp("pipy.exec()");

#ifndef _WIN32
  auto argc = args.size();
  if (!argc) return nullptr;

  int n = 0;
  char *argv[argc + 1];
  for (const auto &arg : args) argv[n++] = strdup(arg.c_str());
  argv[n] = nullptr;

  int in[2], out[2];
  pipe(in);
  pipe(out);

  auto pid = fork();
  if (!pid) {
    dup2(in[0], 0);
    dup2(out[1], 1);
    execvp(argv[0], argv);
    exit(-1);
  } else if (pid < 0) {
    return nullptr;
  }

  std::thread t(
    [&]() {
      int status;
      waitpid(pid, &status, 0);
      close(out[1]);
    }
  );

  Data output;
  char buf[DATA_CHUNK_SIZE];
  while (auto len = read(out[0], buf, sizeof(buf))) {
    output.push(buf, len, &s_dp);
  }

  t.join();

  close(out[0]);
  close(in[0]);
  close(in[1]);

  for (int i = 0; i < n; i++) std::free(argv[i]);

#else
  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.lpSecurityDescriptor = NULL;
  sa.bInheritHandle = TRUE;

  Data output;
  HANDLE in, out;
  auto success = CreatePipe(&in, &out, &sa, 0);
  if (success == FALSE) {
    throw std::runtime_error(
      "Unable to create pipe due to " + Win32_GetLastError("CreatePipe")
    );
  }

  auto cmd =
      std::accumulate(std::next(args.begin()), args.end(), args.front(),
                      [](std::string a, std::string b) { return a + " " + b; });

  std::thread t([&]() {
    char buf[DATA_CHUNK_SIZE];
    DWORD len;
    while (ReadFile(in, buf, sizeof(buf), &len, NULL)) {
      output.push(buf, len, &s_dp);
    }
  });

  PROCESS_INFORMATION pif = {};
  STARTUPINFO si = {.cb = sizeof(STARTUPINFO),
                    .dwFlags = STARTF_USESTDHANDLES,
                    .hStdInput = INVALID_HANDLE_VALUE,
                    .hStdOutput = out,
                    .hStdError = out};

  success = CreateProcess(NULL, const_cast<char *>(cmd.c_str()), NULL, NULL,
                          TRUE, 0, NULL, NULL, &si, &pif);
  if (success == FALSE) {
    CloseHandle(out);
    CloseHandle(in);
    throw std::runtime_error(
      "Unable to exec process due to " + Win32_GetLastError("CreateProcess")
    );
  }

  WaitForSingleObject(pif.hProcess, INFINITE);
  CloseHandle(pif.hThread);
  CloseHandle(pif.hProcess);

  CloseHandle(out);
  CloseHandle(in);

  t.join();
#endif
  return Data::make(std::move(output));
}

auto Pipy::exec(const std::string &cmd) -> Data* {
  return exec_args(utils::split(cmd, ' '));
}

auto Pipy::exec(pjs::Array *argv) -> Data* {
  if (!argv) return nullptr;

  std::list<std::string> args;
  argv->iterate_all(
    [&](pjs::Value &v, int) {
      auto *s = v.to_string();
      args.push_back(s->str());
      s->release();
    }
  );

  return exec_args(args);
}

void Pipy::operator()(pjs::Context &ctx, pjs::Object *obj, pjs::Value &ret) {
  pjs::Value ret_obj;
  pjs::Object *context_prototype = nullptr;
  if (!ctx.arguments(0, &context_prototype)) return;
  if (context_prototype && context_prototype->is_function()) {
    auto *f = context_prototype->as<pjs::Function>();
    (*f)(ctx, 0, nullptr, ret_obj);
    if (!ctx.ok()) return;
    if (!ret_obj.is_object()) {
      ctx.error("function did not return an object");
      return;
    }
    context_prototype = ret_obj.o();
  }
  try {
    auto config = Configuration::make(context_prototype);
    ret.set(config);
  } catch (std::runtime_error &err) {
    ctx.error(err);
  }
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void ClassDef<Pipy>::init() {
  super<Function>();
  ctor();

  variable("inbound", class_of<Pipy::Inbound>());
  variable("outbound", class_of<Pipy::Outbound>());

  accessor("pid", [](Object *, Value &ret) {
#ifdef _WIN32
    ret.set((int)_getpid());
#else
    ret.set((int)getpid());
#endif
  });

  accessor("since", [](Object *, Value &ret) {
    ret.set(Status::LocalInstance::since);
  });

  accessor("source", [](Object *, Value &ret) {
    thread_local static pjs::Ref<pjs::Str> str;
    if (!str) str = pjs::Str::make(Status::LocalInstance::source);
    ret.set(str);
  });

  accessor("name", [](Object *, Value &ret) {
    thread_local static pjs::Ref<pjs::Str> str;
    if (!str) str = pjs::Str::make(Status::LocalInstance::name);
    ret.set(str);
  });

  accessor("uuid", [](Object *, Value &ret) {
    thread_local static pjs::Ref<pjs::Str> str;
    if (!str) str = pjs::Str::make(Status::LocalInstance::uuid);
    ret.set(str);
  });

  method("load", [](Context &ctx, Object*, Value &ret) {
    std::string filename;
    if (!ctx.arguments(1, &filename)) return;
    auto path = utils::path_normalize(filename);
    auto data = Codebase::current()->get(path);
    ret.set(data ? pipy::Data::make(*data) : nullptr);
    if (data) data->release();
  });

  method("list", [](Context &ctx, Object*, Value &ret) {
    std::string pathname;
    if (!ctx.arguments(1, &pathname)) return;
    auto codebase = Codebase::current();
    auto a = Array::make();
    std::function<void(const std::string&, const std::string&)> list_dir;
    list_dir = [&](const std::string &path, const std::string &base) {
      for (const auto &name : codebase->list(path)) {
        if (name.back() == '/') {
          auto sub = name.substr(0, name.length() - 1);
          auto str = path + '/' + sub;
          list_dir(str, base + sub + '/');
        } else {
          a->push(Str::make(base + name));
        }
      }
    };
    list_dir(utils::path_normalize(pathname), "");
    ret.set(a);
  });

  method("solve", [](Context &ctx, Object*, Value &ret) {
    Str *filename;
    if (!ctx.arguments(1, &filename)) return;
    auto worker = static_cast<pipy::Context*>(ctx.root())->worker();
    worker->solve(ctx, filename, ret);
  });

  method("restart", [](Context&, Object*, Value&) {
    Net::main().post(
      []() {
        InputContext ic;
        Codebase::current()->sync(
          true, [](bool ok) {
            if (ok) {
              WorkerManager::get().reload();
            }
          }
        );
      }
    );
  });

  method("exit", [](Context &ctx, Object*, Value&) {
    int exit_code = 0;
    if (!ctx.arguments(0, &exit_code)) return;
    Net::main().post(
      [=]() {
        WorkerManager::get().stop(true);
        if (s_on_exit) s_on_exit(exit_code);
      }
    );
  });

  method("exec", [](Context &ctx, Object*, Value &ret) {
    Str *cmd;
    Array *argv;
    try {
      if (ctx.get(0, cmd)) {
        ret.set(Pipy::exec(cmd->str()));
      } else if (ctx.get(0, argv)) {
        ret.set(Pipy::exec(argv));
      } else {
        ctx.error_argument_type(0, "a string or an array");
      }
    } catch (const std::runtime_error &err) {
      ctx.error(err);
    }
  });
}

template<> void ClassDef<Pipy::Inbound>::init() {
  ctor();

  accessor("count", [](Object*, Value &ret) { ret.set(pipy::Inbound::count()); });

  method("forEach", [](Context &ctx, Object*, Value&) {
    Function *cb;
    if (!ctx.arguments(1, &cb)) return;
    pipy::Inbound::for_each(
      [&](pipy::Inbound *ib) {
        Value arg(ib), ret;
        (*cb)(ctx, 1, &arg, ret);
        return ctx.ok();
      }
    );
  });
}

template<> void ClassDef<Pipy::Outbound>::init() {
  ctor();

  accessor("count", [](Object*, Value &ret) { ret.set(pipy::Outbound::count()); });

  method("forEach", [](Context &ctx, Object*, Value&) {
    Function *cb;
    if (!ctx.arguments(1, &cb)) return;
    pipy::Outbound::for_each(
      [&](pipy::Outbound *ob) {
        Value arg(ob), ret;
        (*cb)(ctx, 1, &arg, ret);
        return ctx.ok();
      }
    );
  });
}

} // namespace pjs
