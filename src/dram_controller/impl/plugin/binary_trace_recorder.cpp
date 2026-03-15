#include <algorithm>
#include <filesystem>
#include <format>
#include <vector>

#include "base/base.h"
#include "dram_controller/controller.h"
#include "dram_controller/plugin.h"
#include "mtrc/mtrc.h"

/// NOTE: Definiton of the file format is found in mtrc/mtrc.h

namespace Ramulator {
class BinaryTraceRecorder : public IControllerPlugin, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(
      IControllerPlugin, BinaryTraceRecorder, "BinaryTraceRecorder",
      "Records the trace in a fixed-width binary format.")
private:
  IDRAM *m_dram;
  MTRCWriter *m_writer;
  std::filesystem::path m_trace_path;
  std::vector<int32_t> m_command_latencies;

  Clk_t m_clk = 0;
  std::string m_trace_path_base;

public:
  int32_t infer_command_latency(int command) const {
    int32_t latency = 1;
    bool found = false;

    for (const auto &level_cons : m_dram->m_timing_cons) {
      for (const auto &timing : level_cons[command]) {
        if (timing.cmd == command && timing.val > 0) {
          latency = found ? std::min(latency, timing.val) : timing.val;
          found = true;
        }
      }
    }

    return latency;
  }

  void init() override {
    m_trace_path_base =
        param<std::string>("path")
            .desc("Path to the trace file, minus the file extension")
            .required();
  }

  void setup(IFrontEnd *frontend, IMemorySystem *memory_system) override {
    m_ctrl = cast_parent<IDRAMController>();
    m_dram = m_ctrl->m_dram;

    m_trace_path =
        std::format("{}_{}.mtrc", m_trace_path_base, m_ctrl->m_channel_id);
    m_writer = new MTRCWriter(
        m_trace_path, m_dram->m_timing_vals("nCL"), m_dram->m_timing_vals("nCWL"),
        static_cast<int16_t>(m_dram->get_level_size("channel")),
        static_cast<int16_t>(m_dram->get_level_size("rank")),
        static_cast<int16_t>(m_dram->get_level_size("bankgroup")),
        static_cast<int16_t>(m_dram->get_level_size("bank")));

    m_command_latencies.resize(m_dram->m_commands.size(), 1);
    for (int cmd = 0; cmd < m_dram->m_commands.size(); cmd++) {
      m_command_latencies[cmd] = infer_command_latency(cmd);
    }
  }

  void update(bool request_found, ReqBuffer::iterator &req_it) override {
    m_clk++;

    if (request_found) {
      int32_t req_issue_duration = 1;
      if (req_it->type_id != Request::Type::Write &&
          req_it->arrive >= 0 && req_it->depart >= req_it->arrive) {
        auto issue_window = req_it->depart - req_it->arrive;
        req_issue_duration =
            static_cast<int32_t>(std::max<Clk_t>(1, issue_window));
      }

      m_writer->write_entry(m_clk, req_it->addr_vec,
                            m_dram->m_commands(req_it->command),
                            req_it->source_id, req_it->type_id, req_it->arrive,
                            req_it->depart, req_issue_duration,
                            m_command_latencies[req_it->command]);
    }
  }

  void finalize() override { m_writer->finalize(); }
};
} // namespace Ramulator