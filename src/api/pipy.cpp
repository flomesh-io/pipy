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
#include "os-platform.hpp"
#include "utils.hpp"
#include "log.hpp"

#include <exception>

#ifndef _WIN32
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#endif

namespace pipy {

static std::function<void(int)> s_on_exit;

void Pipy::on_exit(const std::function<void(int)> &on_exit) {
  s_on_exit = on_exit;
}

thread_local static Data::Producer s_dp("pipy.exec()");

Pipy::ExecOptions::ExecOptions(pjs::Object *options) {
  Value(options, "stdin")
    .get(buf_stdin)
    .check_nullable();
  Value(options, "stderr")
    .get(buf_stderr)
    .check_nullable();
  Value(options, "onExit")
    .get(on_exit_f)
    .check_nullable();
}

#ifndef _WIN32

static auto exec_argv(const std::list<std::string> &args, int &exit_code, const Pipy::ExecOptions &options) -> Data* {
  auto argc = args.size();
  if (!argc) {
    throw std::runtime_error("exec() with no arguments");
  }

  int n = 0;
  char *argv[argc + 1];
  for (const auto &arg : args) argv[n++] = strdup(arg.c_str());
  argv[n] = nullptr;

  std::thread t_stdout, t_stderr;
  std::vector<uint8_t> buf_stdout;
  std::vector<uint8_t> buf_stderr;

  int pipes[3][2];
  std::memset(pipes, 0, sizeof(pipes));

  try {
    if (pipe(pipes[1]) ||
      (options.buf_stdin && pipe(pipes[0])) ||
      (options.buf_stderr && pipe(pipes[2]))
    ) {
      throw std::runtime_error("unable to create pipes");
    }

    auto pid = fork();
    if (!pid) {
      if (pipes[0][0]) dup2(pipes[0][0], 0);
      dup2(pipes[1][1], 1);
      dup2(pipes[2][1] ? pipes[2][1] : pipes[1][1], 2);
      execvp(argv[0], argv);
      std::terminate();
    } else if (pid < 0) {
      throw std::runtime_error("unable to fork");
    }

    auto read_pipe = [&](int pipe) -> std::vector<uint8_t> {
      Data data;
      Data::Builder db(data, &s_dp);
      char buf[DATA_CHUNK_SIZE];

      while (auto len = read(pipe, buf, sizeof(buf))) {
        if (len < 0) break;
        db.push(buf, len);
      }
      db.flush();

      // Unfortunately we were not able to return this in SharedData
      // because SharedData relies on thread-local pjs::Pool,
      // which is dead right after the thread ends.
      return data.to_bytes();
    };

    if (pipes[1][0]) t_stdout = std::thread([&]() { buf_stdout = read_pipe(pipes[1][0]); });
    if (pipes[2][0]) t_stderr = std::thread([&]() { buf_stderr = read_pipe(pipes[2][0]); });

    if (auto *data = options.buf_stdin.get()) {
      for (auto c : data->chunks()) {
        auto ptr = std::get<0>(c);
        auto len = std::get<1>(c);
        bool err = false;
        while (len > 0) {
          auto n = write(pipes[0][1], ptr, len);
          if (n < 0) { err = true; break; }
          len -= n;
          ptr += n;
        }
        if (err) break;
      }
    }

    for (int i = 0; i < 3; i++) {
      if (pipes[i][1]) {
        close(pipes[i][1]);
        pipes[i][1] = 0;
      }
    }

    int status;
    waitpid(pid, &status, 0);
    exit_code = WEXITSTATUS(status);

  } catch (std::runtime_error &) {
    for (int i = 0; i < 3; i++) {
      if (pipes[i][0]) close(pipes[i][0]);
      if (pipes[i][1]) close(pipes[i][1]);
    }
    throw;
  }

  if (t_stdout.joinable()) t_stdout.join();
  if (t_stderr.joinable()) t_stderr.join();

  for (int i = 0; i < 3; i++) {
    if (pipes[i][0]) close(pipes[i][0]);
    if (pipes[i][1]) close(pipes[i][1]);
  }

  if (options.buf_stderr) {
    s_dp.push(options.buf_stderr.get(), buf_stderr);
  }

  return s_dp.make(buf_stdout);
}

auto Pipy::exec(const std::string &cmd, int &exit_code, const ExecOptions &options) -> Data* {
  return exec_argv(utils::split_argv(cmd), exit_code, options);
}

auto Pipy::exec(pjs::Array *argv, int &exit_code, const ExecOptions &options) -> Data* {
  if (!argv) return nullptr;

  std::list<std::string> args;
  argv->iterate_all(
    [&](pjs::Value &v, int) {
      auto *s = v.to_string();
      args.push_back(s->str());
      s->release();
    }
  );

  return exec_argv(args, exit_code, options);
}

#else // _WIN32

static auto exec_line(const std::string &line, int &exit_code, const Pipy::ExecOptions &options) -> Data* {
  std::thread t_stdout, t_stderr;
  std::vector<uint8_t> buf_stdout;
  std::vector<uint8_t> buf_stderr;

  HANDLE pipes[3][2];
  ZeroMemory(pipes, sizeof(pipes));

  try {
    SECURITY_ATTRIBUTES sa;
    ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(&pipes[1][0], &pipes[1][1], &sa, 0) ||
      (options.buf_stdin && !CreatePipe(&pipes[0][0], &pipes[0][1], &sa, 0)) ||
      (options.buf_stderr && !CreatePipe(&pipes[2][0], &pipes[2][1], &sa, 0))
    ) {
      throw std::runtime_error(
        "unable to create pipe: " + os::windows::get_last_error()
      );
    }

    PROCESS_INFORMATION pif = {};
    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(STARTUPINFO);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = pipes[0][0] ? pipes[0][0] : INVALID_HANDLE_VALUE;
    si.hStdOutput = pipes[1][1];
    si.hStdError = pipes[2][1] ? pipes[2][1] : pipes[1][1];

    auto line_w = os::windows::a2w(line);
    pjs::vl_array<wchar_t, 1000> buf_line(line_w.length() + 1);
    std::memcpy(buf_line.data(), line_w.c_str(), buf_line.size() * sizeof(wchar_t));

    if (!CreateProcessW(
      NULL,
      buf_line.data(),
      NULL, NULL, TRUE, 0, NULL, NULL,
      &si, &pif
    )) {
      throw std::runtime_error(
        "unable to create process '" + line + "': " + os::windows::get_last_error()
      );
    }

    auto read_pipe = [&](HANDLE pipe) -> std::vector<uint8_t> {
      Data data;
      Data::Builder db(data, &s_dp);
      char buf[DATA_CHUNK_SIZE];
      DWORD len;

      while (ReadFile(pipe, buf, sizeof(buf), &len, NULL)) {
        db.push(buf, len);
      }
      db.flush();

      // Unfortunately we were not able to return this in SharedData
      // because SharedData relies on thread-local pjs::Pool,
      // which is dead right after the thread ends.
      return data.to_bytes();
    };

    if (pipes[1][0]) t_stdout = std::thread([&]() { buf_stdout = read_pipe(pipes[1][0]); });
    if (pipes[2][0]) t_stderr = std::thread([&]() { buf_stderr = read_pipe(pipes[2][0]); });

    if (auto *data = options.buf_stdin.get()) {
      for (auto c : data->chunks()) {
        auto ptr = std::get<0>(c);
        auto len = std::get<1>(c);
        DWORD written;
        if (!WriteFile(pipes[0][1], ptr, len, &written, NULL)) break;
      }
    }

    for (int i = 0; i < 3; i++) {
      if (pipes[i][1]) {
        CloseHandle(pipes[i][1]);
        pipes[i][1] = 0;
      }
    }

    WaitForSingleObject(pif.hProcess, INFINITE);

    DWORD code;
    GetExitCodeProcess(pif.hProcess, &code);
    exit_code = code;

    CloseHandle(pif.hThread);
    CloseHandle(pif.hProcess);

  } catch (std::runtime_error &) {
    for (int i = 0; i < 3; i++) {
      if (pipes[i][0]) CloseHandle(pipes[i][0]);
      if (pipes[i][1]) CloseHandle(pipes[i][1]);
    }
    throw;
  }

  if (t_stdout.joinable()) t_stdout.join();
  if (t_stderr.joinable()) t_stderr.join();

  for (int i = 0; i < 3; i++) {
    if (pipes[i][0]) CloseHandle(pipes[i][0]);
    if (pipes[i][1]) CloseHandle(pipes[i][1]);
  }

  if (options.buf_stderr) {
    s_dp.push(options.buf_stderr.get(), buf_stderr);
  }

  return s_dp.make(buf_stdout);
}

auto Pipy::exec(const std::string &cmd, int &exit_code, const ExecOptions &options) -> Data* {
  return exec_line(cmd, exit_code, options);
}

auto Pipy::exec(pjs::Array *argv, int &exit_code, const ExecOptions &options) -> Data* {
  if (!argv) return nullptr;

  std::list<std::string> args;
  argv->iterate_all(
    [&](pjs::Value &v, int) {
      auto *s = v.to_string();
      args.push_back(s->str());
      s->release();
    }
  );

  return exec_line(os::windows::encode_argv(args), exit_code, options);
}

#endif // _WIN32

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
    ret.set(os::process_id());
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
    Str *cmd = nullptr;
    Array *argv = nullptr;
    try {
      if (ctx.get(0, cmd) || ctx.get(0, argv)) {
        Object *options;
        if (!ctx.check<Object>(1, options, nullptr)) return;
        Pipy::ExecOptions opts(options);
        pipy::Data *out = nullptr;
        int exit_code = 0;
        if (cmd) {
          out = Pipy::exec(cmd->str(), exit_code, opts);
        } else {
          out = Pipy::exec(argv, exit_code, opts);
        }
        if (auto f = opts.on_exit_f.get()) {
          int argc = 1;
          Value args[2], ret;
          args[0].set(exit_code);
          if (auto err = opts.buf_stderr.get()) {
            args[1].set(err);
            argc = 2;
          }
          (*f)(ctx, argc, args, ret);
        }
        ret.set(out);
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
