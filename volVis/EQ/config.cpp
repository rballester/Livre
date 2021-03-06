
/* Copyright (c) 2006-2011, Stefan Eilemann <eile@equalizergraphics.com>
 *               2007-2011, Maxim Makhinya  <maxmah@gmail.com>
 *               2012,      David Steiner   <steiner@ifi.uzh.ch>
 */

#include "config.h"
#include <msv/util/str.h>


namespace massVolVis
{

Config::Config( eq::ServerPtr parent )
        : eq::Config( parent )
        , _spinX( 5 )
        , _spinY( 5 )
        , _forceUpdate( false )
        , _currentCanvas( 0 )
        , _messageTime( 0 )
{
}


Config::~Config()
{
}


bool Config::init( const eq::uint128_t& )
{
    if( !_animation.isValid( ))
        _animation.loadAnimation( _initData.getPathFilename( ));

    // init distributed objects
    LBCHECK( registerObject( &_frameData  ));
    LBCHECK( registerObject( &_volumeInfo ));

    _initData.setFrameDataId(  _frameData.getID(  ));
    _initData.setVolumeInfoId( _volumeInfo.getID( ));

    _frameData.setAutoObsolete(  getLatency( ));
    _volumeInfo.setAutoObsolete( getLatency( ));

    // setup volume info parameters from command line

    const std::string& modelFileName = _initData.getFilename();
    if( modelFileName.length() > 0 )
        _volumeInfo.setModelFileName( modelFileName );

    const RendererType rType = _initData.getRendererType();
    _volumeInfo.setRendererType( rType );

    _volumeInfo.commit();


    LBCHECK( registerObject( &_initData ));

    // init config
    if( !eq::Config::init( _initData.getID( )))
    {
        _deregisterData();
        return false;
    }

    const eq::Canvases& canvases = getCanvases();
    if( canvases.empty( ))
        _currentCanvas = 0;
    else
        _currentCanvas = canvases.front();

    _setMessage( "Welcome to volVis\nPress F1 for help" );

    return true;
}


bool Config::mapData( const eq::uint128_t& initDataId )
{
    if( !_initData.isAttached() )
    {
        if( !mapObject( &_initData, co::UUID( initDataId )))
            return false;
        unmapObject( &_initData ); // data was retrieved, unmap immediately
    }
    else  // appNode, _initData is registered already
    {
        LBASSERT( _initData.getID() == initDataId );
    }
    return true;
}


bool Config::exit()
{
    const bool ret = eq::Config::exit();
    _deregisterData();

    return ret;
}


void Config::_deregisterData()
{
    _volumeInfo.sync();

    deregisterObject( &_initData   );
    deregisterObject( &_volumeInfo );
    deregisterObject( &_frameData  );

    _initData.setFrameDataId( lunchbox::UUID( ));
}


uint32_t Config::startFrame( const eq::uint128_t& )
{
    // update camera
    if( _animation.isValid( ))
    {
        const eq::Vector3f&  modelRotation = _animation.getModelRotation();
        const CameraAnimation::Step& curStep = _animation.getNextStep();

        _frameData.setModelRotation( modelRotation );
        _frameData.setCameraRotation( curStep.rotation );
        _frameData.setCameraPosition( curStep.position );
    }
    else
    {
        if( _spinX != 0 || _spinY != 0 || _forceUpdate )
        {
            if( _frameData.usePilotMode())
                _frameData.spinCamera( -0.001f * _spinX, -0.001f * _spinY );
            else
                _frameData.spinModel(  -0.001f * _spinX, -0.001f * _spinY, 0.f );
        }

    }

    const lunchbox::uint128_t& version = _frameData.commit();

    _resetMessage();

    return eq::Config::startFrame( version );
}


void Config::_resetMessage()
{
    // reset message after two seconds
    if( _messageTime > 0 && getTime() - _messageTime > 2000 )
    {
        _messageTime = 0;
        _frameData.setMessage( "" );
    }
}


bool Config::handleEvent( const eq::ConfigEvent* event )
{
    switch( event->data.type )
    {
        case eq::Event::KEY_PRESS:
        {
            if( _handleKeyEvent( event->data.keyPress ))
            {
                return true;
            }
            break;
        }

        case eq::Event::CHANNEL_POINTER_BUTTON_PRESS:
        {
            const eq::uint128_t& viewID = event->data.context.view.identifier;
            _frameData.setCurrentViewId( viewID );
            if( viewID == 0 )
            {
                _currentCanvas = 0;
                return false;
            }

            const eq::View* view = find< eq::View >( viewID );
            const eq::Layout* layout = view->getLayout();
            const eq::Canvases& canvases = getCanvases();
            for( eq::CanvasesCIter i = canvases.begin();
                 i != canvases.end(); ++i )
            {
                eq::Canvas* canvas = *i;
                const eq::Layout* canvasLayout = canvas->getActiveLayout();

                if( canvasLayout == layout )
                {
                    _currentCanvas = canvas;
                    return true;
                }
            }
            return true;
        }

        case eq::Event::CHANNEL_POINTER_BUTTON_RELEASE:
        {
            const eq::PointerEvent& releaseEvent =
                event->data.pointerButtonRelease;
            if( releaseEvent.buttons == eq::PTR_BUTTON_NONE)
            {
                if( releaseEvent.button == eq::PTR_BUTTON1 )
                {
                    _spinX = releaseEvent.dy;
                    _spinY = releaseEvent.dx;
                    return true;
                }
                if( releaseEvent.button == eq::PTR_BUTTON2 )
                {
                    _advance = -releaseEvent.dy;
                    return true;
                }
            }
            break;
        }
        
        case eq::Event::CHANNEL_POINTER_MOTION:
        {
            switch( event->data.pointerMotion.buttons )
            {
              case eq::PTR_BUTTON1:
                  _spinX = 0;
                  _spinY = 0;

                  if( _frameData.usePilotMode())
                      _frameData.spinCamera(
                          -0.005f * event->data.pointerMotion.dy,
                          -0.005f * event->data.pointerMotion.dx );
                  else
                      _frameData.spinModel(
                          -0.005f * event->data.pointerMotion.dy,
                          -0.005f * event->data.pointerMotion.dx, 0.f );
                  return true;

              case eq::PTR_BUTTON2:
                  _advance = -event->data.pointerMotion.dy;
                  _frameData.moveCamera( 0.f, 0.f, .005f * _advance );
                  return true;

              case eq::PTR_BUTTON3:
                  _frameData.moveCamera(  .0005f * event->data.pointerMotion.dx,
                                         -.0005f * event->data.pointerMotion.dy,
                                          0.f );
                  return true;
            }
            break;
        }

        case eq::Event::CHANNEL_POINTER_WHEEL:
        {
            _frameData.moveCamera( -0.05f * event->data.pointerWheel.yAxis,
                                   0.f,
                                   0.05f * event->data.pointerWheel.xAxis );
            return true;
        }

        case eq::Event::MAGELLAN_AXIS:
        {
            _spinX = 0;
            _spinY = 0;
            _advance = 0;
            _frameData.spinModel( 0.0001f * event->data.magellan.xRotation,
                                  0.0001f * event->data.magellan.yRotation,
                                  0.0001f * event->data.magellan.zRotation );
            _frameData.moveCamera( 0.0001f * event->data.magellan.xAxis,
                                   0.0001f * event->data.magellan.yAxis,
                                   0.0001f * event->data.magellan.zAxis );
            return true;
        }

        case eq::Event::MAGELLAN_BUTTON:
        {
            if( event->data.magellan.button == eq::PTR_BUTTON1 )
                _frameData.toggleColorMode();

            return true;
        }

        default:
            break;
    }
    return eq::Config::handleEvent( event );
}


bool Config::_handleKeyEvent( const eq::KeyEvent& event )
{
    if( event.key >= '1' && event.key <= '9' )
    {
        _frameData.setMaxTreeDepth( 1 + event.key-'1' );
        _setMessage( "Max tree depth: " + strUtil::toString( int(_frameData.getMaxTreeDepth())) );
        return true;
    }
    switch( event.key )
    {
        case '+':
        case '=':
            _frameData.setMaxTreeDepth( _frameData.getMaxTreeDepth()+1 );
            _setMessage( "Max tree depth: " + strUtil::toString( int(_frameData.getMaxTreeDepth())) );
            return true;

        case '-':
        case '_':
            _frameData.setMaxTreeDepth( _frameData.getMaxTreeDepth()-1 );
            _setMessage( "Max tree depth: " + strUtil::toString( int(_frameData.getMaxTreeDepth())) );
            return true;

        case '[':
        case '{':
            _frameData.decreaseError();
            _setMessage( "Max error: " + (_frameData.useRenderingError() ? strUtil::toString( int(_frameData.getRenderingError())) : "off"));
            return true;

        case ']':
        case '}':
            _frameData.increaseError();
            _setMessage( "Max error: " + (_frameData.useRenderingError() ? strUtil::toString( int(_frameData.getRenderingError())) : "off"));
            return true;

        case eq::KC_UP:
            _frameData.increaseBudget();
            _setMessage( "Budget: " + strUtil::toString( _frameData.getRenderingBudget()) );
            return true;

        case eq::KC_DOWN:
            _frameData.decreaseBudget();
            _setMessage( "Budget: " + strUtil::toString( _frameData.getRenderingBudget()) );
            return true;

        case 'b':
        case 'B':
            _frameData.toggleBackground();
            return true;

        case 'd':
        case 'D':
            _frameData.toggleColorMode();
            return true;

        case eq::KC_F1:
        case 'h':
        case 'H':
            _frameData.toggleHelp();
            return true;

        case 'w':
        case 'W':
        _frameData.toggleBoundingBoxesDisplay();
            return true;

        case 'r':
        case 'R':
            _frameData.toggleRecording();
            return true;
        case ' ':
            _spinX = 0;
            _spinY = 0;
            _forceUpdate = true;
            _frameData.reset();
            return true;

        case 's':
        case 'S':
        _frameData.toggleStatistics();
            return true;

        case 'l':
            _switchLayout( 1 );
            return true;
        case 'L':
            _switchLayout( -1 );
            return true;

        case 'm':
        case 'M':
            _frameData.togglePilotMode();
            return true;

        case 'n':
        case 'N':
            _frameData.toggleNormalsQuality();
            return true;

        case 'p':
        case 'P':
            _frameData.makeScreenshot();
            return true;

        case 'q':
            _frameData.adjustQuality( -.1f );
            return true;

        case 'Q':
            _frameData.adjustQuality( .1f );
            return true;

        case 'c':
        case 'C':
        {
            const eq::Canvases& canvases = getCanvases();
            if( canvases.empty( ))
                return true;

            _frameData.setCurrentViewId( lunchbox::UUID::ZERO );

            if( !_currentCanvas )
            {
                _currentCanvas = canvases.front();
                return true;
            }

            eq::Canvases::const_iterator i = std::find( canvases.begin(),
                                                        canvases.end(),
                                                        _currentCanvas );
            LBASSERT( i != canvases.end( ));

            ++i;
            if( i == canvases.end( ))
                _currentCanvas = canvases.front();
            else
                _currentCanvas = *i;
            return true;
        }

        case 'v':
        case 'V':
        {
            const eq::Canvases& canvases = getCanvases();
            if( !_currentCanvas && !canvases.empty( ))
                _currentCanvas = canvases.front();

            if( !_currentCanvas )
                return true;

            const eq::Layout* layout = _currentCanvas->getActiveLayout();
            if( !layout )
                return true;

            const eq::View* current =
                              find< eq::View >( _frameData.getCurrentViewId( ));

            const eq::Views& views = layout->getViews();
            LBASSERT( !views.empty( ))

            if( !current )
            {
                _frameData.setCurrentViewId( views.front()->getID( ));
                return true;
            }

            eq::Views::const_iterator i = std::find( views.begin(),
                                                          views.end(),
                                                          current );
            LBASSERT( i != views.end( ));

            ++i;
            if( i == views.end( ))
                _frameData.setCurrentViewId( lunchbox::UUID::ZERO );
            else
                _frameData.setCurrentViewId( (*i)->getID( ));
            return true;
        }

        default:
            break;
    }
    return false;
}

void Config::_setMessage( const std::string& message )
{
    _frameData.setMessage( message );
    _messageTime = getTime();
}

void Config::_switchLayout( int32_t increment )
{
    if( !_currentCanvas )
        return;

    _frameData.setCurrentViewId( lunchbox::UUID::ZERO );

    size_t index = _currentCanvas->getActiveLayoutIndex() + increment;
    const eq::Layouts& layouts = _currentCanvas->getLayouts();
    LBASSERT( !layouts.empty( ));

    index = ( index % layouts.size( ));
    _currentCanvas->useLayout( uint32_t( index ));

    const eq::Layout* layout = _currentCanvas->getActiveLayout();
    std::ostringstream stream;
    stream << "Layout ";
    if( layout )
    {
        const std::string& name = layout->getName();
        if( name.empty( ))
            stream << index;
        else
            stream << name;
    }
    else
        stream << "NONE";

    stream << " active";
    _setMessage( stream.str( ));
}

}//namespace massVolVis
