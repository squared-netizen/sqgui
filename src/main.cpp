// sqgui: the squared SDK's UI editor. It reads an app's layouts and
// names.json and says what they hold, as words or as JSON (--json) (phase 2,
// the viewer), and changes one thing at a time, keeping every other byte of
// the file (phase 3: set, unset, undo; 0.4: insert, remove, move).
//
//   sqgui list                       every layout: its kind, its nodes, whether it checks
//   sqgui show LAYOUT                its nodes as an outline, each with its path
//   sqgui get LAYOUT PATH            one value: sqgui get settings rows[1][0].bind
//   sqgui check                      every problem the build would stop for (exit 1)
//   sqgui map                        which layout pushes, replaces, opens or closes which
//   sqgui names                      names.json's addresses, and the layouts using each
//   sqgui view                       the terminal face: the layouts in character cells
//   sqgui set LAYOUT PATH VALUE      change a value, or add a key: sqgui set settings rows[1][0].max 20
//   sqgui unset LAYOUT PATH          remove a key
//   sqgui insert LAYOUT PATH VALUE   put a widget or row into a list, at PATH's index
//   sqgui remove LAYOUT PATH         take a widget or row out of its list
//   sqgui move LAYOUT FROM TO        the widget or row at FROM, to be at TO
//   sqgui undo LAYOUT                the file as it was before sqgui's last edit
//
//   --app DIR      the app's folder: DIR/names.json and DIR/layouts/
//   --names FILE   names.json, and --layouts DIR the layout folder, one by one
//   --json         the answer as JSON
//
// Where the files are: --app, --names and --layouts, else square's answer for
// this workspace (`square --json info`: its app and layouts roles). sqgui
// never looks for them itself (the design: "it asks square").

#include <stdio.h> // popen, stderr, stdout: macros import std does not bring

import sq.data;
import sq.gui;
import sq.messaging;
import sqgui.reading;
import sqgui.app;
import sqgui.face;
import sqgui.edit;
import std;

namespace {

namespace fs = std::filesystem;
using sq::data::JsonValue;
using namespace sqgui;

int usage(std::FILE *out = stderr, int status = 2) {
  std::println(
      out,
      "usage: sqgui [--app DIR | --names FILE --layouts DIR] [--json] COMMAND\n"
      "  list                every layout: its kind, its nodes, whether it "
      "checks\n"
      "  show LAYOUT         its nodes as an outline, each with its path\n"
      "  get LAYOUT PATH     one value (sqgui get settings rows[1][0].bind)\n"
      "  check               every problem the build would stop for\n"
      "  map                 which layout pushes, replaces, opens or closes "
      "which\n"
      "  names               names.json's addresses, and the layouts using "
      "each\n"
      "  view                the terminal face: browse the layouts, drawn in "
      "cells\n"
      "  set LAYOUT PATH VALUE   change one value, or add a key (VALUE: JSON, "
      "else text)\n"
      "  unset LAYOUT PATH   remove a key\n"
      "  insert LAYOUT PATH VALUE   put a widget or row into a list: before\n"
      "                      PATH's index, or at the end (index = length)\n"
      "  remove LAYOUT PATH  take a widget or row out of its list\n"
      "  move LAYOUT FROM TO the widget or row at FROM, to be at TO\n"
      "  undo LAYOUT         put back the file as it was before sqgui's last "
      "edit\n"
      "  --force             save an edit the build would stop for\n"
      "where the files are: --app, --names, --layouts, else square's answer");
  return status;
}

// --- the commands -----------------------------------------------------------

int list(const App &app, bool as_json) {
  JsonValue::Array rows;
  for (const sqgui::Read &one : app.layouts) {
    const sqgui::Compiled *compiled = find_compiled(app, one.layout.name);
    const std::size_t problems = problems_in(app, one.layout.name);
    const std::size_t nodes = compiled != nullptr ? compiled->nodes.size()
                                                  : nodes_of(one.root).size();
    if (as_json) {
      rows.emplace_back(JsonValue::Object{
          {"name", JsonValue(one.layout.name)},
          {"kind", JsonValue(kind_word(one.layout.kind))},
          {"nodes", JsonValue(static_cast<std::int64_t>(nodes))},
          {"problems", JsonValue(static_cast<std::int64_t>(problems))},
      });
    } else {
      std::println("{:<20} {:<7} {:>4} nodes{}", one.layout.name,
                   kind_word(one.layout.kind), nodes,
                   problems == 0 ? std::string()
                                 : std::format("  {} problem{}", problems,
                                               problems == 1 ? "" : "s"));
    }
  }
  if (as_json) {
    std::println("{}", json_text(JsonValue(std::move(rows)), true));
  } else if (app.layouts.empty()) {
    std::println("no layouts{}",
                 app.files.layouts.empty() ? "" : " in " + app.files.layouts);
  }
  return 0;
}

int show(const App &app, std::string_view name, bool as_json) {
  const sqgui::Read *layout = find_layout(app, name);
  if (layout == nullptr) {
    std::println(stderr, "sqgui: no layout {} (sqgui list)", name);
    return 1;
  }
  const std::vector<Visit> visits = nodes_of(layout->root);
  if (as_json) {
    JsonValue::Array nodes;
    for (const Visit &v : visits) {
      nodes.emplace_back(JsonValue::Object{
          {"path", JsonValue(v.path)},
          {"depth", JsonValue(static_cast<std::int64_t>(v.depth))},
          {"node", *v.node}});
    }
    std::println(
        "{}", json_text(JsonValue(JsonValue::Object{
                            {"name", JsonValue(layout->layout.name)},
                            {"kind", JsonValue(kind_word(layout->layout.kind))},
                            {"nodes", JsonValue(std::move(nodes))}}),
                        true));
    return 0;
  }
  std::string head = std::format("{} ({}", layout->layout.name,
                                 kind_word(layout->layout.kind));
  if (const JsonValue *place = layout->root.find("place")) {
    head += ", " + text_of(place);
  }
  if (const JsonValue *modal = layout->root.find("modal");
      modal != nullptr && modal->boolean_if() != nullptr &&
      *modal->boolean_if()) {
    head += ", modal";
  }
  std::println("{})", head);
  if (const std::string note = text_of(layout->root.find("note"));
      !note.empty()) {
    std::println("  {}", note);
  }
  for (const Visit &v : visits) {
    std::println("{}{:<{}}  {}",
                 std::string(static_cast<std::size_t>(2 + 2 * v.depth), ' '),
                 v.path, std::max(0, 24 - 2 * v.depth), node_words(*v.node));
  }
  if (const std::size_t problems = problems_in(app, name); problems != 0) {
    std::println("  {} problem{}: sqgui check", problems,
                 problems == 1 ? "" : "s");
  }
  return 0;
}

int get(const App &app, std::string_view name, std::string_view path,
        bool as_json) {
  const sqgui::Read *layout = find_layout(app, name);
  if (layout == nullptr) {
    std::println(stderr, "sqgui: no layout {} (sqgui list)", name);
    return 1;
  }
  const auto value = at_path(layout->root, path);
  if (!value) {
    std::println(stderr, "sqgui: {}.json: {}", name, value.error());
    return 1;
  }
  const std::string *s = (*value)->string_if();
  std::println("{}",
               s != nullptr && !as_json
                   ? *s
                   : json_text(**value, (*value)->object_if() != nullptr));
  return 0;
}

int check(const App &app, bool as_json) {
  if (as_json) {
    JsonValue::Array problems;
    for (const sq::gui::FileProblem &p : app.problems) {
      problems.emplace_back(JsonValue::Object{{"where", JsonValue(p.where)},
                                              {"what", JsonValue(p.what)}});
    }
    std::println("{}",
                 json_text(JsonValue(JsonValue::Object{
                               {"names", JsonValue(static_cast<std::int64_t>(
                                             app.schema.names.size()))},
                               {"layouts", JsonValue(static_cast<std::int64_t>(
                                               app.layouts.size()))},
                               {"problems", JsonValue(std::move(problems))}}),
                           true));
  } else {
    for (const sq::gui::FileProblem &p : app.problems) {
      std::println("{}: {}", p.where, p.what);
    }
    if (app.problems.empty()) {
      std::println("no problems: {} name{}, {} layout{}",
                   app.schema.names.size(),
                   app.schema.names.size() == 1 ? "" : "s", app.layouts.size(),
                   app.layouts.size() == 1 ? "" : "s");
    } else {
      std::println("{} problem{}", app.problems.size(),
                   app.problems.size() == 1 ? "" : "s");
    }
  }
  return app.problems.empty() ? 0 : 1;
}

int map(const App &app, bool as_json) {
  const std::vector<Edge> edges = edges_of(app);
  if (as_json) {
    JsonValue::Array nodes;
    for (const sqgui::Read &one : app.layouts) {
      nodes.emplace_back(
          JsonValue::Object{{"name", JsonValue(one.layout.name)},
                            {"kind", JsonValue(kind_word(one.layout.kind))},
                            {"cpp", JsonValue(false)}});
    }
    for (const std::string &screen : app.schema.screens) {
      nodes.emplace_back(JsonValue::Object{{"name", JsonValue(screen)},
                                           {"kind", JsonValue("screen")},
                                           {"cpp", JsonValue(true)}});
    }
    JsonValue::Array links;
    for (const Edge &e : edges) {
      if (e.verb == "do" || e.verb == "set") {
        continue;
      }
      links.emplace_back(JsonValue::Object{
          {"from", JsonValue(e.from)},
          {"verb", JsonValue(e.verb)},
          {"to", e.target.empty() ? JsonValue(nullptr) : JsonValue(e.target)},
          {"path", JsonValue(e.path)}});
    }
    std::println("{}", json_text(JsonValue(JsonValue::Object{
                                     {"layouts", JsonValue(std::move(nodes))},
                                     {"links", JsonValue(std::move(links))}}),
                                 true));
    return 0;
  }
  for (const sqgui::Read &one : app.layouts) {
    std::println("{} ({})", one.layout.name, kind_word(one.layout.kind));
    bool any = false;
    for (const Edge &e : edges) {
      if (e.from != one.layout.name || e.verb == "do" || e.verb == "set") {
        continue;
      }
      any = true;
      if (navigates(e.verb)) {
        const bool cpp = find_layout(app, e.target) == nullptr &&
                         app.schema.has_screen(e.target);
        std::println("  {:<8} {}{}", e.verb, e.target, cpp ? "  (C++)" : "");
      } else {
        std::println("  {}", e.verb);
      }
    }
    if (!any) {
      std::println("  (goes nowhere)");
    }
  }
  for (const std::string &screen : app.schema.screens) {
    std::println("{} (screen, C++)", screen);
  }
  return 0;
}

int names(const App &app, bool as_json) {
  // Which layouts bind or tap each address.
  std::map<std::string, std::set<std::string>> users;
  for (const sqgui::Read &one : app.layouts) {
    for (const Visit &v : nodes_of(one.root)) {
      if (const std::string bind = text_of(v.node->find("bind"));
          !bind.empty()) {
        users[bind].insert(one.layout.name);
      }
      const std::string tap = text_of(v.node->find("tap"));
      if (tap.starts_with("do ") || tap.starts_with("set ")) {
        std::string rest = tap.substr(tap.find(' ') + 1);
        users[rest.substr(0, rest.find(' '))].insert(one.layout.name);
      }
    }
  }
  JsonValue::Array rows;
  for (const sq::gui::NameEntry &entry : app.schema.names) {
    std::string verbs;
    for (const auto &[verb, word] :
         {std::pair{sq::messaging::Verb::state, "state"},
          std::pair{sq::messaging::Verb::set, "set"},
          std::pair{sq::messaging::Verb::do_, "do"},
          std::pair{sq::messaging::Verb::event, "event"}}) {
      if (entry.verbs.has(verb)) {
        verbs += (verbs.empty() ? "" : ",") + std::string(word);
      }
    }
    std::string used;
    for (const std::string &u : users[entry.name]) {
      used += (used.empty() ? "" : ", ") + u;
    }
    const std::string type(entry.list.empty() ? type_word(entry.type) : "list");
    if (as_json) {
      JsonValue::Array by;
      for (const std::string &u : users[entry.name]) {
        by.emplace_back(u);
      }
      rows.emplace_back(
          JsonValue::Object{{"name", JsonValue(entry.name)},
                            {"type", JsonValue(type)},
                            {"verbs", JsonValue(verbs)},
                            {"uid", JsonValue(entry.uid)},
                            {"note", JsonValue(entry.note)},
                            {"used_by", JsonValue(std::move(by))}});
    } else {
      std::println("{:<24} {:<6} {:<12} {:<3}  {}", entry.name, type, verbs,
                   entry.uid ? "uid" : "", used.empty() ? "(unused)" : used);
    }
  }
  if (as_json) {
    std::println("{}", json_text(JsonValue(std::move(rows)), true));
  } else if (app.schema.names.empty()) {
    std::println("no names{}",
                 app.files.names.empty() ? " (no names.json)" : "");
  }
  return 0;
}

// --- the editing commands (phase 3) -----------------------------------------
//
// An edit rewrites only the value it touches (sqgui.edit); the rest of the
// file keeps its bytes. The result is checked as the build would check it:
// an edit that gives the layout a problem it did not have is not saved
// (--force saves it all the same). Each save keeps the file as it was, for
// `sqgui undo`, in $XDG_STATE_HOME/sqgui/undo (else ~/.local/state/sqgui/undo),
// never in the workspace.

[[nodiscard]] std::optional<std::string> read_text(const fs::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return std::nullopt;
  }
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

/// `text` written to `path` whole: to a file beside it, then renamed over it.
[[nodiscard]] bool write_text(const fs::path &path, std::string_view text) {
  const fs::path temporary = path.string() + ".sqgui-new";
  {
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out ||
        !out.write(text.data(), static_cast<std::streamsize>(text.size()))) {
      return false;
    }
  }
  std::error_code error;
  fs::rename(temporary, path, error);
  if (error) {
    fs::remove(temporary, error);
    return false;
  }
  return true;
}

/// Where a layout file's undo steps are kept.
[[nodiscard]] fs::path undo_folder(const fs::path &file) {
  const char *state = std::getenv("XDG_STATE_HOME");
  const char *home = std::getenv("HOME");
  fs::path base = state != nullptr && *state != '\0' ? fs::path(state)
                  : home != nullptr ? fs::path(home) / ".local/state"
                                    : fs::path(".");
  std::error_code error;
  std::string key = fs::absolute(file, error).lexically_normal().string();
  std::ranges::replace(key, '/', '%');
  return base / "sqgui" / "undo" / key;
}

/// The undo steps kept for a file, oldest first (each NNNN.before and
/// NNNN.after).
[[nodiscard]] std::vector<int> undo_steps(const fs::path &folder) {
  std::vector<int> steps;
  std::error_code error;
  for (const fs::directory_entry &entry :
       fs::directory_iterator(folder, error)) {
    if (entry.path().extension() == ".before") {
      int n = 0;
      const std::string stem = entry.path().stem().string();
      if (std::from_chars(stem.data(), stem.data() + stem.size(), n).ec ==
          std::errc()) {
        steps.push_back(n);
      }
    }
  }
  std::ranges::sort(steps);
  return steps;
}

constexpr std::size_t undo_kept = 50;

void keep_undo(const fs::path &file, std::string_view before,
               std::string_view after) {
  const fs::path folder = undo_folder(file);
  std::error_code error;
  fs::create_directories(folder, error);
  std::vector<int> steps = undo_steps(folder);
  const int next = steps.empty() ? 1 : steps.back() + 1;
  const auto name = [&](int n, std::string_view ext) {
    return folder / std::format("{:04}.{}", n, ext);
  };
  if (!write_text(name(next, "before"), before) ||
      !write_text(name(next, "after"), after)) {
    std::println(stderr,
                 "sqgui: saved, but could not keep the step for undo in {}",
                 folder.string());
    return;
  }
  while (steps.size() + 1 > undo_kept) {
    fs::remove(name(steps.front(), "before"), error);
    fs::remove(name(steps.front(), "after"), error);
    steps.erase(steps.begin());
  }
}

/// VALUE as JSON: as typed when it is JSON (11, true, "11", {...}), else a
/// string.
[[nodiscard]] std::string value_json(std::string_view typed) {
  if (sq::data::parse_json(typed)) {
    return std::string(typed);
  }
  return sqgui::json_string(typed);
}

/// The problems a layout has, as "where: what" lines.
[[nodiscard]] std::vector<std::string> problems_of(const App &app,
                                                   std::string_view name) {
  const std::string file = std::string(name) + ".json";
  std::vector<std::string> out;
  for (const sq::gui::FileProblem &p : app.problems) {
    if (p.where == file || p.where.starts_with(file + ":")) {
      out.push_back(std::format("{}: {}", p.where, p.what));
    }
  }
  return out;
}

/// Write `after` over the layout's file, check it, and keep it or put back
/// `before`. `what` says what was done, for the words and --json.
int save_edit(const App &app, std::string_view name, const fs::path &file,
              const std::string &before, const std::string &after,
              std::string_view what, bool force, bool as_json,
              bool keep_step = true) {
  if (after == before) {
    std::println("{}", as_json ? R"({"changed": false})" : "no change");
    return 0;
  }
  const std::vector<std::string> had = problems_of(app, name);
  if (!write_text(file, after)) {
    std::println(stderr, "sqgui: {} could not be written", file.string());
    return 1;
  }
  const App now = read_app(app.files);
  const std::vector<std::string> has = problems_of(now, name);
  std::vector<std::string> added;
  for (const std::string &p : has) {
    if (std::ranges::find(had, p) == had.end()) {
      added.push_back(p);
    }
  }
  if (!added.empty() && !force) {
    if (!write_text(file, before)) {
      std::println(stderr, "sqgui: {} could not be put back; it holds the edit",
                   file.string());
    }
    for (const std::string &p : added) {
      std::println(stderr, "{}", p);
    }
    std::println(stderr,
                 "sqgui: not saved: the build would stop for {} (--force saves "
                 "it anyway)",
                 added.size() == 1 ? "this" : "these");
    return 1;
  }
  if (keep_step) {
    keep_undo(file, before, after);
  }
  if (as_json) {
    JsonValue::Array problems;
    for (const std::string &p : has) {
      problems.emplace_back(p);
    }
    std::println("{}",
                 json_text(JsonValue(JsonValue::Object{
                               {"changed", JsonValue(true)},
                               {"layout", JsonValue(std::string(name))},
                               {"did", JsonValue(std::string(what))},
                               {"problems", JsonValue(std::move(problems))}}),
                           true));
  } else {
    std::println("{}.json: {}", name, what);
    for (const std::string &p : added) {
      std::println("{}", p);
    }
  }
  return 0;
}

/// The layout's file, or why not.
[[nodiscard]] std::optional<fs::path> layout_file(const App &app,
                                                  std::string_view name) {
  if (find_layout(app, name) == nullptr || app.files.layouts.empty()) {
    std::println(stderr, "sqgui: no layout {} (sqgui list)", name);
    return std::nullopt;
  }
  return fs::path(app.files.layouts) / (std::string(name) + ".json");
}

int set(const App &app, std::string_view name, std::string_view path,
        std::string_view typed, bool force, bool as_json) {
  const auto file = layout_file(app, name);
  const auto steps = sqgui::parse_path(path);
  if (!file) {
    return 1;
  }
  if (!steps) {
    std::println(stderr, "sqgui: {} is not a path (rows[1][0].text)", path);
    return 2;
  }
  const auto before = read_text(*file);
  if (!before) {
    std::println(stderr, "sqgui: {} could not be read", file->string());
    return 1;
  }
  const std::string value = value_json(typed);
  const auto after = sqgui::set_value(*before, *steps, value);
  if (!after) {
    std::println(stderr, "sqgui: {}.json: {}", name, after.error());
    return 1;
  }
  return save_edit(app, name, *file, *before, *after,
                   std::format("{} = {}", path, value), force, as_json);
}

int unset(const App &app, std::string_view name, std::string_view path,
          bool force, bool as_json) {
  const auto file = layout_file(app, name);
  const auto steps = sqgui::parse_path(path);
  if (!file) {
    return 1;
  }
  if (!steps) {
    std::println(stderr, "sqgui: {} is not a path (rows[1][0].style)", path);
    return 2;
  }
  const auto before = read_text(*file);
  if (!before) {
    std::println(stderr, "sqgui: {} could not be read", file->string());
    return 1;
  }
  const auto after = sqgui::unset_value(*before, *steps);
  if (!after) {
    std::println(stderr, "sqgui: {}.json: {}", name, after.error());
    return 1;
  }
  return save_edit(app, name, *file, *before, *after,
                   std::format("{} removed", path), force, as_json);
}

/// One edit to a layout's file: `change` turns its text into the new text.
int edit_layout(const App &app, std::string_view name, std::string_view what,
                bool force, bool as_json,
                const std::function<std::expected<std::string, std::string>(
                    std::string_view)> &change) {
  const auto file = layout_file(app, name);
  if (!file) {
    return 1;
  }
  const auto before = read_text(*file);
  if (!before) {
    std::println(stderr, "sqgui: {} could not be read", file->string());
    return 1;
  }
  const auto after = change(*before);
  if (!after) {
    std::println(stderr, "sqgui: {}.json: {}", name, after.error());
    return 1;
  }
  return save_edit(app, name, *file, *before, *after, what, force, as_json);
}

/// PATH as steps, or nothing (said).
[[nodiscard]] std::optional<std::vector<sqgui::Step>>
path_of(std::string_view path) {
  auto steps = sqgui::parse_path(path);
  if (!steps) {
    std::println(stderr, "sqgui: {} is not a path (rows[1][0])", path);
  }
  return steps;
}

int insert(const App &app, std::string_view name, std::string_view path,
           std::string_view typed, bool force, bool as_json) {
  const auto steps = path_of(path);
  if (!steps) {
    return 2;
  }
  const std::string value = value_json(typed);
  return edit_layout(app, name, std::format("{} inserted", path), force,
                     as_json, [&](std::string_view text) {
                       return sqgui::insert_value(text, *steps, value);
                     });
}

int remove(const App &app, std::string_view name, std::string_view path,
           bool force, bool as_json) {
  const auto steps = path_of(path);
  if (!steps) {
    return 2;
  }
  return edit_layout(
      app, name, std::format("{} removed", path), force, as_json,
      [&](std::string_view text) { return sqgui::remove_value(text, *steps); });
}

int move(const App &app, std::string_view name, std::string_view from,
         std::string_view to, bool force, bool as_json) {
  const auto a = path_of(from);
  const auto b = path_of(to);
  if (!a || !b) {
    return 2;
  }
  return edit_layout(
      app, name, std::format("{} moved to {}", from, to), force, as_json,
      [&](std::string_view text) { return sqgui::move_value(text, *a, *b); });
}

int undo(const App &app, std::string_view name, bool as_json) {
  const auto file = layout_file(app, name);
  if (!file) {
    return 1;
  }
  const fs::path folder = undo_folder(*file);
  const std::vector<int> steps = undo_steps(folder);
  if (steps.empty()) {
    std::println(stderr, "sqgui: nothing to undo in {}.json", name);
    return 1;
  }
  const fs::path before_file =
      folder / std::format("{:04}.before", steps.back());
  const fs::path after_file = folder / std::format("{:04}.after", steps.back());
  const auto before = read_text(before_file);
  const auto after = read_text(after_file);
  const auto now = read_text(*file);
  if (!before || !after || !now) {
    std::println(stderr, "sqgui: the undo step for {}.json could not be read",
                 name);
    return 1;
  }
  if (*now != *after) {
    std::println(stderr,
                 "sqgui: {}.json changed since sqgui's last edit: not undone "
                 "(its undo steps are in {})",
                 name, folder.string());
    return 1;
  }
  if (!write_text(*file, *before)) {
    std::println(stderr, "sqgui: {} could not be written", file->string());
    return 1;
  }
  std::error_code error;
  fs::remove(before_file, error);
  fs::remove(after_file, error);
  std::println("{}",
               as_json
                   ? json_text(JsonValue(JsonValue::Object{
                                   {"undone", JsonValue(true)},
                                   {"layout", JsonValue(std::string(name))},
                                   {"left", JsonValue(static_cast<std::int64_t>(
                                                steps.size() - 1))}}),
                               true)
                   : std::format("{}.json: undone ({} more to undo)", name,
                                 steps.size() - 1));
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  std::vector<std::string_view> args(argv + 1, argv + argc);
  std::optional<std::string> app_dir, names_file, layouts_dir;
  bool as_json = false;
  bool force = false;
  std::vector<std::string_view> words;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string_view a = args[i];
    if (a == "--help" || a == "-h" || a == "help") {
      return usage(stdout, 0);
    }
    if (a == "--json") {
      as_json = true;
    } else if (a == "--force") {
      force = true;
    } else if (a == "--app" || a == "--names" || a == "--layouts") {
      if (i + 1 >= args.size()) {
        return usage();
      }
      const std::string value(args[++i]);
      (a == "--app"     ? app_dir
       : a == "--names" ? names_file
                        : layouts_dir) = value;
    } else if (a.starts_with("--")) {
      return usage();
    } else {
      words.push_back(a);
    }
  }
  if (words.empty()) {
    return usage();
  }
  const std::string_view command = words[0];
  const std::size_t want =
      command == "show" || command == "undo"                          ? 2
      : command == "get" || command == "unset" || command == "remove" ? 3
      : command == "set" || command == "insert" || command == "move"  ? 4
                                                                      : 1;
  if (command != "list" && command != "show" && command != "get" &&
      command != "check" && command != "map" && command != "names" &&
      command != "view" && command != "set" && command != "unset" &&
      command != "undo" && command != "insert" && command != "remove" &&
      command != "move") {
    std::println(stderr, "sqgui: no command {} (sqgui --help)", command);
    return 2;
  }
  if (words.size() != want) {
    return usage();
  }

  Files files;
  if (app_dir || names_file || layouts_dir) {
    std::error_code error;
    if (app_dir) {
      const fs::path dir(*app_dir);
      if (fs::is_regular_file(dir / "names.json", error)) {
        files.names = (dir / "names.json").string();
      }
      if (fs::is_directory(dir / "layouts", error)) {
        files.layouts = (dir / "layouts").string();
      }
    }
    if (names_file) {
      files.names = *names_file;
    }
    if (layouts_dir) {
      files.layouts = *layouts_dir;
    }
  } else {
    auto asked = ask_square();
    if (!asked) {
      std::println(stderr, "sqgui: {}", asked.error());
      return 1;
    }
    files = std::move(*asked);
  }
  const App app = read_app(files);
  if (app.names_unreadable) {
    std::println(stderr, "sqgui: {} could not be read", files.names);
    return 1;
  }
  if (command == "list") {
    return list(app, as_json);
  }
  if (command == "show") {
    return show(app, words[1], as_json);
  }
  if (command == "get") {
    return get(app, words[1], words[2], as_json);
  }
  if (command == "check") {
    return check(app, as_json);
  }
  if (command == "map") {
    return map(app, as_json);
  }
  if (command == "view") {
    return run_face(app);
  }
  if (command == "set") {
    return set(app, words[1], words[2], words[3], force, as_json);
  }
  if (command == "unset") {
    return unset(app, words[1], words[2], force, as_json);
  }
  if (command == "insert") {
    return insert(app, words[1], words[2], words[3], force, as_json);
  }
  if (command == "remove") {
    return remove(app, words[1], words[2], force, as_json);
  }
  if (command == "move") {
    return move(app, words[1], words[2], words[3], force, as_json);
  }
  if (command == "undo") {
    return undo(app, words[1], as_json);
  }
  return names(app, as_json);
}
