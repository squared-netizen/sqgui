// sqgui.face: the terminal face (the design's "Terminal" row): the app's
// layouts in character cells, in sqterm, Termux or any terminal over ssh,
// on squared-tui. Phase 2: it views; nothing is written.
//
//   +-- layouts ------------+-- settings (screen) ----------------------+
//   | landing        screen |  [x] Sound                                 |
//   |>settings       screen |  <-----o----->  settings.volume           |
//   | discard        pane   |  ...                                      |
//   +-- outline ------------+                                           |
//   | rows[0][0] check_box  |                                           |
//   |>rows[1][0] slider     |                                           |
//   +-----------------------+-------------------------------------------+
//   | rows[1][0]  slider  bind="settings.volume"  max=11  step=0.5       |
//   | tab: layouts/outline  enter: open, follow a tap  b: back  q: quit  |
//
// The preview is the layout laid out in cells, not a picture of it: sizes
// rounded to cells, an image as its name, a bound value as its address (the
// window face shows the pixels). The node selected in the outline is drawn
// inverse in the preview. Problems for the layout shown replace the help line.
//
// Face is a model and a drawing, with no terminal of its own: run() gives it
// one (sq::tui::Terminal), and the tests drive it through sq::tui::Test.

export module sqgui.face;

import sq.data;
import sq.gui;
import sq.tui;
import sqgui.app;
import sqgui.reading;
import std;

export namespace sqgui
{

class Face
{
    public:
        explicit Face( const App &app ) : app_( app )
        {
            select_layout( 0 );
        }

        /** Takes a key; false when it asks to quit. */
        bool key( const sq::tui::Key &key );
        /** Draws the whole face into `screen`. */
        void draw( sq::tui::Screen &screen ) const;

        [[nodiscard]] std::size_t layout_index() const noexcept { return layout_; }
        [[nodiscard]] std::size_t node_index() const noexcept { return node_; }
        [[nodiscard]] bool outline_focused() const noexcept { return outline_; }

    private:
        void select_layout( std::size_t index );
        void open_layout( std::string_view name );
        [[nodiscard]] const Read *current() const noexcept;
        void draw_list( sq::tui::View view ) const;
        void draw_outline( sq::tui::View view ) const;
        void draw_preview( sq::tui::View view ) const;
        void draw_status( sq::tui::View view ) const;
        int draw_rows( sq::tui::View &view, const sq::data::JsonValue &rows, int x, int y, int width ) const;
        int draw_node( sq::tui::View &view, const sq::data::JsonValue &node, int x, int y, int width ) const;

        const App &app_;
        std::size_t layout_ = 0;
        std::size_t node_ = 0;
        bool outline_ = false;
        std::vector<Visit> visits_;
        std::vector<std::size_t> back_;
        std::string message_;
};

/** The terminal face on this terminal, until q: its exit status. */
int run_face( const App &app );

} // namespace sqgui

namespace sqgui
{

namespace
{

using sq::data::JsonValue;
using sq::tui::Style;

[[nodiscard]] Style bold()
{
    Style s;
    s.attributes.bold = true;
    return s;
}

[[nodiscard]] Style inverse()
{
    Style s;
    s.attributes.inverse = true;
    return s;
}

[[nodiscard]] Style faint()
{
    Style s;
    s.attributes.faint = true;
    return s;
}

/// `text` cut to `width` columns (counting each UTF-8 sequence as one).
[[nodiscard]] std::string fit( std::string_view text, int width )
{
    if( width <= 0 ) {
        return {};
    }
    std::string out;
    int columns = 0;
    for( std::size_t i = 0; i < text.size(); ) {
        const auto lead = static_cast<unsigned char>( text[i] );
        const std::size_t length = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        if( columns == width ) {
            break;
        }
        out.append( text.substr( i, length ) );
        i += length;
        ++columns;
    }
    return out;
}

[[nodiscard]] std::string word( const JsonValue *value )
{
    if( value == nullptr ) {
        return {};
    }
    if( const std::string *s = value->string_if() ) {
        return *s;
    }
    return json_text( *value, false );
}

[[nodiscard]] int span_of( const JsonValue &node )
{
    if( const JsonValue *span = node.find( "span" ) ) {
        if( const auto *u = span->unsigned_integer_if() ) {
            return std::max( 1, static_cast<int>( *u ) );
        }
    }
    return 1;
}

} // namespace

const Read *Face::current() const noexcept
{
    return layout_ < app_.layouts.size() ? &app_.layouts[layout_] : nullptr;
}

void Face::select_layout( std::size_t index )
{
    layout_ = index;
    node_ = 0;
    visits_ = current() != nullptr ? nodes_of( current()->root ) : std::vector<Visit>{};
}

void Face::open_layout( std::string_view name )
{
    for( std::size_t i = 0; i < app_.layouts.size(); ++i ) {
        if( app_.layouts[i].layout.name == name ) {
            back_.push_back( layout_ );
            select_layout( i );
            message_.clear();
            return;
        }
    }
    message_ = app_.schema.has_screen( name ) ? std::format( "{} is a C++ screen: no layout to show", name )
                                              : std::format( "no layout {}", name );
}

bool Face::key( const sq::tui::Key &key )
{
    using sq::tui::KeyName;
    if( key == 'q' || ( key.modifiers.control && key.character == 'c' ) ) {
        return false;
    }
    message_.clear();
    const std::size_t count = outline_ ? visits_.size() : app_.layouts.size();
    std::size_t &at = outline_ ? node_ : layout_;
    const auto move_to = [&]( std::size_t to ) {
        if( outline_ ) {
            node_ = to;
        } else {
            select_layout( to );
        }
    };
    if( key.name == KeyName::tab ) {
        outline_ = !outline_ && !visits_.empty();
    } else if( key.name == KeyName::up || key == 'k' ) {
        if( at > 0 ) {
            move_to( at - 1 );
        }
    } else if( key.name == KeyName::down || key == 'j' ) {
        if( at + 1 < count ) {
            move_to( at + 1 );
        }
    } else if( key.name == KeyName::home || key == 'g' ) {
        move_to( 0 );
    } else if( ( key.name == KeyName::end || key == 'G' ) && count > 0 ) {
        move_to( count - 1 );
    } else if( key.name == KeyName::enter ) {
        if( !outline_ ) {
            outline_ = !visits_.empty();
        } else if( node_ < visits_.size() ) {
            // Follow the node's tap, as the preview would: push, replace and
            // open go to that layout; back and close come back.
            const std::string tap = text_of( visits_[node_].node->find( "tap" ) );
            const std::size_t space = tap.find( ' ' );
            const std::string verb = tap.substr( 0, space );
            if( navigates( verb ) && space != std::string::npos ) {
                const std::string rest = tap.substr( space + 1 );
                open_layout( rest.substr( 0, rest.find( ' ' ) ) );
            } else if( ( verb == "back" || verb == "close" ) && !back_.empty() ) {
                const std::size_t to = back_.back();
                back_.pop_back();
                select_layout( to );
            } else if( !tap.empty() ) {
                message_ = std::format( "tap: {}  (a telegram: shown, sent nowhere)", tap );
            } else {
                message_ = "no tap here";
            }
        }
    } else if( key == 'b' || key.name == KeyName::escape || key.name == KeyName::backspace ) {
        if( !back_.empty() ) {
            const std::size_t to = back_.back();
            back_.pop_back();
            select_layout( to );
        } else if( outline_ ) {
            outline_ = false;
        }
    }
    return true;
}

void Face::draw_list( sq::tui::View view ) const
{
    view.box();
    view.put( 2, 0, " layouts ", outline_ ? Style{} : bold() );
    const int rows = view.height() - 2;
    const int first = std::max( 0, static_cast<int>( layout_ ) - rows + 1 );
    for( int i = 0; i < rows && first + i < static_cast<int>( app_.layouts.size() ); ++i ) {
        const Read &one = app_.layouts[static_cast<std::size_t>( first + i )];
        const bool here = static_cast<std::size_t>( first + i ) == layout_;
        const std::size_t problems = problems_in( app_, one.layout.name );
        std::string line = std::format( "{:<14} {:<6}{}", one.layout.name, kind_word( one.layout.kind ),
                                        problems != 0 ? " !" : "" );
        line = fit( line, view.width() - 2 );
        line.resize( static_cast<std::size_t>( std::max( 0, view.width() - 2 ) ), ' ' );
        view.put( 1, static_cast<std::uint16_t>( 1 + i ), line, here && !outline_ ? inverse() : here ? bold() : Style{} );
    }
}

void Face::draw_outline( sq::tui::View view ) const
{
    view.box();
    view.put( 2, 0, " outline ", outline_ ? bold() : Style{} );
    const int rows = view.height() - 2;
    const int first = std::max( 0, static_cast<int>( node_ ) - rows + 1 );
    for( int i = 0; i < rows && first + i < static_cast<int>( visits_.size() ); ++i ) {
        const Visit &v = visits_[static_cast<std::size_t>( first + i )];
        const bool here = static_cast<std::size_t>( first + i ) == node_;
        std::string type = text_of( v.node->find( "type" ) );
        if( type.empty() ) {
            type = "label";
        }
        std::string line = std::string( static_cast<std::size_t>( v.depth ), ' ' ) + type;
        if( const std::string t = text_of( v.node->find( "text" ) ); !t.empty() ) {
            line += " \"" + t + "\"";
        } else if( const std::string b = text_of( v.node->find( "bind" ) ); !b.empty() ) {
            line += " " + b;
        }
        line = fit( line, view.width() - 2 );
        line.resize( static_cast<std::size_t>( std::max( 0, view.width() - 2 ) ), ' ' );
        view.put( 1, static_cast<std::uint16_t>( 1 + i ), line, here && outline_ ? inverse() : Style{} );
    }
}

int Face::draw_node( sq::tui::View &view, const JsonValue &node, int x, int y, int width ) const
{
    if( width <= 0 || y >= view.height() ) {
        return 1;
    }
    const bool selected = outline_ && node_ < visits_.size() && visits_[node_].node == &node;
    const Style style = selected ? inverse() : Style{};
    std::string type = text_of( node.find( "type" ) );
    const std::string text = text_of( node.find( "text" ) );
    const std::string bind = text_of( node.find( "bind" ) );
    const auto put = [&]( std::string_view s, const Style &with ) {
        view.put( static_cast<std::uint16_t>( x ), static_cast<std::uint16_t>( y ), fit( s, width ), with );
    };
    // A bound value shows its address: the preview has no model to ask.
    const std::string shown = !bind.empty() && ( text.empty() || text == "-" || text == "0" ) ? "<" + bind + ">" : text;
    if( type.empty() || type == "label" ) {
        put( shown.empty() ? "label" : shown, bind.empty() || shown != "<" + bind + ">" ? style : selected ? style : faint() );
    } else if( type == "button" ) {
        put( "[ " + text + " ]", selected ? style : bold() );
    } else if( type == "check_box" ) {
        put( "[x] " + text, style );
    } else if( type == "text_field" ) {
        const std::string inside = text.empty() ? bind : text;
        put( "[" + inside + std::string( static_cast<std::size_t>( std::max( 0, width - 2 - static_cast<int>( inside.size() ) ) ), '_' ) + "]", style );
    } else if( type == "slider" ) {
        const int bar = std::max( 3, std::min( width, 15 ) );
        std::string s( static_cast<std::size_t>( bar ), '-' );
        s[static_cast<std::size_t>( bar / 2 )] = 'o';
        put( "<" + s + ">", style );
    } else if( type == "progress_bar" ) {
        const int bar = std::max( 3, std::min( width - 2, 15 ) );
        put( "[" + std::string( static_cast<std::size_t>( bar / 2 ), '#' ) +
                 std::string( static_cast<std::size_t>( bar - bar / 2 ), ' ' ) + "]",
             style );
    } else if( type == "image" ) {
        put( "{" + text + "}", style );
    } else if( type == "separator" ) {
        if( const JsonValue *v = node.find( "vertical" ); v != nullptr && v->boolean_if() != nullptr && *v->boolean_if() ) {
            put( "|", style );
        } else {
            put( std::string( static_cast<std::size_t>( width ), '-' ), style );
        }
    } else if( type == "custom" ) {
        put( "<" + word( node.find( "name" ) ) + ">", style );
    } else if( type == "table" ) {
        if( const JsonValue *rows = node.find( "rows" ) ) {
            return std::max( 1, draw_rows( view, *rows, x, y, width ) );
        }
    } else if( type == "scroll" ) {
        if( const JsonValue *child = node.find( "node" ) ) {
            return draw_node( view, *child, x, y, width );
        }
    } else if( type == "stack" ) {
        if( const JsonValue *nodes = node.find( "nodes" ); nodes != nullptr && nodes->array_if() != nullptr &&
                                                           !nodes->array_if()->empty() ) {
            // Stacked: the first drawn, the rest named.
            const int h = draw_node( view, nodes->array_if()->front(), x, y, width );
            if( nodes->array_if()->size() > 1 && y + h < view.height() ) {
                view.put( static_cast<std::uint16_t>( x ), static_cast<std::uint16_t>( y + h ),
                          fit( std::format( "(+{} stacked)", nodes->array_if()->size() - 1 ), width ), faint() );
                return h + 1;
            }
            return h;
        }
    } else if( type == "list" ) {
        // Three rows of its row, as the design's preview shows a list.
        int h = 0;
        if( const JsonValue *row = node.find( "row" ) ) {
            for( int i = 0; i < 3; ++i ) {
                h += draw_node( view, *row, x, y + h, width );
            }
        }
        return std::max( 1, h );
    } else {
        put( "<" + type + ">", style );
    }
    return 1;
}

int Face::draw_rows( sq::tui::View &view, const JsonValue &rows, int x, int y, int width ) const
{
    const JsonValue::Array *list = rows.array_if();
    if( list == nullptr ) {
        return 0;
    }
    int used = 0;
    for( const JsonValue &row : *list ) {
        const JsonValue::Array *cells = row.array_if();
        if( cells == nullptr || cells->empty() ) {
            continue;
        }
        int spans = 0;
        for( const JsonValue &cell : *cells ) {
            spans += span_of( cell );
        }
        int at = x, height = 1, spent = 0;
        for( const JsonValue &cell : *cells ) {
            spent += span_of( cell );
            const int end = x + width * spent / spans;
            height = std::max( height, draw_node( view, cell, at, y + used, std::max( 1, end - at - 1 ) ) );
            at = end;
        }
        used += height;
    }
    return used;
}

void Face::draw_preview( sq::tui::View view ) const
{
    view.box();
    const Read *layout = current();
    if( layout == nullptr ) {
        view.put( 2, 0, " no layouts ", bold() );
        return;
    }
    view.put( 2, 0, std::format( " {} ({}) ", layout->layout.name, kind_word( layout->layout.kind ) ), bold() );
    sq::tui::View inside = view.view( { 1, 1, static_cast<std::uint16_t>( std::max( 0, view.width() - 2 ) ),
                                        static_cast<std::uint16_t>( std::max( 0, view.height() - 2 ) ) } );
    if( const JsonValue *rows = layout->root.find( "rows" ) ) {
        draw_rows( inside, *rows, 1, 0, std::max( 1, inside.width() - 2 ) );
    }
}

void Face::draw_status( sq::tui::View view ) const
{
    std::string first;
    if( outline_ && node_ < visits_.size() ) {
        first = visits_[node_].path + "  " + node_words( *visits_[node_].node );
    } else if( const Read *layout = current() ) {
        const std::string note = text_of( layout->root.find( "note" ) );
        first = note.empty() ? layout->layout.name + ".json" : note;
    }
    view.put( 0, 0, fit( first, view.width() ) );
    std::string second = "tab: layouts/outline  enter: open, follow a tap  b: back  q: quit";
    Style style = faint();
    if( !message_.empty() ) {
        second = message_;
        style = bold();
    } else if( const Read *layout = current() ) {
        const std::string file = layout->layout.name + ".json";
        for( const sq::gui::FileProblem &p : app_.problems ) {
            if( p.where == file || p.where.starts_with( file + ":" ) ) {
                second = "problem: " + p.where + ": " + p.what;
                style = bold();
                break;
            }
        }
    }
    if( view.height() > 1 ) {
        view.put( 0, 1, fit( second, view.width() ), style );
    }
}

void Face::draw( sq::tui::Screen &screen ) const
{
    screen.clear();
    const int width = screen.columns(), height = screen.rows();
    if( width < 30 || height < 8 ) {
        screen.put( 0, 0, "sqgui: too small" );
        return;
    }
    const int left = std::min( 28, width / 3 );
    const int body = height - 2;
    const int list = std::max( 3, std::min( body / 3, static_cast<int>( app_.layouts.size() ) + 2 ) );
    const auto u = []( int v ) { return static_cast<std::uint16_t>( v ); };
    draw_list( screen.view( { 0, 0, u( left ), u( list ) } ) );
    draw_outline( screen.view( { 0, u( list ), u( left ), u( body - list ) } ) );
    draw_preview( screen.view( { u( left ), 0, u( width - left ), u( body ) } ) );
    draw_status( screen.view( { 0, u( body ), u( width ), 2 } ) );
}

int run_face( const App &app )
{
    auto terminal = sq::tui::Terminal::open();
    if( !terminal ) {
        std::println( std::cerr, "sqgui: {}", sq::tui::describe( terminal.error() ) );
        return 1;
    }
    sq::tui::Screen screen( terminal->columns(), terminal->rows() );
    Face face( app );
    for( ;; ) {
        face.draw( screen );
        terminal->show( screen );
        const std::optional<sq::tui::Event> event = terminal->next( std::chrono::milliseconds( -1 ) );
        if( !event ) {
            continue;
        }
        if( const auto *key = std::get_if<sq::tui::Key>( &*event ) ) {
            if( !face.key( *key ) ) {
                return 0;
            }
        } else if( const auto *resize = std::get_if<sq::tui::Resize>( &*event ) ) {
            screen.resize( resize->columns, resize->rows );
        }
    }
}

} // namespace sqgui
