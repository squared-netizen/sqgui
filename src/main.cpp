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
//   sqgui view                       the terminal face: the layouts in character cells
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
import std;

namespace
{

namespace fs = std::filesystem;
using sq::data::JsonValue;
using namespace sqgui;

int usage( std::FILE *out = stderr, int status = 2 )
{
    std::println( out, "usage: sqgui [--app DIR | --names FILE --layouts DIR] [--json] COMMAND\n"
                       "  list                every layout: its kind, its nodes, whether it checks\n"
                       "  show LAYOUT         its nodes as an outline, each with its path\n"
                       "  get LAYOUT PATH     one value (sqgui get settings rows[1][0].bind)\n"
                       "  check               every problem the build would stop for\n"
                       "  map                 which layout pushes, replaces, opens or closes which\n"
                       "  names               names.json's addresses, and the layouts using each\n"
                       "  view                the terminal face: browse the layouts, drawn in cells\n"
                       "where the files are: --app, --names, --layouts, else square's answer" );
    return status;
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
        command != "names" && command != "view" ) {
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
    if( command == "view" ) {
        return run_face( app );
    }
    return names( app, as_json );
}
