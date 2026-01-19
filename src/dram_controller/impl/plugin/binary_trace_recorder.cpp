#include <filesystem>
#include <format>

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

  Clk_t m_clk = 0;
  std::string m_trace_path_base;

public:
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
    m_writer = new MTRCWriter(m_trace_path);
  }

  void update(bool request_found, ReqBuffer::iterator &req_it) override {
    m_clk++;

    if (request_found) {
      m_writer->write_entry(m_clk, req_it->addr_vec,
                            m_dram->m_commands(req_it->command));
    }
  }

  void finalize() override { m_writer->finalize(); }
};
} // namespace Ramulator