#include <pipy/nmi.h>
#include <string>
#include <thread>
#include <cstdlib>

//
// PingPipeline
//

class PingPipeline {
public:
  PingPipeline(pipy_pipeline pipeline) : m_pipeline(pipeline) {}

  void process(pjs_value evt) {
    if (pipy_is_MessageStart(evt)) {
      if (!m_message_started) {
        m_message_started = true;
        m_message_body.clear();
      }
    } else if (pipy_is_Data(evt)) {
      if (m_message_started) {
        auto len = pipy_Data_get_size(evt);
        auto buf = new char[len];
        pipy_Data_get_data(evt, buf, len);
        m_message_body.append(buf, len);
        delete [] buf;
      }
    } else if (pipy_is_MessageEnd(evt)) {
      if (m_message_started) {
        new Ping(m_pipeline, m_message_body);
        m_message_started = false;
      }
    }
  }

private:
  pipy_pipeline m_pipeline;
  std::string m_message_body;
  bool m_message_started = false;

  //
  // PingPipeline::Ping
  //

  class Ping {
  public:
    Ping(pipy_pipeline pipeline, const std::string &host)
      : m_pipeline(pipeline)
      , m_host(host)
    {
      pipy_hold(pipeline);
      std::thread(
        [this]() {
          auto cmd = "ping -c 1 " + m_host;
          m_result = std::system(cmd.c_str());
          pipy_schedule(m_pipeline, 0, output, this);
        }
      ).detach();
    }

  private:
    pipy_pipeline m_pipeline;
    std::string m_host;
    int m_result;

    static void output(void *user_ptr) {
      static_cast<Ping*>(user_ptr)->output();
    }

    void output() {
      std::string msg(m_result ? "Failed" : "Succeeded");
      msg += " to ping " + m_host + '\n';
      pipy_output_event(m_pipeline, pipy_Data_new(msg.c_str(), msg.length()));
      pipy_free(m_pipeline);
      delete this;
    }
  };
};

static void pipeline_init(pipy_pipeline ppl, void **user_ptr) {
  *user_ptr = new PingPipeline(ppl);
}

static void pipeline_free(pipy_pipeline ppl, void *user_ptr) {
  delete static_cast<PingPipeline*>(user_ptr);
}

static void pipeline_process(pipy_pipeline ppl, void *user_ptr, pjs_value evt) {
  static_cast<PingPipeline*>(user_ptr)->process(evt);
}

extern "C" void pipy_module_init() {
  pipy_define_pipeline(
    "",
    pipeline_init,
    pipeline_free,
    pipeline_process
  );
}
