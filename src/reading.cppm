// sqgui.reading: an app's names.json and layout files, read and checked as
// the build does (sqgui-gen) and as the editor shows them (sqgui): one
// reader, so the editor's problems are the build's, word for word.
//
// Each file is read with squared's layout_file (read_json, read_names,
// read_layout_kind, read_layout), into a LayoutWriter, and checked; a name
// that cannot become a C++ identifier is a problem too, since the build
// writes one for each.

export module sqgui.reading;

import sq.gui;
import sq.messaging;
import sq.data;
import std;

export namespace sqgui
{

namespace fs = std::filesystem;
using sq::gui::FileProblems;
using sq::gui::Layout;
using sq::gui::LayoutKind;
using sq::gui::LayoutNode;

/// One layout, read and written: its name and a copy of its table.
struct Compiled
{
        std::string name;
        std::string identifier;
        LayoutKind kind = LayoutKind::screen;
        sq::gui::PaneShape pane{};
        std::vector<LayoutNode> nodes;
        std::string text;
        std::vector<float> numbers;
};

void add_problem( FileProblems &problems, std::string where, std::string what )
{
    sq::gui::FileProblem problem;
    problem.where = std::move( where );
    problem.what = std::move( what );
    problems.push_back( std::move( problem ) );
}

/// "s" for a count other than one.
[[nodiscard]] std::string_view plural( std::size_t count ) noexcept
{
    if( count == 1 ) {
        return "";
    }
    return "s";
}

[[nodiscard]] std::optional<std::string> read_file( const fs::path &path )
{
    std::ifstream in( path, std::ios::binary );
    if( !in ) {
        return std::nullopt;
    }
    return std::string( std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() );
}

/// C++'s keywords, and the names sqgen's own files use.
constexpr std::string_view keywords[] = {
    "alignas",
    "alignof",
    "and",
    "and_eq",
    "asm",
    "auto",
    "bitand",
    "bitor",
    "bool",
    "break",
    "case",
    "catch",
    "char",
    "char8_t",
    "char16_t",
    "char32_t",
    "class",
    "compl",
    "concept",
    "const",
    "consteval",
    "constexpr",
    "constinit",
    "const_cast",
    "continue",
    "co_await",
    "co_return",
    "co_yield",
    "decltype",
    "default",
    "delete",
    "do",
    "double",
    "dynamic_cast",
    "else",
    "enum",
    "explicit",
    "export",
    "extern",
    "false",
    "float",
    "for",
    "friend",
    "goto",
    "if",
    "inline",
    "int",
    "long",
    "mutable",
    "namespace",
    "new",
    "noexcept",
    "not",
    "not_eq",
    "nullptr",
    "operator",
    "or",
    "or_eq",
    "private",
    "protected",
    "public",
    "register",
    "reinterpret_cast",
    "requires",
    "return",
    "short",
    "signed",
    "sizeof",
    "static",
    "static_assert",
    "static_cast",
    "struct",
    "switch",
    "template",
    "this",
    "thread_local",
    "throw",
    "true",
    "try",
    "typedef",
    "typeid",
    "typename",
    "union",
    "unsigned",
    "using",
    "virtual",
    "void",
    "volatile",
    "wchar_t",
    "while",
    "xor",
    "xor_eq",
    "all",
    "find",
    "names",
    "layouts",
    "sq",
};

/// A dotted name as a C++ identifier (a dot becomes an underscore); empty
/// when it cannot be one, and `why` then says why.
[[nodiscard]] std::string identifier_of( std::string_view name, std::string &why )
{
    std::string out( name );
    std::replace( out.begin(), out.end(), '.', '_' );
    if( out.empty() || ( out.front() >= '0' && out.front() <= '9' ) ) {
        why = "starts with a digit: not a C++ name";
        return {};
    }
    if( out.find( "__" ) != std::string::npos || out.front() == '_' ) {
        why = "becomes " + out + ", a name C++ keeps for itself (__, or _ first)";
        return {};
    }
    if( std::find( std::begin( keywords ), std::end( keywords ), out ) != std::end( keywords ) ) {
        why = "becomes " + out + ", a C++ keyword or a name sqgen uses";
        return {};
    }
    return out;
}

/// The keys' identifiers, one per address; a problem for each that is not a
/// C++ name, or that two names share.
[[nodiscard]] std::vector<std::string> key_identifiers( const sq::gui::NameSchema &schema,
                                                        const std::string &file,
                                                        FileProblems &problems )
{
    std::vector<std::string> out;
    for( const sq::gui::NameEntry &entry : schema.names ) {
        const std::string where = file + ": names[\"" + entry.name + "\"]";
        std::string why;
        const std::string id = identifier_of( entry.name, why );
        if( id.empty() ) {
            add_problem( problems, where, why );
            out.emplace_back();
            continue;
        }
        for( std::size_t i = 0; i < out.size(); ++i ) {
            if( out[i] == id ) {
                const std::string shared = "becomes names::" + id + ", as " + schema.names[i].name;
                add_problem( problems, where, shared + " does: rename one" );
            }
        }
        out.push_back( id );
    }
    return out;
}

/// A layout file, read as far as its kind.
struct Read
{
        Compiled layout;
        sq::data::JsonValue root;
};

/// The layout files in `directory`, sorted.
[[nodiscard]] std::vector<fs::path> layout_files( const fs::path &directory )
{
    std::vector<fs::path> files;
    std::error_code error;
    for( const fs::directory_entry &entry : fs::directory_iterator( directory, error ) ) {
        if( entry.is_regular_file() && entry.path().extension() == ".json" ) {
            files.push_back( entry.path() );
        }
    }
    std::sort( files.begin(), files.end() );
    return files;
}

/// Read one layout file as far as its kind; nothing when it cannot be.
[[nodiscard]] std::optional<Read> read_kind( const fs::path &path, const std::vector<Read> &earlier,
                                             FileProblems &problems )
{
    const std::string file = path.filename().string();
    Compiled layout;
    layout.name = path.stem().string();
    std::string why;
    const std::string id = identifier_of( layout.name, why );
    if( !sq::messaging::valid_name( layout.name ) || id.empty() ) {
        std::string what = "a layout's name is its file's: lowercase words, digits and _";
        if( !why.empty() ) {
            what += "; it " + why;
        }
        add_problem( problems, file, what );
        return std::nullopt;
    }
    layout.identifier = id;
    for( const Read &other : earlier ) {
        if( other.layout.identifier == layout.identifier ) {
            const std::string shared = "becomes layouts::" + layout.identifier;
            add_problem( problems, file, shared + ", as " + other.layout.name + ".json does" );
        }
    }
    const std::optional<std::string> text = read_file( path );
    if( !text ) {
        add_problem( problems, file, "could not be read" );
        return std::nullopt;
    }
    sq::data::JsonValue root;
    if( !sq::gui::read_json( file, *text, root, problems ) ) {
        return std::nullopt;
    }
    const std::size_t before = problems.size();
    layout.kind = sq::gui::read_layout_kind( file, root, problems );
    if( problems.size() != before ) {
        return std::nullopt;
    }
    return Read{ std::move( layout ), std::move( root ) };
}

/// Every layout file in `directory`, read as far as its kind (sorted by name).
[[nodiscard]] std::vector<Read> read_layouts( const fs::path &directory, FileProblems &problems )
{
    std::vector<Read> read;
    for( const fs::path &path : layout_files( directory ) ) {
        std::optional<Read> one = read_kind( path, read, problems );
        if( one ) {
            read.push_back( std::move( *one ) );
        }
    }
    return read;
}

/// The layouts another may navigate to: every one read, with its kind.
[[nodiscard]] std::vector<sq::gui::LayoutTarget> targets_of( const std::vector<Read> &read )
{
    std::vector<sq::gui::LayoutTarget> targets;
    for( const Read &one : read ) {
        targets.push_back( { one.layout.name, one.layout.kind } );
    }
    return targets;
}

/// Each layout read, checked against `schema` and the others: the table of
/// each that checked.
[[nodiscard]] std::vector<Compiled> compile( const std::vector<Read> &read,
                                             const sq::gui::NameSchema &schema,
                                             FileProblems &problems )
{
    const std::vector<sq::gui::LayoutTarget> targets = targets_of( read );
    std::vector<LayoutNode> nodes( 65535 - 2 );
    std::vector<char> text( 65535 );
    std::vector<float> numbers( 65535 );
    std::vector<Compiled> out;
    for( const Read &each : read ) {
        Compiled layout = each.layout;
        const std::string file = layout.name + ".json";
        sq::gui::LayoutWriter writer( nodes, text, numbers, layout.name, layout.kind );
        if( !sq::gui::read_layout( file, each.root, schema, targets, writer, problems ) ) {
            continue;
        }
        const Layout written = writer.layout();
        if( const sq::gui::LayoutCheck checked = sq::gui::check( written ); !checked.ok() ) {
            const std::string why( sq::gui::problem_text( checked.problem ) );
            add_problem( problems, file,
                         "sqgen wrote a table that does not check (a bug): " + why );
            continue;
        }
        layout.nodes.assign( written.nodes().begin(), written.nodes().end() );
        layout.text.assign( written.text() );
        layout.numbers.assign( written.numbers().begin(), written.numbers().end() );
        layout.pane = written.pane();
        out.push_back( std::move( layout ) );
    }
    return out;
}

/// Every layout file, read, their kinds first; the table of each that checked.
[[nodiscard]] std::vector<Compiled> compile_layouts( const fs::path &directory,
                                                     const sq::gui::NameSchema &schema,
                                                     FileProblems &problems )
{
    return compile( read_layouts( directory, problems ), schema, problems );
}

/// Read names.json into `schema`, and the keys' identifiers; false when it
/// could not be read (the caller says so: it is not one of the file's problems).
[[nodiscard]] bool read_schema( const std::string &path, sq::gui::NameSchema &schema,
                                std::vector<std::string> &identifiers, FileProblems &problems )
{
    const std::string file = fs::path( path ).filename().string();
    const std::optional<std::string> text = read_file( path );
    if( !text ) {
        return false;
    }
    sq::data::JsonValue root;
    if( sq::gui::read_json( file, *text, root, problems ) ) {
        if( sq::gui::read_names( file, root, schema, problems ) ) {
            identifiers = key_identifiers( schema, file, problems );
        }
    }
    return true;
}

} // namespace sqgui
