#include <pipy/nmi.h>
#include <atomic>
#include <thread>
#include <string>

class CounterPipeline {
  pipy_pipeline m_pipeline;
  std::atomic<int> m_counter;
  std::thread m_thread;

  void main() {
    auto ppl = m_pipeline;
    for (int i = 0; i < 10; i++) {
      m_counter++;
      pipy_schedule(ppl, 0, output_number, this);
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    pipy_schedule(ppl, 0, output_end, this);
  }

  static void output_number(void *user_ptr) {
    auto thiz = static_cast<CounterPipeline*>(user_ptr);
    auto ppl = thiz->m_pipeline;
    auto msg = std::to_string(thiz->m_counter);
    pipy_output_event(ppl, pipy_MessageStart_new(0));
    pipy_output_event(ppl, pipy_Data_new(msg.c_str(), msg.length()));
    pipy_output_event(ppl, pipy_MessageEnd_new(0, 0));
  }

  static void output_end(void *user_ptr) {
    auto thiz = static_cast<CounterPipeline*>(user_ptr);
    auto ppl = thiz->m_pipeline;
    pipy_output_event(ppl, pipy_StreamEnd_new(0));
    pipy_free(ppl);
  }

public:
  CounterPipeline(pipy_pipeline ppl)
    : m_pipeline(ppl)
    , m_counter(0)
    , m_thread([this]() { main(); })
  {
    pipy_hold(m_pipeline);
    m_thread.detach();
  }
};

static void pipeline_init(pipy_pipeline ppl, void **user_ptr) {
  *user_ptr = new CounterPipeline(ppl);
}

static void pipeline_free(pipy_pipeline ppl, void *user_ptr) {
  delete static_cast<CounterPipeline*>(user_ptr);
}

static void pipeline_process(pipy_pipeline ppl, void *user_ptr, pjs_value evt) {
}

extern "C" void pipy_module_init() {
  pipy_define_pipeline(
    "",
    pipeline_init,
    pipeline_free,
    pipeline_process
  );
}
