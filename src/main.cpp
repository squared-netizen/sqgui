// sqgui: the squared SDK's UI editor. Phase 2, the viewer: it reads an app's
// layouts and names.json and says what they hold, as words or as JSON
// (--json), and writes nothing.
//
//   sqgui list                       every layout: its kind, its nodes, whether it checks
//   sqgui show LAYOUT                its nodes as an outline, each with its path
//   sqgui get LAYOUT PATH            one value: sqgui get settings rows[1][0].bind
//   sqgui check                      every problem the build would stop for (exit 1)
//   sqgui map                        which layout pushes, replaces, opens or closes which
//   sqgui names                      names.json's addresses, and the layouts using each
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
import std;

namespace
{

namespace fs = std::filesystem;
using sq::data::JsonValue;

int usage( std::FILE *out = stderr, int status = 2 )
{
    std::println( out, "usage: sqgui [--app DIR | --names FILE --layouts DIR] [--json] COMMAND\n"
                       "  list                every layout: its kind, its nodes, whether it checks\n"
                       "  show LAYOUT         its nodes as an outline, each with its path\n"
                       "  get LAYOUT PATH     one value (sqgui get settings rows[1][0].bind)\n"
                       "  check               every problem the build would stop for\n"
                       "  map                 which layout pushes, replaces, opens or closes which\n"
                       "  names               names.json's addresses, and the layouts using each\n"
                       "where the files are: --app, --names, --layouts, else square's answer" );
    return status;
}

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

// --- the commands -----------------------------------------------------------

int list( const App &app, bool as_json )
{
    JsonValue::Array rows;
    for( const sqgui::Read &one : app.layouts ) {
        const sqgui::Compiled *compiled = find_compiled( app, one.layout.name );
        const std::size_t problems = problems_in( app, one.layout.name );
        const std::size_t nodes = compiled != nullptr ? compiled->nodes.size() : nodes_of( one.root ).size();
        if( as_json ) {
            rows.emplace_back( JsonValue::Object{
                { "name", JsonValue( one.layout.name ) },
                { "kind", JsonValue( kind_word( one.layout.kind ) ) },
                { "nodes", JsonValue( static_cast<std::int64_t>( nodes ) ) },
                { "problems", JsonValue( static_cast<std::int64_t>( problems ) ) },
            } );
        } else {
            std::println( "{:<20} {:<7} {:>4} nodes{}", one.layout.name, kind_word( one.layout.kind ), nodes,
                          problems == 0 ? std::string()
                                        : std::format( "  {} problem{}", problems, problems == 1 ? "" : "s" ) );
        }
    }
    if( as_json ) {
        std::println( "{}", json_text( JsonValue( std::move( rows ) ), true ) );
    } else if( app.layouts.empty() ) {
        std::println( "no layouts{}", app.files.layouts.empty() ? "" : " in " + app.files.layouts );
    }
    return 0;
}

int show( const App &app, std::string_view name, bool as_json )
{
    const sqgui::Read *layout = find_layout( app, name );
    if( layout == nullptr ) {
        std::println( stderr, "sqgui: no layout {} (sqgui list)", name );
        return 1;
    }
    const std::vector<Visit> visits = nodes_of( layout->root );
    if( as_json ) {
        JsonValue::Array nodes;
        for( const Visit &v : visits ) {
            nodes.emplace_back( JsonValue::Object{ { "path", JsonValue( v.path ) },
                                                   { "depth", JsonValue( static_cast<std::int64_t>( v.depth ) ) },
                                                   { "node", *v.node } } );
        }
        std::println( "{}", json_text( JsonValue( JsonValue::Object{
                                           { "name", JsonValue( layout->layout.name ) },
                                           { "kind", JsonValue( kind_word( layout->layout.kind ) ) },
                                           { "nodes", JsonValue( std::move( nodes ) ) } } ),
                                       true ) );
        return 0;
    }
    std::string head = std::format( "{} ({}", layout->layout.name, kind_word( layout->layout.kind ) );
    if( const JsonValue *place = layout->root.find( "place" ) ) {
        head += ", " + text_of( place );
    }
    if( const JsonValue *modal = layout->root.find( "modal" );
        modal != nullptr && modal->boolean_if() != nullptr && *modal->boolean_if() ) {
        head += ", modal";
    }
    std::println( "{})", head );
    if( const std::string note = text_of( layout->root.find( "note" ) ); !note.empty() ) {
        std::println( "  {}", note );
    }
    for( const Visit &v : visits ) {
        std::println( "{}{:<{}}  {}", std::string( static_cast<std::size_t>( 2 + 2 * v.depth ), ' ' ), v.path,
                      std::max( 0, 24 - 2 * v.depth ), node_words( *v.node ) );
    }
    if( const std::size_t problems = problems_in( app, name ); problems != 0 ) {
        std::println( "  {} problem{}: sqgui check", problems, problems == 1 ? "" : "s" );
    }
    return 0;
}

int get( const App &app, std::string_view name, std::string_view path, bool as_json )
{
    const sqgui::Read *layout = find_layout( app, name );
    if( layout == nullptr ) {
        std::println( stderr, "sqgui: no layout {} (sqgui list)", name );
        return 1;
    }
    const auto value = at_path( layout->root, path );
    if( !value ) {
        std::println( stderr, "sqgui: {}.json: {}", name, value.error() );
        return 1;
    }
    const std::string *s = ( *value )->string_if();
    std::println( "{}", s != nullptr && !as_json ? *s : json_text( **value, ( *value )->object_if() != nullptr ) );
    return 0;
}

int check( const App &app, bool as_json )
{
    if( as_json ) {
        JsonValue::Array problems;
        for( const sq::gui::FileProblem &p : app.problems ) {
            problems.emplace_back(
                JsonValue::Object{ { "where", JsonValue( p.where ) }, { "what", JsonValue( p.what ) } } );
        }
        std::println( "{}", json_text( JsonValue( JsonValue::Object{
                                           { "names", JsonValue( static_cast<std::int64_t>( app.schema.names.size() ) ) },
                                           { "layouts", JsonValue( static_cast<std::int64_t>( app.layouts.size() ) ) },
                                           { "problems", JsonValue( std::move( problems ) ) } } ),
                                       true ) );
    } else {
        for( const sq::gui::FileProblem &p : app.problems ) {
            std::println( "{}: {}", p.where, p.what );
        }
        if( app.problems.empty() ) {
            std::println( "no problems: {} name{}, {} layout{}", app.schema.names.size(),
                          app.schema.names.size() == 1 ? "" : "s", app.layouts.size(),
                          app.layouts.size() == 1 ? "" : "s" );
        } else {
            std::println( "{} problem{}", app.problems.size(), app.problems.size() == 1 ? "" : "s" );
        }
    }
    return app.problems.empty() ? 0 : 1;
}

int map( const App &app, bool as_json )
{
    const std::vector<Edge> edges = edges_of( app );
    if( as_json ) {
        JsonValue::Array nodes;
        for( const sqgui::Read &one : app.layouts ) {
            nodes.emplace_back( JsonValue::Object{ { "name", JsonValue( one.layout.name ) },
                                                   { "kind", JsonValue( kind_word( one.layout.kind ) ) },
                                                   { "cpp", JsonValue( false ) } } );
        }
        for( const std::string &screen : app.schema.screens ) {
            nodes.emplace_back( JsonValue::Object{ { "name", JsonValue( screen ) },
                                                   { "kind", JsonValue( "screen" ) },
                                                   { "cpp", JsonValue( true ) } } );
        }
        JsonValue::Array links;
        for( const Edge &e : edges ) {
            if( e.verb == "do" || e.verb == "set" ) {
                continue;
            }
            links.emplace_back( JsonValue::Object{ { "from", JsonValue( e.from ) },
                                                   { "verb", JsonValue( e.verb ) },
                                                   { "to", e.target.empty() ? JsonValue( nullptr ) : JsonValue( e.target ) },
                                                   { "path", JsonValue( e.path ) } } );
        }
        std::println( "{}", json_text( JsonValue( JsonValue::Object{ { "layouts", JsonValue( std::move( nodes ) ) },
                                                                     { "links", JsonValue( std::move( links ) ) } } ),
                                       true ) );
        return 0;
    }
    for( const sqgui::Read &one : app.layouts ) {
        std::println( "{} ({})", one.layout.name, kind_word( one.layout.kind ) );
        bool any = false;
        for( const Edge &e : edges ) {
            if( e.from != one.layout.name || e.verb == "do" || e.verb == "set" ) {
                continue;
            }
            any = true;
            if( navigates( e.verb ) ) {
                const bool cpp = find_layout( app, e.target ) == nullptr && app.schema.has_screen( e.target );
                std::println( "  {:<8} {}{}", e.verb, e.target, cpp ? "  (C++)" : "" );
            } else {
                std::println( "  {}", e.verb );
            }
        }
        if( !any ) {
            std::println( "  (goes nowhere)" );
        }
    }
    for( const std::string &screen : app.schema.screens ) {
        std::println( "{} (screen, C++)", screen );
    }
    return 0;
}

int names( const App &app, bool as_json )
{
    // Which layouts bind or tap each address.
    std::map<std::string, std::set<std::string>> users;
    for( const sqgui::Read &one : app.layouts ) {
        for( const Visit &v : nodes_of( one.root ) ) {
            if( const std::string bind = text_of( v.node->find( "bind" ) ); !bind.empty() ) {
                users[bind].insert( one.layout.name );
            }
            const std::string tap = text_of( v.node->find( "tap" ) );
            if( tap.starts_with( "do " ) || tap.starts_with( "set " ) ) {
                std::string rest = tap.substr( tap.find( ' ' ) + 1 );
                users[rest.substr( 0, rest.find( ' ' ) )].insert( one.layout.name );
            }
        }
    }
    JsonValue::Array rows;
    for( const sq::gui::NameEntry &entry : app.schema.names ) {
        std::string verbs;
        for( const auto &[verb, word] : { std::pair{ sq::messaging::Verb::state, "state" },
                                          std::pair{ sq::messaging::Verb::set, "set" },
                                          std::pair{ sq::messaging::Verb::do_, "do" },
                                          std::pair{ sq::messaging::Verb::event, "event" } } ) {
            if( entry.verbs.has( verb ) ) {
                verbs += ( verbs.empty() ? "" : "," ) + std::string( word );
            }
        }
        std::string used;
        for( const std::string &u : users[entry.name] ) {
            used += ( used.empty() ? "" : ", " ) + u;
        }
        const std::string type( entry.list.empty() ? type_word( entry.type ) : "list" );
        if( as_json ) {
            JsonValue::Array by;
            for( const std::string &u : users[entry.name] ) {
                by.emplace_back( u );
            }
            rows.emplace_back( JsonValue::Object{ { "name", JsonValue( entry.name ) },
                                                  { "type", JsonValue( type ) },
                                                  { "verbs", JsonValue( verbs ) },
                                                  { "uid", JsonValue( entry.uid ) },
                                                  { "note", JsonValue( entry.note ) },
                                                  { "used_by", JsonValue( std::move( by ) ) } } );
        } else {
            std::println( "{:<24} {:<6} {:<12} {:<3}  {}", entry.name, type, verbs, entry.uid ? "uid" : "",
                          used.empty() ? "(unused)" : used );
        }
    }
    if( as_json ) {
        std::println( "{}", json_text( JsonValue( std::move( rows ) ), true ) );
    } else if( app.schema.names.empty() ) {
        std::println( "no names{}", app.files.names.empty() ? " (no names.json)" : "" );
    }
    return 0;
}

} // namespace

int main( int argc, char **argv )
{
    std::vector<std::string_view> args( argv + 1, argv + argc );
    std::optional<std::string> app_dir, names_file, layouts_dir;
    bool as_json = false;
    std::vector<std::string_view> words;
    for( std::size_t i = 0; i < args.size(); ++i ) {
        const std::string_view a = args[i];
        if( a == "--help" || a == "-h" || a == "help" ) {
            return usage( stdout, 0 );
        }
        if( a == "--json" ) {
            as_json = true;
        } else if( a == "--app" || a == "--names" || a == "--layouts" ) {
            if( i + 1 >= args.size() ) {
                return usage();
            }
            const std::string value( args[++i] );
            ( a == "--app" ? app_dir : a == "--names" ? names_file : layouts_dir ) = value;
        } else if( a.starts_with( "--" ) ) {
            return usage();
        } else {
            words.push_back( a );
        }
    }
    if( words.empty() ) {
        return usage();
    }
    const std::string_view command = words[0];
    const std::size_t want = command == "show" ? 2 : command == "get" ? 3 : 1;
    if( command != "list" && command != "show" && command != "get" && command != "check" && command != "map" &&
        command != "names" ) {
        std::println( stderr, "sqgui: no command {} (sqgui --help)", command );
        return 2;
    }
    if( words.size() != want ) {
        return usage();
    }

    Files files;
    if( app_dir || names_file || layouts_dir ) {
        std::error_code error;
        if( app_dir ) {
            const fs::path dir( *app_dir );
            if( fs::is_regular_file( dir / "names.json", error ) ) {
                files.names = ( dir / "names.json" ).string();
            }
            if( fs::is_directory( dir / "layouts", error ) ) {
                files.layouts = ( dir / "layouts" ).string();
            }
        }
        if( names_file ) {
            files.names = *names_file;
        }
        if( layouts_dir ) {
            files.layouts = *layouts_dir;
        }
    } else {
        auto asked = ask_square();
        if( !asked ) {
            std::println( stderr, "sqgui: {}", asked.error() );
            return 1;
        }
        files = std::move( *asked );
    }
    const App app = read_app( files );
    if( app.names_unreadable ) {
        std::println( stderr, "sqgui: {} could not be read", files.names );
        return 1;
    }
    if( command == "list" ) {
        return list( app, as_json );
    }
    if( command == "show" ) {
        return show( app, words[1], as_json );
    }
    if( command == "get" ) {
        return get( app, words[1], words[2], as_json );
    }
    if( command == "check" ) {
        return check( app, as_json );
    }
    if( command == "map" ) {
        return map( app, as_json );
    }
    return names( app, as_json );
}
