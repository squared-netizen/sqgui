// sqgui.app: an app's layouts and names.json, found (through square, or
// where the command line says) and read through sqgui.reading; and what the
// viewer's faces ask of them: each layout's nodes with their paths, a value
// by its path, the navigation between layouts. The command line (main.cpp)
// and the terminal face (face.cppm) both read through it.

module;

#include <stdio.h> // popen: a macro-free function import std does not bring

export module sqgui.app;

import sq.data;
import sq.gui;
import sq.messaging;
import sqgui.reading;
import std;

export namespace sqgui
{

namespace fs = std::filesystem;
using sq::data::JsonValue;

[[nodiscard]] std::string json_text( const JsonValue &value, bool pretty )
{
    const auto written = sq::data::write_json( value, { .pretty = pretty } );
    return written ? written.text : std::string( "null" );
}

[[nodiscard]] std::string text_of( const JsonValue *value )
{
    const std::string *s = value != nullptr ? value->string_if() : nullptr;
    return s != nullptr ? *s : std::string();
}

// --- where the files are ----------------------------------------------------

struct Files
{
        std::string names;   // empty: none
        std::string layouts; // empty: none
};

/// square's answer: the workspace's app and layouts roles (square --json info).
[[nodiscard]] std::expected<Files, std::string> ask_square()
{
    FILE *pipe = popen( "square --json info 2>/dev/null", "r" );
    if( pipe == nullptr ) {
        return std::unexpected( "could not run square" );
    }
    std::string out;
    std::array<char, 4096> buffer{};
    while( std::size_t n = std::fread( buffer.data(), 1, buffer.size(), pipe ) ) {
        out.append( buffer.data(), n );
    }
    const int status = pclose( pipe );
    if( status != 0 || out.empty() ) {
        return std::unexpected( "square found no workspace here (or is not installed: pkg install square); "
                                "say where the files are with --app DIR" );
    }
    const auto parsed = sq::data::parse_json( out );
    if( !parsed ) {
        return std::unexpected( "square's answer is not JSON" );
    }
    const fs::path root = text_of( parsed.value.find( "workspace" ) );
    const JsonValue *roles = parsed.value.find( "roles" );
    const std::string app = text_of( roles != nullptr ? roles->find( "app" ) : nullptr );
    std::string layouts = text_of( roles != nullptr ? roles->find( "layouts" ) : nullptr );
    if( app.empty() && layouts.empty() ) {
        return std::unexpected( "the workspace names no app or layouts role (square where app)" );
    }
    Files files;
    std::error_code error;
    if( !app.empty() && fs::is_regular_file( root / app / "names.json", error ) ) {
        files.names = ( root / app / "names.json" ).string();
    }
    if( layouts.empty() ) {
        layouts = app + "/layouts";
    }
    if( fs::is_directory( root / layouts, error ) ) {
        files.layouts = ( root / layouts ).string();
    }
    return files;
}

// --- the app, read ----------------------------------------------------------

struct App
{
        Files files;
        sq::gui::NameSchema schema;
        std::vector<sqgui::Read> layouts;      // every file read as far as its kind
        std::vector<sqgui::Compiled> compiled; // each that checked
        sq::gui::FileProblems problems;
        bool names_unreadable = false;
};

[[nodiscard]] App read_app( const Files &files )
{
    App app;
    app.files = files;
    std::vector<std::string> identifiers;
    if( !files.names.empty() &&
        !sqgui::read_schema( files.names, app.schema, identifiers, app.problems ) ) {
        app.names_unreadable = true;
    }
    if( !files.layouts.empty() ) {
        app.layouts = sqgui::read_layouts( files.layouts, app.problems );
        app.compiled = sqgui::compile( app.layouts, app.schema, app.problems );
    }
    return app;
}

[[nodiscard]] const sqgui::Read *find_layout( const App &app, std::string_view name )
{
    for( const sqgui::Read &one : app.layouts ) {
        if( one.layout.name == name ) {
            return &one;
        }
    }
    return nullptr;
}

[[nodiscard]] const sqgui::Compiled *find_compiled( const App &app, std::string_view name )
{
    for( const sqgui::Compiled &one : app.compiled ) {
        if( one.name == name ) {
            return &one;
        }
    }
    return nullptr;
}

/// A layout's problems: those whose place is its file.
[[nodiscard]] std::size_t problems_in( const App &app, std::string_view name )
{
    const std::string file = std::string( name ) + ".json";
    std::size_t count = 0;
    for( const sq::gui::FileProblem &p : app.problems ) {
        if( p.where == file || p.where.starts_with( file + ":" ) ) {
            ++count;
        }
    }
    return count;
}

/// A value type as names.json spells it.
[[nodiscard]] std::string_view type_word( sq::messaging::ValueType type )
{
    using sq::messaging::ValueType;
    switch( type ) {
        case ValueType::none:
            return "-";
        case ValueType::boolean:
            return "bool";
        case ValueType::integer:
            return "int";
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
    return "-";
}

[[nodiscard]] std::string_view kind_word( sq::gui::LayoutKind kind )
{
    return kind == sq::gui::LayoutKind::pane ? "pane" : "screen";
}

// --- walking a layout file's nodes -----------------------------------------

/// One node of a layout file: its path (rows[1][0], rows[3][0].rows[0][1],
/// rows[4][0].node), its depth, and its JSON.
struct Visit
{
        std::string path;
        int depth = 0;
        const JsonValue *node = nullptr;
};

void walk_rows( const JsonValue &rows, const std::string &path, int depth, std::vector<Visit> &out );

void walk_node( const JsonValue &node, const std::string &path, int depth, std::vector<Visit> &out )
{
    out.push_back( { path, depth, &node } );
    if( const JsonValue *rows = node.find( "rows" ) ) {
        walk_rows( *rows, path + ".rows", depth + 1, out );
    }
    if( const JsonValue *child = node.find( "node" ); child != nullptr && child->object_if() != nullptr ) {
        walk_node( *child, path + ".node", depth + 1, out );
    }
    if( const JsonValue *row = node.find( "row" ); row != nullptr && row->object_if() != nullptr ) {
        walk_node( *row, path + ".row", depth + 1, out );
    }
    if( const JsonValue *nodes = node.find( "nodes" ); nodes != nullptr && nodes->array_if() != nullptr ) {
        std::size_t i = 0;
        for( const JsonValue &each : *nodes->array_if() ) {
            if( each.object_if() != nullptr ) {
                walk_node( each, std::format( "{}.nodes[{}]", path, i ), depth + 1, out );
            }
            ++i;
        }
    }
}

void walk_rows( const JsonValue &rows, const std::string &path, int depth, std::vector<Visit> &out )
{
    const JsonValue::Array *list = rows.array_if();
    if( list == nullptr ) {
        return;
    }
    for( std::size_t r = 0; r < list->size(); ++r ) {
        const JsonValue::Array *cells = ( *list )[r].array_if();
        if( cells == nullptr ) {
            continue;
        }
        for( std::size_t c = 0; c < cells->size(); ++c ) {
            if( ( *cells )[c].object_if() != nullptr ) {
                walk_node( ( *cells )[c], std::format( "{}[{}][{}]", path, r, c ), depth, out );
            }
        }
    }
}

[[nodiscard]] std::vector<Visit> nodes_of( const JsonValue &root )
{
    std::vector<Visit> out;
    if( const JsonValue *rows = root.find( "rows" ) ) {
        walk_rows( *rows, "rows", 0, out );
    }
    return out;
}

/// A node's keys as words: its type first, then text, bind, tap, style, the rest by name.
[[nodiscard]] std::string node_words( const JsonValue &node )
{
    static constexpr std::array<std::string_view, 4> first{ "text", "bind", "tap", "style" };
    static constexpr std::array<std::string_view, 6> inner{ "type", "rows", "node", "nodes", "row", "note" };
    std::string out = text_of( node.find( "type" ) );
    if( out.empty() ) {
        out = "label";
    }
    const auto add = [&]( std::string_view key, const JsonValue &value ) {
        out += std::format( "  {}={}", key, json_text( value, false ) );
    };
    for( std::string_view key : first ) {
        if( const JsonValue *value = node.find( key ) ) {
            add( key, *value );
        }
    }
    if( const JsonValue::Object *object = node.object_if() ) {
        for( const auto &[key, value] : *object ) {
            if( std::ranges::find( first, key ) != first.end() || std::ranges::find( inner, key ) != inner.end() ) {
                continue;
            }
            add( key, value );
        }
    }
    return out;
}

// --- a path into a layout file: rows[1][0].bind ------------------------------

[[nodiscard]] std::expected<const JsonValue *, std::string> at_path( const JsonValue &root, std::string_view path )
{
    const JsonValue *here = &root;
    std::size_t i = 0;
    while( i < path.size() ) {
        if( path[i] == '.' ) {
            ++i;
            continue;
        }
        if( path[i] == '[' ) {
            const std::size_t close = path.find( ']', i );
            if( close == std::string_view::npos ) {
                return std::unexpected( "no ] after [" );
            }
            std::size_t index = 0;
            const auto digits = path.substr( i + 1, close - i - 1 );
            const auto [end, error] = std::from_chars( digits.data(), digits.data() + digits.size(), index );
            if( error != std::errc() || end != digits.data() + digits.size() ) {
                return std::unexpected( std::format( "[{}] is not an index", digits ) );
            }
            const JsonValue::Array *array = here->array_if();
            if( array == nullptr || index >= array->size() ) {
                return std::unexpected( std::format( "{}: no [{}] there", path.substr( 0, i ), index ) );
            }
            here = &( *array )[index];
            i = close + 1;
            continue;
        }
        std::size_t end = i;
        while( end < path.size() && path[end] != '.' && path[end] != '[' ) {
            ++end;
        }
        const std::string_view key = path.substr( i, end - i );
        const JsonValue *next = here->find( key );
        if( next == nullptr ) {
            return std::unexpected( std::format( "{}: no {} there", i == 0 ? "the layout" : path.substr( 0, i - 1 ), key ) );
        }
        here = next;
        i = end;
    }
    return here;
}

// --- navigation: a tap's first word ----------------------------------------

struct Edge
{
        std::string from;
        std::string verb;   // push, replace, open, close, back, quit, do, set
        std::string target; // a layout or a C++ screen; empty for close, back, quit
        std::string path;   // where the tap is
};

[[nodiscard]] std::vector<Edge> edges_of( const App &app )
{
    std::vector<Edge> out;
    for( const sqgui::Read &layout : app.layouts ) {
        for( const Visit &visit : nodes_of( layout.root ) ) {
            const std::string tap = text_of( visit.node->find( "tap" ) );
            if( tap.empty() ) {
                continue;
            }
            const std::size_t space = tap.find( ' ' );
            Edge edge{ layout.layout.name, tap.substr( 0, space ), {}, visit.path + ".tap" };
            if( space != std::string::npos ) {
                std::string rest = tap.substr( space + 1 );
                edge.target = rest.substr( 0, rest.find( ' ' ) );
            }
            out.push_back( std::move( edge ) );
        }
    }
    return out;
}

[[nodiscard]] bool navigates( std::string_view verb )
{
    return verb == "push" || verb == "replace" || verb == "open";
}

} // namespace sqgui
