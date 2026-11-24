// sqgui.edit: a layout file changed in place, byte for byte everywhere but
// the value an edit touches (the design: "an edit rewrites only what it
// touched; everything else keeps its text and order").
//
// sq.data parses JSON into values and forgets where they were; an edit
// needs where. So this reads the file's text as JSON only far enough to find
// the bytes of one value by its path (rows[1][0].text, the path sqgui show
// prints), and an edit replaces exactly those bytes, adds a key just after an
// object's last member, or removes one member and its comma; in a list (a
// table's rows, a row's widgets), puts an element in, takes one out, or moves
// one, with the separator the list already uses (", ", or a newline and an
// indent). What it writes is checked by the caller with the build's checker
// (sqgui.reading).

export module sqgui.edit;

import std;

export namespace sqgui {

/// Where a value is in the text: [begin, end).
struct Span {
  std::size_t begin = 0;
  std::size_t end = 0;
};

/// One step of a path: a key or an index.
struct Step {
  std::string key;
  std::optional<std::size_t> index;
};

/// "rows[1][0].text" as steps; nothing when it is not a path.
[[nodiscard]] std::optional<std::vector<Step>>
parse_path(std::string_view path);

/// The value at `path` in `text`; why not when it is not there.
[[nodiscard]] std::expected<Span, std::string>
find_value(std::string_view text, const std::vector<Step> &path);

/// `text` with the value at `path` set to `value` (JSON text): replaced
/// where it is, or added as the object's last member where the key is new.
[[nodiscard]] std::expected<std::string, std::string>
set_value(std::string_view text, const std::vector<Step> &path,
          std::string_view value);

/// `text` without the member at `path` (an object's key), its comma too.
[[nodiscard]] std::expected<std::string, std::string>
unset_value(std::string_view text, const std::vector<Step> &path);

/// `text` with `value` (JSON text) put into the list at `path`, which ends
/// with an index: before the element there, or after the last when the index
/// is the list's length. The list's own separator is used (", " or a newline
/// and its indent), so a row added to a table looks like its neighbours.
[[nodiscard]] std::expected<std::string, std::string>
insert_value(std::string_view text, const std::vector<Step> &path,
             std::string_view value);

/// `text` without the list's element at `path`, and one separator.
[[nodiscard]] std::expected<std::string, std::string>
remove_value(std::string_view text, const std::vector<Step> &path);

/// `text` with the element at `from` moved so that it is at `to` afterwards
/// (`to` read as the file is once the element is taken out): rows[0][2] to
/// rows[0][0] puts it first; rows[0][2] to rows[3][1] into another row.
[[nodiscard]] std::expected<std::string, std::string>
move_value(std::string_view text, const std::vector<Step> &from,
           const std::vector<Step> &to);

/// A JSON string literal of `s`, escaped.
[[nodiscard]] std::string json_string(std::string_view s);

} // namespace sqgui

namespace sqgui {

namespace {

[[nodiscard]] bool space(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

[[nodiscard]] std::size_t skip(std::string_view t, std::size_t i) noexcept {
  while (i < t.size() && space(t[i])) {
    ++i;
  }
  return i;
}

/// The end of the string starting at `i` (its opening quote), past its closing
/// one.
[[nodiscard]] std::optional<std::size_t> string_end(std::string_view t,
                                                    std::size_t i) noexcept {
  for (++i; i < t.size(); ++i) {
    if (t[i] == '\\') {
      ++i;
    } else if (t[i] == '"') {
      return i + 1;
    }
  }
  return std::nullopt;
}

/// The end of the value starting at `i`.
[[nodiscard]] std::optional<std::size_t> value_end(std::string_view t,
                                                   std::size_t i) {
  if (i >= t.size()) {
    return std::nullopt;
  }
  const char c = t[i];
  if (c == '"') {
    return string_end(t, i);
  }
  if (c == '{' || c == '[') {
    const char close = c == '{' ? '}' : ']';
    i = skip(t, i + 1);
    if (i < t.size() && t[i] == close) {
      return i + 1;
    }
    for (;;) {
      if (c == '{') {
        const auto key = string_end(t, i);
        if (!key) {
          return std::nullopt;
        }
        i = skip(t, *key);
        if (i >= t.size() || t[i] != ':') {
          return std::nullopt;
        }
        i = skip(t, i + 1);
      }
      const auto end = value_end(t, i);
      if (!end) {
        return std::nullopt;
      }
      i = skip(t, *end);
      if (i < t.size() && t[i] == ',') {
        i = skip(t, i + 1);
        continue;
      }
      if (i < t.size() && t[i] == close) {
        return i + 1;
      }
      return std::nullopt;
    }
  }
  // A number, true, false or null: to the next delimiter.
  std::size_t end = i;
  while (end < t.size() && !space(t[end]) && t[end] != ',' && t[end] != '}' &&
         t[end] != ']') {
    ++end;
  }
  return end > i ? std::optional(end) : std::nullopt;
}

/// The text of a JSON string's contents (no escapes expected in keys).
[[nodiscard]] std::string_view key_text(std::string_view t, std::size_t begin,
                                        std::size_t end) noexcept {
  return t.substr(begin + 1, end - begin - 2);
}

/// An object's members: each key's span and value's span, from the object at
/// `i`.
struct Member {
  std::string key;
  Span key_span;
  Span value;
};

[[nodiscard]] std::optional<std::vector<Member>> members_of(std::string_view t,
                                                            std::size_t i) {
  std::vector<Member> out;
  i = skip(t, i + 1);
  if (i < t.size() && t[i] == '}') {
    return out;
  }
  for (;;) {
    const auto key = string_end(t, i);
    if (!key) {
      return std::nullopt;
    }
    Member m;
    m.key = std::string(key_text(t, i, *key));
    m.key_span = {i, *key};
    i = skip(t, *key);
    if (i >= t.size() || t[i] != ':') {
      return std::nullopt;
    }
    i = skip(t, i + 1);
    const auto end = value_end(t, i);
    if (!end) {
      return std::nullopt;
    }
    m.value = {i, *end};
    out.push_back(std::move(m));
    i = skip(t, *end);
    if (i < t.size() && t[i] == ',') {
      i = skip(t, i + 1);
      continue;
    }
    return out;
  }
}

[[nodiscard]] std::optional<std::vector<Span>> elements_of(std::string_view t,
                                                           std::size_t i) {
  std::vector<Span> out;
  i = skip(t, i + 1);
  if (i < t.size() && t[i] == ']') {
    return out;
  }
  for (;;) {
    const auto end = value_end(t, i);
    if (!end) {
      return std::nullopt;
    }
    out.push_back({i, *end});
    i = skip(t, *end);
    if (i < t.size() && t[i] == ',') {
      i = skip(t, i + 1);
      continue;
    }
    return out;
  }
}

[[nodiscard]] std::string where(const std::vector<Step> &path,
                                std::size_t upto) {
  std::string out;
  for (std::size_t i = 0; i < upto && i < path.size(); ++i) {
    if (path[i].index) {
      out += std::format("[{}]", *path[i].index);
    } else {
      out += (out.empty() ? "" : ".") + path[i].key;
    }
  }
  return out.empty() ? std::string("the layout") : out;
}

/// The object holding the last step's key (or the array holding its index):
/// its span, and the last step.
[[nodiscard]] std::expected<Span, std::string>
parent_of(std::string_view text, const std::vector<Step> &path) {
  std::vector<Step> parent(path.begin(), path.end() - 1);
  return find_value(text, parent);
}

} // namespace

std::optional<std::vector<Step>> parse_path(std::string_view path) {
  std::vector<Step> out;
  std::size_t i = 0;
  while (i < path.size()) {
    if (path[i] == '.') {
      ++i;
      continue;
    }
    if (path[i] == '[') {
      const std::size_t close = path.find(']', i);
      if (close == std::string_view::npos) {
        return std::nullopt;
      }
      std::size_t n = 0;
      const auto digits = path.substr(i + 1, close - i - 1);
      const auto [end, error] =
          std::from_chars(digits.data(), digits.data() + digits.size(), n);
      if (error != std::errc() || end != digits.data() + digits.size()) {
        return std::nullopt;
      }
      out.push_back({{}, n});
      i = close + 1;
      continue;
    }
    std::size_t end = i;
    while (end < path.size() && path[end] != '.' && path[end] != '[') {
      ++end;
    }
    out.push_back({std::string(path.substr(i, end - i)), std::nullopt});
    i = end;
  }
  return out;
}

std::expected<Span, std::string> find_value(std::string_view text,
                                            const std::vector<Step> &path) {
  std::size_t at = skip(text, 0);
  const auto whole = value_end(text, at);
  if (!whole) {
    return std::unexpected("the file is not JSON");
  }
  Span here{at, *whole};
  for (std::size_t s = 0; s < path.size(); ++s) {
    const Step &step = path[s];
    if (step.index) {
      if (text[here.begin] != '[') {
        return std::unexpected(std::format("{}: not a list", where(path, s)));
      }
      const auto elements = elements_of(text, here.begin);
      if (!elements || *step.index >= elements->size()) {
        return std::unexpected(
            std::format("{}: no [{}] there", where(path, s), *step.index));
      }
      here = (*elements)[*step.index];
    } else {
      if (text[here.begin] != '{') {
        return std::unexpected(
            std::format("{}: not an object", where(path, s)));
      }
      const auto members = members_of(text, here.begin);
      if (!members) {
        return std::unexpected("the file is not JSON");
      }
      const auto it = std::ranges::find(*members, step.key, &Member::key);
      if (it == members->end()) {
        return std::unexpected(
            std::format("{}: no {} there", where(path, s), step.key));
      }
      here = it->value;
    }
  }
  return here;
}

std::expected<std::string, std::string> set_value(std::string_view text,
                                                  const std::vector<Step> &path,
                                                  std::string_view value) {
  if (path.empty()) {
    return std::unexpected("a path to a value, not the whole layout");
  }
  if (const auto found = find_value(text, path)) {
    std::string out(text.substr(0, found->begin));
    out += value;
    out += text.substr(found->end);
    return out;
  }
  // A new key: added after the object's last member, on the same line.
  const Step &last = path.back();
  if (last.index) {
    return find_value(text, path).transform([](Span) { return std::string(); });
  }
  const auto parent = parent_of(text, path);
  if (!parent) {
    return std::unexpected(parent.error());
  }
  if (text[parent->begin] != '{') {
    return std::unexpected(
        std::format("{}: not an object", where(path, path.size() - 1)));
  }
  const auto members = members_of(text, parent->begin);
  if (!members) {
    return std::unexpected("the file is not JSON");
  }
  const std::string member = json_string(last.key) + ": " + std::string(value);
  std::string out;
  if (members->empty()) {
    out = std::string(text.substr(0, parent->begin + 1)) + " " + member + " " +
          std::string(text.substr(parent->end - 1));
  } else {
    const std::size_t after = members->back().value.end;
    out = std::string(text.substr(0, after)) + ", " + member +
          std::string(text.substr(after));
  }
  return out;
}

std::expected<std::string, std::string>
unset_value(std::string_view text, const std::vector<Step> &path) {
  if (path.empty() || path.back().index) {
    return std::unexpected(
        "a key to remove (rows[1][0].style), not a list's element");
  }
  const auto parent = parent_of(text, path);
  if (!parent) {
    return std::unexpected(parent.error());
  }
  const auto members = members_of(text, parent->begin);
  if (!members) {
    return std::unexpected("the file is not JSON");
  }
  const auto it = std::ranges::find(*members, path.back().key, &Member::key);
  if (it == members->end()) {
    return std::unexpected(std::format(
        "{}: no {} there", where(path, path.size() - 1), path.back().key));
  }
  std::size_t from = 0, to = 0;
  if (it != members->begin()) {
    // From the previous value's end (its comma goes with this member).
    from = std::prev(it)->value.end;
    to = it->value.end;
  } else if (std::next(it) != members->end()) {
    // The first of several: up to the next key.
    from = it->key_span.begin;
    to = std::next(it)->key_span.begin;
  } else {
    // The only member: the object becomes empty.
    from = it->key_span.begin;
    to = it->value.end;
  }
  return std::string(text.substr(0, from)) + std::string(text.substr(to));
}

namespace {

/// The list holding the element `path` ends with: its span and elements.
struct List {
  Span span;
  std::vector<Span> elements;
};

[[nodiscard]] std::expected<List, std::string>
list_of(std::string_view text, const std::vector<Step> &path) {
  if (path.empty() || !path.back().index) {
    return std::unexpected(
        std::format("{}: a list's element (rows[1][0]), not a key",
                    where(path, path.size())));
  }
  const auto parent = parent_of(text, path);
  if (!parent) {
    return std::unexpected(parent.error());
  }
  if (text[parent->begin] != '[') {
    return std::unexpected(
        std::format("{}: not a list", where(path, path.size() - 1)));
  }
  auto elements = elements_of(text, parent->begin);
  if (!elements) {
    return std::unexpected("the file is not JSON");
  }
  return List{*parent, std::move(*elements)};
}

/// What stands between a list's elements: its first gap, else `otherwise`.
[[nodiscard]] std::string separator_of(std::string_view text, const List &list,
                                       std::string_view otherwise = ", ") {
  if (list.elements.size() >= 2) {
    return std::string(text.substr(
        list.elements[0].end, list.elements[1].begin - list.elements[0].end));
  }
  return std::string(otherwise);
}

/// insert_value, with `otherwise` the separator for a list of one or none.
std::expected<std::string, std::string>
insert_with(std::string_view text, const std::vector<Step> &path,
            std::string_view value, std::string_view otherwise) {
  const auto list = list_of(text, path);
  if (!list) {
    return std::unexpected(list.error());
  }
  const std::size_t at = *path.back().index;
  const std::size_t n = list->elements.size();
  if (at > n) {
    return std::unexpected(
        std::format("{}: the list has {} (insert at 0 to {})",
                    where(path, path.size() - 1), n, n));
  }
  const std::string sep = separator_of(text, *list, otherwise);
  std::string out;
  if (n == 0) {
    const std::size_t inside = list->span.begin + 1;
    out = std::string(text.substr(0, inside)) + std::string(value) +
          std::string(text.substr(inside));
  } else if (at == n) {
    const std::size_t after = list->elements.back().end;
    out = std::string(text.substr(0, after)) + sep + std::string(value) +
          std::string(text.substr(after));
  } else {
    const std::size_t before = list->elements[at].begin;
    out = std::string(text.substr(0, before)) + std::string(value) + sep +
          std::string(text.substr(before));
  }
  return out;
}

} // namespace

std::expected<std::string, std::string>
insert_value(std::string_view text, const std::vector<Step> &path,
             std::string_view value) {
  return insert_with(text, path, value, ", ");
}

std::expected<std::string, std::string>
remove_value(std::string_view text, const std::vector<Step> &path) {
  const auto list = list_of(text, path);
  if (!list) {
    return std::unexpected(list.error());
  }
  const std::size_t at = *path.back().index;
  const auto &e = list->elements;
  if (at >= e.size()) {
    return std::unexpected(
        std::format("{}: no [{}] there", where(path, path.size() - 1), at));
  }
  std::size_t from = 0, to = 0;
  if (e.size() == 1) {
    from = e[0].begin;
    to = e[0].end;
  } else if (at > 0) {
    from = e[at - 1].end; // its separator goes with it
    to = e[at].end;
  } else {
    from = e[0].begin;
    to = e[1].begin;
  }
  return std::string(text.substr(0, from)) + std::string(text.substr(to));
}

std::expected<std::string, std::string>
move_value(std::string_view text, const std::vector<Step> &from,
           const std::vector<Step> &to) {
  if (to.size() >= from.size() &&
      std::equal(from.begin(), from.end(), to.begin(),
                 [](const Step &a, const Step &b) {
                   return a.key == b.key && a.index == b.index;
                 })) {
    return std::unexpected("an element cannot move into itself");
  }
  const auto found = find_value(text, from);
  if (!found) {
    return std::unexpected(found.error());
  }
  const std::string moving(
      text.substr(found->begin, found->end - found->begin));
  // The separator of the list it leaves, for a list left with one element
  // (a two-widget row on two lines, reordered).
  const auto source = list_of(text, from);
  const std::string sep =
      source ? separator_of(text, *source) : std::string(", ");
  const auto without = remove_value(text, from);
  if (!without) {
    return std::unexpected(without.error());
  }
  return insert_with(*without, to, moving, sep);
}

std::string json_string(std::string_view s) {
  std::string out = "\"";
  for (const char c : s) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) {
        out += std::format("\\u{:04x}", static_cast<unsigned>(c));
      } else {
        out += c;
      }
    }
  }
  return out + "\"";
}

} // namespace sqgui
