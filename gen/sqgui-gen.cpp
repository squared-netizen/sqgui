// sqgui-gen: an app's names.json and layout files, compiled into C++. It was
// sqgen, in squared/tools/sqgen (sqgui's design: "sqgen lives here"); its
// output is sqgen's, byte for byte, its messages too ("sqgen: ..."), so a
// build that called sqgen calls this and nothing else changes.
//
//   sqgui-gen [--names sq_app/names.json] [--layouts sq_app/layouts] --out build/gen
//
// Writes, into --out, only the files whose text changed (so nothing that
// includes them rebuilds for nothing):
//
//   names.hpp     every address as a typed key, in namespace names
//   layouts.hpp   every layout, declared, in namespace layouts; all(), find()
//   layouts.cpp   their node tables, in read-only data
//
// Every file is read and checked before anything is written: on any problem
// each is printed, one line apiece, and nothing is written (exit 1). Built for
// the machine that builds the app, headless: squared's gui and messaging
// modules for the layout reader and checker (layout_file), the null graphics
// backend, no GLES. A build-time tool, so the standard library is used
// freely here.

#include <stdint.h>
#include <stdio.h>

import sq.gui;
import sq.messaging;
import sq.data;
import sqgui.reading;

import std;

namespace
{

namespace fs = std::filesystem;
using sqgui::Compiled;
using sqgui::plural;
using sqgui::read_file;
using sqgui::identifier_of;
using sq::gui::FileProblems;
using sq::gui::Layout;
using sq::gui::LayoutKind;
using sq::gui::LayoutNode;
using sq::messaging::ValueType;
using sq::messaging::Verb;

/// Write `text` to `path` unless it holds it already; whether it was written.
[[nodiscard]] bool write_if_changed( const fs::path &path, const std::string &text )
{
    if( const std::optional<std::string> old = read_file( path ); old && *old == text ) {
        return false;
    }
    const fs::path part = path.string() + ".part";
    {
        std::ofstream out( part, std::ios::binary | std::ios::trunc );
        out << text;
        if( !out ) {
            std::fprintf( stderr, "sqgen: could not write %s\n", part.c_str() );
            std::exit( 1 );
        }
    }
    fs::rename( part, path );
    return true;
}

[[nodiscard]] std::string_view cpp_type( ValueType type ) noexcept
{
    switch( type ) {
        case ValueType::none:
            return "void";
        case ValueType::boolean:
            return "bool";
        case ValueType::integer:
            return "std::int64_t";
        case ValueType::number:
            return "double";
        case ValueType::vec2:
            return "std::array<float, 2>";
        case ValueType::vec3:
            return "std::array<float, 3>";
        case ValueType::vec4:
            return "std::array<float, 4>";
        case ValueType::text:
            return "std::string_view";
        case ValueType::uids:
            return "std::span<const std::uint32_t>";
    }
    return "void";
}

[[nodiscard]] std::string cpp_verbs( sq::messaging::Verbs verbs )
{
    std::string out;
    for( const Verb verb : { Verb::state, Verb::event, Verb::set, Verb::do_ } ) {
        if( !verbs.has( verb ) ) {
            continue;
        }
        if( !out.empty() ) {
            out += " | ";
        }
        out += "sq::messaging::";
        if( verb == Verb::do_ ) {
            out += "do_";
        } else {
            out += sq::messaging::verb_text( verb );
        }
    }
    return out;
}

/// A C++ string literal for any bytes: printable ASCII as itself, the rest as
/// three-digit octal escapes, so a '\0' before a digit cannot run into it.
[[nodiscard]] std::string literal( std::string_view bytes )
{
    std::string out = "\"";
    for( const char c : bytes ) {
        const auto byte = static_cast<unsigned char>( c );
        if( c == '"' || c == '\\' ) {
            out += '\\';
            out += c;
        } else if( byte >= 0x20 && byte < 0x7F && c != '?' ) {
            out += c;
        } else {
            std::array<char, 5> escaped{};
            std::snprintf( escaped.data(), escaped.size(), "\\%03o", byte );
            out += escaped.data();
        }
    }
    return out + "\"";
}

/// A float as a C++ literal that reads back to the same value.
[[nodiscard]] std::string float_literal( float value )
{
    std::array<char, 32> buffer{};
    const auto written = std::to_chars( buffer.data(), buffer.data() + buffer.size(), value );
    std::string out( buffer.data(), written.ptr );
    if( out.find_first_of( ".e" ) == std::string::npos ) {
        out += ".0";
    }
    return out + "F";
}

[[nodiscard]] std::string_view alignment( sq::gui::Alignment value ) noexcept
{
    switch( value ) {
        case sq::gui::Alignment::start:
            return "sq::gui::Alignment::start";
        case sq::gui::Alignment::end:
            return "sq::gui::Alignment::end";
        case sq::gui::Alignment::center:
            break;
    }
    return "sq::gui::Alignment::center";
}

/// One node as a designated initializer: only what differs from the defaults.
/// A value type's enumerator, as the generated code names it.
[[nodiscard]] std::string_view value_type_name( ValueType type ) noexcept
{
    switch( type ) {
        case ValueType::none:
            return "none";
        case ValueType::boolean:
            return "boolean";
        case ValueType::integer:
            return "integer";
        case ValueType::number:
            return "number";
        case ValueType::vec2:
            return "vec2";
        case ValueType::vec3:
            return "vec3";
        case ValueType::vec4:
            return "vec4";
        case ValueType::text:
            return "text";
        case ValueType::uids:
            return "uids";
    }
    return "none";
}

[[nodiscard]] std::string node_literal( const LayoutNode &node, const std::string &text )
{
    std::vector<std::string> fields;
    const Layout words( {}, {}, {}, text );
    const auto offset = [&fields]( std::string_view name, std::uint16_t value ) {
        if( value != 0 ) {
            fields.push_back( "." + std::string( name ) + " = " + std::to_string( value ) );
        }
    };
    if( node.bind.id != 0 ) {
        const std::string name = literal( words.text( node.bind_text ) );
        fields.push_back( ".bind = sq::messaging::Name( " + name + " )" );
    }
    offset( "text", node.text );
    offset( "tap", node.tap );
    offset( "bind_text", node.bind_text );
    offset( "style", node.style );
    offset( "inside", node.inside );
    offset( "numbers", node.numbers );
    const std::string type( sq::gui::node_type_name( node.type ) );
    fields.push_back( ".type = sq::gui::NodeType::" + type );
    if( node.align_x != sq::gui::Alignment::center ) {
        fields.push_back( ".align_x = " + std::string( alignment( node.align_x ) ) );
    }
    if( node.align_y != sq::gui::Alignment::center ) {
        fields.push_back( ".align_y = " + std::string( alignment( node.align_y ) ) );
    }
    if( node.span != 1 ) {
        fields.push_back( ".span = " + std::to_string( node.span ) );
    }
    if( !( node.pad < 0.0F ) ) {
        fields.push_back( ".pad = " + float_literal( node.pad ) );
    }
    const std::array<std::pair<std::string_view, bool>, 8> flags = { {
        { "grow_x", node.grow_x },
        { "grow_y", node.grow_y },
        { "fill_x", node.fill_x },
        { "fill_y", node.fill_y },
        { "new_row", node.new_row },
        { "vertical", node.vertical },
        { "has_numbers", node.has_numbers },
        { "sends_set", node.sends_set },
    } };
    for( const auto &[name, on] : flags ) {
        if( on ) {
            fields.push_back( "." + std::string( name ) + " = true" );
        }
    }
    if( node.value != ValueType::none ) {
        const std::string value( value_type_name( node.value ) );
        fields.push_back( ".value = sq::messaging::ValueType::" + value );
    }
    if( node.row_uid ) {
        fields.push_back( ".row_uid = true" ); // declared after value
    }
    std::string out = "{ ";
    for( std::size_t i = 0; i < fields.size(); ++i ) {
        if( i != 0 ) {
            out += ", ";
        }
        out += fields[i];
    }
    return out + " }";
}

/// The opening of a generated file: who made it, from what.
[[nodiscard]] std::string banner( const std::string &source, std::string_view edit )
{
    std::string line = "// Generated by sqgen from " + source + ". Do not edit: edit ";
    line += edit;
    return line + ".\n";
}

[[nodiscard]] std::string names_header( const sq::gui::NameSchema &schema,
                                        const std::vector<std::string> &identifiers,
                                        const std::string &source )
{
    std::ostringstream out;
    out << banner( source, "that file" );
    out << "#pragma once\n\n";
    out << "import sq.messaging;\nimport std;\n\n";
    out << "namespace names\n{\n\n";
    for( std::size_t i = 0; i < schema.names.size(); ++i ) {
        const sq::gui::NameEntry &entry = schema.names[i];
        if( !entry.note.empty() ) {
            std::string note = entry.note;
            std::replace( note.begin(), note.end(), '\n', ' ' );
            out << "/// " << note << "\n";
        }
        out << "inline constexpr sq::messaging::Key<" << cpp_type( entry.type ) << ", ";
        out << cpp_verbs( entry.verbs );
        if( entry.uid ) {
            out << ", sq::messaging::with_uid";
        }
        out << "> " << identifiers[i] << "{ " << literal( entry.name ) << " };\n";
    }
    out << "\n} // namespace names\n";
    return out.str();
}

[[nodiscard]] std::string_view kind_word( LayoutKind kind ) noexcept
{
    if( kind == LayoutKind::pane ) {
        return "pane";
    }
    return "screen";
}

[[nodiscard]] std::string layouts_header( const std::vector<Compiled> &layouts,
                                          const std::string &source )
{
    std::ostringstream out;
    out << banner( source, "the layout files" );
    out << "#pragma once\n\n";
    out << "import sq.gui;\nimport std;\n\n";
    out << "namespace layouts\n{\n\n";
    for( const Compiled &layout : layouts ) {
        out << "/// " << layout.name << ".json, a " << kind_word( layout.kind ) << "\n";
        out << "extern const sq::gui::Layout " << layout.identifier << ";\n";
    }
    out << "\n/// Every layout, sorted by name.\n";
    out << "[[nodiscard]] std::span<const sq::gui::Layout *const> all() noexcept;\n\n";
    out << "/// The layout called `name`; nullptr when there is none.\n";
    out << "[[nodiscard]] const sq::gui::Layout *find( std::string_view name ) noexcept;\n\n";
    out << "/// The C++ screens names.json lists, sorted: each one added by the app\n";
    out << "/// (`app.stage.add<T>()`), which the Stage checks at start.\n";
    out << "[[nodiscard]] std::span<const std::string_view> screens() noexcept;\n\n";
    out << "} // namespace layouts\n";
    return out.str();
}

/// One layout's tables: its text, its nodes and its numbers.
void layout_tables( std::ostringstream &out, const Compiled &layout )
{
    const std::string &id = layout.identifier;
    out << "// " << layout.name << ".json\n";
    out << "constexpr char " << id << "_text[] = " << literal( layout.text ) << ";\n";
    if( !layout.nodes.empty() ) {
        out << "constexpr sq::gui::LayoutNode " << id << "_nodes[] = {\n";
        for( const LayoutNode &node : layout.nodes ) {
            out << "    " << node_literal( node, layout.text ) << ",\n";
        }
        out << "};\n";
    }
    if( !layout.numbers.empty() ) {
        out << "constexpr float " << id << "_numbers[] = { ";
        for( std::size_t i = 0; i < layout.numbers.size(); ++i ) {
            if( i != 0 ) {
                out << ", ";
            }
            out << float_literal( layout.numbers[i] );
        }
        out << " };\n";
    }
    out << "\n";
}

/// One layout's definition, from its tables.
void layout_definition( std::ostringstream &out, const Compiled &layout )
{
    const std::string &id = layout.identifier;
    out << "constinit const sq::gui::Layout " << id << "{ " << literal( layout.name );
    out << ", sq::gui::LayoutKind::" << kind_word( layout.kind );
    if( layout.nodes.empty() ) {
        out << ", {}";
    } else {
        out << ", " << id << "_nodes";
    }
    out << ", std::string_view( " << id << "_text, sizeof( " << id << "_text ) - 1 )";
    if( !layout.numbers.empty() ) {
        out << ", " << id << "_numbers";
    } else if( layout.kind == LayoutKind::pane ) {
        out << ", {}";
    }
    if( layout.kind == LayoutKind::pane ) {
        out << ", sq::gui::PaneShape{ sq::gui::PanePlace::";
        out << sq::gui::pane_place_name( layout.pane.place ) << ", ";
        out << ( layout.pane.modal ? "true" : "false" ) << " }";
    }
    out << " };\n";
}

[[nodiscard]] std::string layouts_source( const std::vector<Compiled> &layouts,
                                          std::vector<std::string> screens,
                                          const std::string &source )
{
    std::ostringstream out;
    out << banner( source, "the layout files" );
    out << "#include <layouts.hpp>\n\n";
    out << "import sq.gui;\nimport sq.messaging;\nimport std;\n\n";
    out << "namespace layouts\n{\n\nnamespace\n{\n\n";
    for( const Compiled &layout : layouts ) {
        layout_tables( out, layout );
    }
    out << "} // namespace\n\n";
    for( const Compiled &layout : layouts ) {
        layout_definition( out, layout );
    }
    out << "\nnamespace\n{\n\n";
    out << "constexpr std::array<const sq::gui::Layout *, " << layouts.size() << "> sorted = {";
    for( std::size_t i = 0; i < layouts.size(); ++i ) {
        out << ( i == 0 ? " &" : ", &" ) << layouts[i].identifier;
    }
    if( !layouts.empty() ) {
        out << " ";
    }
    out << "};\n\n";
    out << "[[nodiscard]] bool named_below( const sq::gui::Layout *layout,\n";
    out << "                                std::string_view name ) noexcept\n";
    out << "{\n    return layout->name() < name;\n}\n\n";
    out << "} // namespace\n\n";
    out << "std::span<const sq::gui::Layout *const> all() noexcept\n{\n    return sorted;\n}\n\n";
    out << "const sq::gui::Layout *find( std::string_view name ) noexcept\n{\n";
    out << "    const auto found =\n";
    out << "        std::lower_bound( sorted.begin(), sorted.end(), name, named_below );\n";
    out << "    if( found == sorted.end() || ( *found )->name() != name ) {\n";
    out << "        return nullptr;\n    }\n    return *found;\n}\n\n";
    std::sort( screens.begin(), screens.end() );
    out << "namespace\n{\n\n";
    out << "constexpr std::array<std::string_view, " << screens.size() << "> screen_names = {";
    for( std::size_t i = 0; i < screens.size(); ++i ) {
        out << ( i == 0 ? " " : ", " ) << literal( screens[i] );
    }
    if( !screens.empty() ) {
        out << " ";
    }
    out << "};\n\n} // namespace\n\n";
    out << "std::span<const std::string_view> screens() noexcept\n{\n    return "
           "screen_names;\n}\n\n";
    out << "} // namespace layouts\n";
    return out.str();
}

int usage()
{
    std::fprintf( stderr, "usage: sqgui-gen [--names FILE] [--layouts DIR] --out DIR\n" );
    return 2;
}

/// What the command line asked for.
struct Arguments
{
        std::string names;
        std::string layouts;
        std::string out;
};

[[nodiscard]] std::optional<Arguments> arguments_of( int argc, char **argv )
{
    Arguments read;
    for( int i = 1; i < argc; ++i ) {
        const std::string_view argument = argv[i];
        if( i + 1 >= argc ) {
            return std::nullopt;
        }
        if( argument == "--names" ) {
            read.names = argv[++i];
        } else if( argument == "--layouts" ) {
            read.layouts = argv[++i];
        } else if( argument == "--out" ) {
            read.out = argv[++i];
        } else {
            return std::nullopt;
        }
    }
    if( read.out.empty() ) {
        return std::nullopt;
    }
    return read;
}

} // namespace

int main( int argc, char **argv )
{
    const std::optional<Arguments> given = arguments_of( argc, argv );
    if( !given ) {
        return usage();
    }

    FileProblems problems;
    sq::gui::NameSchema schema;
    std::vector<std::string> identifiers;
    if( !given->names.empty() && !sqgui::read_schema( given->names, schema, identifiers, problems ) ) {
        std::fprintf( stderr, "sqgen: %s could not be read\n", given->names.c_str() );
        return 1;
    }
    std::vector<Compiled> layouts;
    if( !given->layouts.empty() ) {
        if( !fs::is_directory( given->layouts ) ) {
            std::fprintf( stderr, "sqgen: %s is not a directory\n", given->layouts.c_str() );
            return 1;
        }
        layouts = sqgui::compile_layouts( given->layouts, schema, problems );
    }
    if( !problems.empty() ) {
        for( const sq::gui::FileProblem &problem : problems ) {
            std::fprintf( stderr, "sqgen: %s: %s\n", problem.where.c_str(), problem.what.c_str() );
        }
        const std::size_t count = problems.size();
        const std::string_view s = plural( count );
        std::fprintf( stderr, "sqgen: %zu problem%.*s; nothing written\n", count,
                      static_cast<int>( s.size() ), s.data() );
        return 1;
    }

    fs::create_directories( given->out );
    const fs::path out( given->out );
    std::string names_from = "no names.json";
    if( !given->names.empty() ) {
        names_from = given->names;
    }
    std::string layouts_from = "no layouts";
    if( !given->layouts.empty() ) {
        layouts_from = given->layouts;
    }
    std::size_t written = 0;
    if( write_if_changed( out / "names.hpp", names_header( schema, identifiers, names_from ) ) ) {
        ++written;
    }
    if( write_if_changed( out / "layouts.hpp", layouts_header( layouts, layouts_from ) ) ) {
        ++written;
    }
    const std::string source = layouts_source( layouts, schema.screens, layouts_from );
    if( write_if_changed( out / "layouts.cpp", source ) ) {
        ++written;
    }
    std::string summary = std::to_string( schema.names.size() ) + " key";
    summary += plural( schema.names.size() );
    summary += ", " + std::to_string( layouts.size() ) + " layout";
    summary += plural( layouts.size() );
    summary += "; " + std::to_string( written ) + " file";
    summary += plural( written );
    std::printf( "  sqgen          %s changed\n", summary.c_str() );
    return 0;
}
