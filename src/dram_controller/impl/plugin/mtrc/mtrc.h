/**
 * @file mtrc.h
 * Defines the MTRC file format (MTRC = Memory TRaCe)
 * A fixed-width binary memory trace format for fast parsing.
 * +--------------+------+-------------------------------------+
 *
 * A typical trace file should look like this:
 *
 * +----------------+
 * |  Header (24B)  |
 * +----------------+
 * | Entry #1 (32B) |
 * +----------------+
 * | Entry #2 (32B) |
 * +----------------+
 * | ...            |
 * +----------------+
 * | Dictionary (v) |
 * +----------------+
 *
 * HEADER:
 * Holds metadata about the trace.
 * It has a fixed width of 24 bytes.
 *
 * +--------------+------+-------------------------------------+
 * |     Name     | Size |             Description             |
 * +--------------+------+-------------------------------------+
 * | magic        | 5B   | "RAM2\0" (null-terminated)          |
 * | version      | 1B   | Major version of the file format    |
 * | num_commands | 1B   | Number of unique command strings    |
 * | reserved     | 1B   | Padding to align next field to 8B   |
 * | num_entries  | 8B   | Number of entries / trace events    |
 * | dict_offset  | 8B   | Byte offset where dictionary starts |
 * +--------------+------+-------------------------------------+
 *
 * ENTRY:
 * Holds a single trace event.
 * It has a fixed width of 32 bytes, meaning two entries fit into a single
 * 64-bit cache line.
 *
 * All address fields (clk, channel, rank, bankgroup, bank, row, column) are
 * signed integers. Invalid address components are represented as -1.
 *
 * +-------------+------+---------------------------------------------+
 * |    Name     | Size |                 Description                 |
 * +-------------+------+---------------------------------------------+
 * | clk         | 8B   | Clock cycle in which the event occurs       |
 * | channel     | 2B   | Channel                                     |
 * | rank        | 2B   | Rank                                        |
 * | bankgroup   | 4B   | Bankgroup                                   |
 * | bank        | 4B   | Bank                                        |
 * | row         | 4B   | Row                                         |
 * | column      | 4B   | Column                                      |
 * | cmd_id      | 1B   | Command ID (index in the dictionary)        |
 * | reserved    | 3B   | Padding to align struct to 32 bytes         |
 * +-------------+------+---------------------------------------------+
 *
 * DICTIONARY:
 * So we don't store the command string (e.g. "ACT", "RD", "WR", etc.) in the
 * trace file, we create a dictionary of command strings, where we can query
 * them by index (cmd_id). It has a variable width, starting at dict_offset
 * found in the header.
 *
 * +-------------+---------------+
 * | Length (1B) | String Bytes  |
 * +-------------+---------------+
 * | Length (1B) | String Bytes  |
 * +-------------+---------------+
 * | ...         | ...           |
 * +-------------+---------------+
 *
 */

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string_view>
#include <vector>

#define MTRC_VERSION 1
#define MTRC_MAGIC "RAM2"

struct Header {
  char magic[5];        // (0x00) "RAM2\0"
  uint8_t version;      // (0x04) Major version of the file format
  uint8_t num_commands; // (0x05) Number of unique command strings
  uint8_t reserved;     // (0x06) Padding to align next field to 8B
  uint64_t num_entries; // (0x08) Number of entries / trace events
  uint64_t dict_offset; // (0x10) Offset to the dictionary in the file
};

/// @warning(ziad): Do NOT change the order of the fields, or the size of the
/// struct. If you have to, then make sure to update the static_asserts at the
/// bottom of the file and/or reorder the fields for optimal alignment.
/// @todo(ziad): bankgroup, bank, row, column don't have to be 32 bits.
/// Things like channel and rank don't have to be 16 bits, I've never seen them
/// exceed 255. There's opportunity to make the trace event 16 bytes wide,
/// allowing 4 entries per cache line. Also, these fields are signed because
/// 'TraceRecorder' uses -1 as value for invalid address vector components. We
/// could add a 1B flag to indicate invalid values.
struct Entry {
  int64_t clk;         // (0x00) Clock cycle in which the event occurs
  int16_t channel;     // (0x08) Channel
  int16_t rank;        // (0x0A) Rank
  int32_t bankgroup;   // (0x0C) Bankgroup
  int32_t bank;        // (0x10) Bank
  int32_t row;         // (0x14) Row
  int32_t column;      // (0x18) Column
  uint8_t cmd_id;      // (0x1C) Command ID (index in the dictionary)
  uint8_t reserved[3]; // (0x1D) Padding to align struct to 32 bytes
} __attribute__((packed));

class MTRCWriter {
private:
  std::ofstream m_file;
  uint64_t m_num_entries = 0;
  uint64_t m_dict_offset = 0;

  std::vector<std::string> m_commands;

public:
  MTRCWriter(const std::string &path) {
    m_file.open(path, std::ios::binary);
    if (!m_file.is_open()) {
      throw std::runtime_error("Failed to open trace file: " + path);
    }

    init_header();
  }

  void init_header() {
    // Dummy Header to be overwritten after metadata is known.
    Header header = {
        .magic = MTRC_MAGIC,
        .version = MTRC_VERSION,
        .num_commands = 0,
        .reserved = 0,
        .num_entries = 0,
        .dict_offset = 0,
    };

    m_file.write(reinterpret_cast<char *>(&header), sizeof(Header));
  }

  void finalize_header() {
    auto num_commands = static_cast<uint8_t>(m_commands.size());

    Header header = {
        .magic = MTRC_MAGIC,
        .version = MTRC_VERSION,
        .num_commands = num_commands,
        .reserved = 0,
        .num_entries = m_num_entries,
        .dict_offset = m_dict_offset,
    };

    m_file.seekp(0, std::ios::beg);
    m_file.write(reinterpret_cast<char *>(&header), sizeof(Header));
  }

  void write_entry(int64_t clk, const std::vector<int> &addr_vec,
                   std::string_view cmd) {
    std::string cmd_str(cmd);

    // Find the command in the dictionary, or add it if it's not found.
    auto it = std::find(m_commands.begin(), m_commands.end(), cmd_str);
    if (it == m_commands.end()) {
      m_commands.push_back(cmd_str);
      it = m_commands.end() - 1;
    }
    uint8_t cmd_id = static_cast<uint8_t>(it - m_commands.begin());

    auto channel = static_cast<int16_t>(addr_vec[0]);
    auto rank = static_cast<int16_t>(addr_vec[1]);
    auto bankgroup = static_cast<int32_t>(addr_vec[2]);
    auto bank = static_cast<int32_t>(addr_vec[3]);
    auto row = static_cast<int32_t>(addr_vec[4]);
    auto column = static_cast<int32_t>(addr_vec[5]);

    Entry entry = {
        .clk = clk,
        .channel = channel,
        .rank = rank,
        .bankgroup = bankgroup,
        .bank = bank,
        .row = row,
        .column = column,
        .cmd_id = cmd_id,
        .reserved = {},
    };

    m_file.write(reinterpret_cast<char *>(&entry), sizeof(Entry));
    m_num_entries++;
  }

  void write_dictionary() {
    m_dict_offset = sizeof(Header) + sizeof(Entry) * m_num_entries;
    m_file.seekp(m_dict_offset, std::ios::beg);

    for (const auto &cmd : m_commands) {
      uint8_t length = static_cast<uint8_t>(cmd.size());
      m_file.write(reinterpret_cast<const char *>(&length), 1);
      m_file.write(cmd.data(), cmd.size());
    }
  }

  void finalize() {
    write_dictionary();
    finalize_header();
    m_file.close();
  }
};

static_assert(sizeof(Header) == 24, "(mtrc) Header size must be 24 bytes");
static_assert(sizeof(Entry) == 32, "(mtrc) Entry size must be 32 bytes");