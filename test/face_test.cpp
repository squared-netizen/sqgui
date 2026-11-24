// The terminal face (src/face.cppm) on the test app, test/gen/, through
// squared-tui's test bench: drawn into an emulated terminal, driven by keys,
// and read back as the text each row shows. No terminal is opened.

#include <stdio.h> // stderr
#include <stdlib.h> // getenv

import sq.tui;
import sqgui.app;
import sqgui.face;
import std;

namespace
{

int failures = 0;

void check( bool ok, std::string_view what, int line )
{
    if( !ok ) {
        std::println( stderr, "face_test.cpp:{}: {}", line, what );
        ++failures;
    }
}
#define CHECK( ... ) check( ( __VA_ARGS__ ), #__VA_ARGS__, __LINE__ )

/// Every row the emulator shows, joined: what a person would read.
std::string shown( sq::tui::Test &bench )
{
    std::string out;
    for( std::uint16_t y = 0; y < bench.screen().rows(); ++y ) {
        out += bench.text( 0, y ) + "\n";
    }
    return out;
}

void draw( const sqgui::Face &face, sq::tui::Test &bench )
{
    face.draw( bench.screen() );
    bench.show();
}

sq::tui::Key named( sq::tui::KeyName name )
{
    sq::tui::Key key;
    key.name = name;
    return key;
}

sq::tui::Key character( char32_t c )
{
    sq::tui::Key key;
    key.character = c;
    return key;
}

} // namespace

int main()
{
    sqgui::Files files;
    files.names = std::string( FIXTURE ) + "/names.json";
    files.layouts = std::string( FIXTURE ) + "/layouts";
    const sqgui::App app = sqgui::read_app( files );
    CHECK( app.problems.empty() );

    sqgui::Face face( app );
    sq::tui::Test bench( 100, 30 );
    draw( face, bench );
    std::string text = shown( bench );
    // The list, the first layout (discard: a pane) in the preview, the help line.
    CHECK( text.find( "layouts" ) != std::string::npos );
    CHECK( text.find( "discard (pane)" ) != std::string::npos );
    CHECK( text.find( "[ No ]" ) != std::string::npos );
    CHECK( text.find( "q: quit" ) != std::string::npos );

    // Down twice: settings, its controls drawn in cells.
    CHECK( face.key( named( sq::tui::KeyName::down ) ) );
    CHECK( face.key( character( 'j' ) ) );
    CHECK( face.layout_index() == 2 );
    draw( face, bench );
    text = shown( bench );
    CHECK( text.find( "settings (screen)" ) != std::string::npos );
    CHECK( text.find( "[x] Sound" ) != std::string::npos );
    CHECK( text.find( "<-------o------->" ) != std::string::npos ); // the slider
    CHECK( text.find( "[ Mute ]" ) != std::string::npos );           // inside a nested table
    if( std::getenv( "FACE_TEST_SHOW" ) != nullptr ) {
        std::print( "{}", text ); // what it looks like: FACE_TEST_SHOW=1 build/test/face_test
    }
    CHECK( text.find( "<item.name>" ) != std::string::npos );        // a list's row, bound
    CHECK( text.find( "{white}" ) != std::string::npos );            // a stack: its first, an image
    CHECK( text.find( "(+1 stacked)" ) != std::string::npos );       // and how many more

    // Tab: the outline; the node's keys on the status line.
    CHECK( face.key( named( sq::tui::KeyName::tab ) ) );
    CHECK( face.outline_focused() );
    CHECK( face.key( named( sq::tui::KeyName::down ) ) );
    draw( face, bench );
    text = shown( bench );
    CHECK( text.find( "rows[1][0]  slider  bind=\"settings.volume\"" ) != std::string::npos );

    // Enter on a node with a telegram tap: shown, sent nowhere.
    CHECK( face.key( named( sq::tui::KeyName::home ) ) );
    for( int i = 0; i < 4; ++i ) {
        CHECK( face.key( named( sq::tui::KeyName::down ) ) ); // rows[3][0].rows[0][0]: Mute
    }
    CHECK( face.key( named( sq::tui::KeyName::enter ) ) );
    draw( face, bench );
    CHECK( shown( bench ).find( "tap: set settings.sound false" ) != std::string::npos );

    // Back on landing, enter on "push settings" follows it; b comes back.
    CHECK( face.key( named( sq::tui::KeyName::tab ) ) ); // to the list
    CHECK( face.key( named( sq::tui::KeyName::up ) ) );   // landing
    CHECK( face.layout_index() == 1 );
    CHECK( face.key( named( sq::tui::KeyName::enter ) ) ); // into its outline
    CHECK( face.key( named( sq::tui::KeyName::end ) ) );   // the last node
    // landing's nodes: label, two labels, Settings, Play, Open, Work, Quit.
    CHECK( face.key( named( sq::tui::KeyName::home ) ) );
    for( int i = 0; i < 3; ++i ) {
        CHECK( face.key( named( sq::tui::KeyName::down ) ) );
    }
    CHECK( face.key( named( sq::tui::KeyName::enter ) ) ); // push settings
    CHECK( face.layout_index() == 2 );
    CHECK( face.key( character( 'b' ) ) );
    CHECK( face.layout_index() == 1 );

    // replace workspace: a C++ screen, said so. Back on landing the outline
    // starts again at its first node; Work is the seventh.
    for( int i = 0; i < 6; ++i ) {
        CHECK( face.key( named( sq::tui::KeyName::down ) ) );
    }
    CHECK( face.key( named( sq::tui::KeyName::enter ) ) );
    draw( face, bench );
    CHECK( shown( bench ).find( "workspace is a C++ screen" ) != std::string::npos );

    // A small terminal says so; q quits.
    sq::tui::Test small( 20, 5 );
    draw( face, small );
    CHECK( shown( small ).find( "too small" ) != std::string::npos );
    CHECK( !face.key( character( 'q' ) ) );

    if( failures != 0 ) {
        std::println( stderr, "face_test: {} failed", failures );
        return 1;
    }
    std::println( "face_test: ok" );
    return 0;
}
