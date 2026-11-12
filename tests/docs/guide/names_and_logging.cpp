// SPDX-License-Identifier: MIT-0
//
// docs/guide/names-and-logging.md: the page shows the regions between --8<-- lines, and the checks stay here.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>

#include "docs/guide/model.hpp"
#include "docs/log_capture.hpp"
#include "dreamor/entity_vector.hpp"
#include "dreamor/format_buffer.hpp"
#include "dreamor/index_set.hpp"
#include "dreamor/logger.hpp"
#include "support/check.hpp"

namespace {

using namespace guide;

// --8<-- [start:shift]
enum class Shift { kDay, kNight };  // no std::formatter of its own: names as its number
// --8<-- [end:shift]

// --8<-- [start:format-buffer]
class RowNames : dreamor::FormatBuffer<64> {  // inherited privately: format() is for its members
 public:
  [[nodiscard]] static const char* capacity(cMachine m, cPeriod t) { return format("Capacity[{},{}]", m, t); }
};

// --8<-- [end:format-buffer]

// --8<-- [start:log]
void logSets(const Sets& sets, const dreamor::EntityVector<Item>& items) {
  DREAMOR_LOG_INFO("{} rows of production over {} periods", sets.produce.size(), sets.periods.size());
  if (sets.needed.size() < items.size()) {
    DREAMOR_LOG_WARNING("{} of {} items are needed by no one", items.size() - sets.needed.size(), items.size());
  }
  if (sets.produce.empty()) { DREAMOR_LOG_ERROR("no machine makes an item that is needed"); }
}

// --8<-- [end:log]

bool holds(const std::string& text, std::string_view part) {
  return text.find(part) != std::string::npos;
}

void nameARow(const Plant& p) {
  const dreamor::EntityVector<Item>& items       = p.items;
  const dreamor::EntityVector<Machine>& machines = p.machines;

  // --8<-- [start:indexed-name]
  const cMachine m{machines.at("M1")};
  const cPeriod t{2};
  const char* name = dreamor::indexedName("Hours", m, t);  // Hours_M1_2
  // --8<-- [end:indexed-name]
  DREAMOR_CHECK(std::string_view{name} == "Hours_M1_2");
  const char* next = dreamor::indexedName("Hours", m, 3);
  DREAMOR_CHECK(next == name && std::string_view{next} == "Hours_M1_3");  // the same buffer, written over

  // --8<-- [start:key-parts]
  const std::string supplier = "acme";
  const std::string buy      = dreamor::indexedName("Buy", supplier, items.at("bolt"));  // Buy_acme_bolt
  const std::string overtime = dreamor::indexedName("Overtime", m, Shift::kNight, t);    // Overtime_M1_1_2
  // --8<-- [end:key-parts]
  DREAMOR_CHECK(buy == "Buy_acme_bolt");
  DREAMOR_CHECK(overtime == "Overtime_M1_1_2");
}

void anotherShape(const Plant& p) {
  const dreamor::EntityVector<Item>& items       = p.items;
  const dreamor::EntityVector<Machine>& machines = p.machines;
  const cMachine m{machines.at("M1")};
  const cItem i{items.at("frame")};
  const cPeriod t{2};

  // --8<-- [start:format]
  const std::string make = std::format("Make[{},{},{}]", m, i, t);  // Make[M1,frame,2]
  const std::string line = std::format("{:<8}{:>4}", i, t);         // "frame      2"
  // --8<-- [end:format]
  DREAMOR_CHECK(make == "Make[M1,frame,2]");
  DREAMOR_CHECK(line == "frame      2");
}

void inABuffer(const Plant& p) {
  const dreamor::EntityVector<Machine>& machines = p.machines;

  // --8<-- [start:capacity-name]
  const cMachine m1{machines.at("M1")};
  const char* name = RowNames::capacity(m1, 1);  // Capacity[M1,1]
  // --8<-- [end:capacity-name]
  DREAMOR_CHECK(std::string_view{name} == "Capacity[M1,1]");
  const char* second = RowNames::capacity(cMachine{machines.at("M2")}, 3);
  DREAMOR_CHECK(second == name && std::string_view{second} == "Capacity[M2,3]");  // one buffer, the thread's
}

void logMessages(const Plant& p) {
  const dreamor::EntityVector<Item>& items = p.items;
  const Sets sets                          = byChain(p, bomTable(p));
  DREAMOR_CHECK(sets.produce.size() == 15 && sets.periods.size() == 3 && sets.needed.size() == 4);
  DREAMOR_CHECK(!sets.needed.contains(items.at("bracket")));  // the one item needed by no one

  const LogCapture log;
  DREAMOR_CHECK(log.isOpen());
  logSets(sets, items);
  const std::string text = log.text();

  DREAMOR_CHECK(holds(text, "[INFO] 15 rows of production over 3 periods\n"));
  DREAMOR_CHECK(holds(text, "[WARN] 1 of 5 items are needed by no one\n"));
  DREAMOR_CHECK(!holds(text, "[ERROR]"));
  // A line: the time, the file and line, the level, the message.
  DREAMOR_CHECK(text.starts_with("[+") && holds(text, "ms] [names_and_logging.cpp:"));
}

void chooseWhere(const Sets& sets, const dreamor::EntityVector<Item>& items, const std::string& log_path) {
  std::FILE* const before = dreamor::logger::getOutput();

  // --8<-- [start:level-and-file]
  dreamor::logger::setLevel(dreamor::logger::Level::kWarning);  // every thread drops info lines from here on
  const dreamor::logger::FileSink sink{log_path.c_str(), "w"};  // this thread's lines go to the file while sink lives
  logSets(sets, items);
  // --8<-- [end:level-and-file]
  DREAMOR_CHECK(sink.isOpen() && dreamor::logger::getOutput() != before);

  // Another thread: the level is the process's, the output each thread's own.
  std::FILE* other_output = nullptr;
  auto other_level        = dreamor::logger::Level::kInfo;
  const auto look         = [&] {
    other_output = dreamor::logger::getOutput();
    other_level  = dreamor::logger::getLevel();
  };
  std::thread{look}.join();
  DREAMOR_CHECK(other_output == stderr && other_level == dreamor::logger::Level::kWarning);
}

void levelAndFile(const Plant& p) {
  const Sets sets                     = byChain(p, bomTable(p));
  const dreamor::logger::Level level  = dreamor::logger::getLevel();
  std::FILE* const before             = dreamor::logger::getOutput();
  const std::filesystem::path logFile = std::filesystem::temp_directory_path() / "dreamor_guide_names_and_logging.log";

  {
    std::ofstream earlier{logFile};
    earlier << "a line of an earlier run\n";
  }
  chooseWhere(sets, p.items, logFile.string());
  DREAMOR_CHECK(dreamor::logger::getOutput() == before);  // sink gone: the lines go where they went before
  DREAMOR_CHECK(dreamor::logger::getLevel() == dreamor::logger::Level::kWarning);
  dreamor::logger::setLevel(level);

  std::ifstream in{logFile};
  const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
  in.close();  // Windows does not remove a file that is open
  std::filesystem::remove(logFile);
  DREAMOR_CHECK(holds(text, "[WARN] 1 of 5 items are needed by no one\n"));
  DREAMOR_CHECK(std::ranges::count(text, '\n') == 1);  // the warning line alone
  DREAMOR_CHECK(!holds(text, "[INFO]"));
  DREAMOR_CHECK(!holds(text, "an earlier run"));  // "w": the file started afresh
}

}  // namespace

int main() try {
  const Plant p = plant();
  nameARow(p);
  anotherShape(p);
  inABuffer(p);
  logMessages(p);
  levelAndFile(p);
  return ::report("guide/names-and-logging");
} catch (...) { return ::uncaught(); }
