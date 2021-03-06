// Cinder
#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinder/gl/gl.h"
#include "cinder/Log.h"
#include "cinder/Timer.h"
#include "cinder/audio/Voice.h"
#include "cinder/CinderMath.h"
#include "cinder/qtime/AvfWriter.h"
#include "cinder/FileWatcher.h"
#include "cinder/Capture.h"

// Blocks
#include "Osc.h"
#include "CinderImGui.h"
#include "MidiIn.h"
#include "MidiMessage.h"
#include "MidiConstants.h"
#include "cinderSyphon.h"

// C++
#include <ctime>
#include <boost/filesystem.hpp>

// Couleurs
#include "Parameters.h"
#include "Performance.h"
#include "Patch.h"
#include "Constants.h"
#include "MultipassShader.h"
#include "Utils.h"

using namespace ci;
using namespace ci::app;
using namespace std;

class CouleursApp : public App {
public:
  CouleursApp();
  void setup() override;
  void update() override;
  void keyDown( KeyEvent event ) override;
  void mouseMove( MouseEvent event ) override;
  void fileDrop( FileDropEvent event ) override;
  static vector<string>& getArgs() { static vector<string> args; return args; }
  
private:
  void initShaderWatching();
  
  // Setup
  void setupUI();
  void setupScene();
  void setupMidi();
  void setupOSC();
  void loadCurrentPatch();  
  
  // Update
  void updateOSC();
  void updateUI();
  void updateShaders();
  void exportGIFFrames();
  void updateTimer();
  void updateParams();
  void updateCamera();
  
  void drawUI();
  void drawScene();
  void bindUniforms( gl::GlslProgRef shader );  
  
  void resizeScene();
  
  void clearFBO( gl::FboRef fbo );

  void exportFrame( string suffix, bool exportParams );
  void saveParams();
  void resetParams();
  
  void abletonMidiListener( midi::Message msg );
  void controllerMidiListener( midi::Message msg );
  
  osc::ReceiverUdp             mOSCIn;
  midi::Input                  mAbletonMidiIn, mControllerMidiIn;
  
  Performance                  mPerformance;
  Patch&                       currentPatch() { return mPerformance.currentPatch(); };
  Parameters&                  currentParams() { return currentPatch().params(); };
      
  bool                         mSceneIsSetup = false;
  bool                         mHeadlessMode = false;
  bool                         mSaveHeadlessScreenshot = false;
  bool                         mLoopExportMode = false;
  
  // Time
  float                        mTime = 0;
  bool                         mTimeStopped = false;
  
  // Mouse
  ivec2                        mMousePosition;

  // Camera
  CaptureRef                   mCapture;
  gl::Texture2dRef             mCaptureTex;

  // AV Sync
  ci::Timer                    mTimer;
  int                          mBPM = 100, mSection = 0, mNumSections;
  float                        mTick; //[0 - 1]      

  MultipassShader              mMultipassShader;
  
  // Window Management
  ci::app::WindowRef           mUIWindow, mSceneWindow;

  // Syphon
  syphonServer                 mScreenSyphon;
  syphonClient                 mClientSyphon;
  ci::gl::FboRef               mSyphonFBO;
};

CouleursApp::CouleursApp() : mPerformance( { PATCH_NAME } ), mOSCIn( OSC_PORT ) 
{    
  // Window Management
  mUIWindow = getWindow();
  mUIWindow->setTitle( "Couleurs: Parameters" );
  mUIWindow->getSignalDraw().connect( bind( &CouleursApp::drawUI, this ) );
  mUIWindow->setPos( 0, WINDOW_PADDING );
  mUIWindow->setSize( UI_WIDTH, UI_HEIGHT );
  console() << "UI Window content scale: " << mUIWindow->getContentScale() << endl;
    
  mSceneWindow = createWindow( Window::Format().size( SCENE_WIDTH, SCENE_HEIGHT ) );
  mSceneWindow->setTitle( "Couleurs: Render" );
  mSceneWindow->getSignalDraw().connect( bind( &CouleursApp::drawScene, this ) );
  mSceneWindow->getSignalResize().connect( bind( &CouleursApp::resizeScene, this ) );
  mSceneWindow->setPos( 0, UI_HEIGHT + 2 * WINDOW_PADDING );
  console() << "Scene Window content scale: " << mSceneWindow->getContentScale() << endl;  

  // Midi
  setupMidi();
}

void CouleursApp::setup() 
{
  // Read command-line arguments
  for( vector<string>::const_iterator argIt = getArgs().begin(); argIt != getArgs().end(); ++argIt ) {
		if ( *argIt == "headless" ) {
      mHeadlessMode = true;
    };

    if ( *argIt == "loop_export" ) {
      mLoopExportMode = true;
    };
  }

  setupUI();
  setupScene();
  mTimer.start();

  // Syphon
  mScreenSyphon.setName( "Couleurs" );  
  mClientSyphon.setServerName( "Processing Syphon" );	
  mSyphonFBO = gl::Fbo::create( toPixels( mSceneWindow->getWidth() ), toPixels( mSceneWindow->getHeight() ) );

  // Camera
  mCapture = Capture::create( toPixels( mSceneWindow->getWidth() ), toPixels( mSceneWindow->getHeight() ) );
  mCapture->start();

  // OSC
  setupOSC();
}

void CouleursApp::setupUI() 
{
  mUIWindow->getRenderer()->makeCurrentContext();
  
  // UI  
  ui::initialize( 
    ui::Options()
    .window( mUIWindow )
    .frameRounding( 0.0f )
    .darkTheme()    
  );
}

void CouleursApp::setupScene() 
{
  mSceneWindow->getRenderer()->makeCurrentContext();
  
  // Shaders
  initShaderWatching();
  auto width = mHeadlessMode ? HEADLESS_WIDTH : toPixels( mSceneWindow->getWidth() );
  auto height = mHeadlessMode ? HEADLESS_HEIGHT : toPixels( mSceneWindow->getHeight() );
  mMultipassShader.init( width, 
                         height,
                         [this] ( gl::GlslProgRef shader ) { bindUniforms( shader ); },
                         mLoopExportMode );  
  loadCurrentPatch();
  
  // GL State
  gl::disableDepthRead();
  gl::disableDepthWrite();
  gl::disableBlending();  
  
  mSceneIsSetup = true;
}

void CouleursApp::setupOSC()
{
  int numOSCChannels = 8;
  for ( size_t i = 0; i < numOSCChannels; i++ ) {
    mOSCIn.setListener( "/jo_ann/" + std::to_string( i ),
    [&, i]( const osc::Message &msg ){
      float value = msg[0].flt();
      console() << "OSC Value is: " << value << endl;
      auto params = currentParams().getParametersForOSCChannel( i );      
      for ( size_t j = 0; j < params.size(); j++ ) {        
        auto param = params[j];
        console() << "Updating param: " << param->name << endl;
        param->currentValue = lerp( param->min, param->max, value );  
      }
    });    
  }  

  try {
		// Bind the receiver to the endpoint. This function may throw.
		mOSCIn.bind();
	}
	catch( const osc::Exception &ex ) {
		CI_LOG_E( "Error binding: " << ex.what() << " val: " << ex.value() );		
	}

  mOSCIn.listen(
	[]( asio::error_code error, asio::ip::udp::endpoint endpoint ) -> bool {
		if( error ) {
			CI_LOG_E( "Error Listening: " << error.message() << " val: " << error.value() << " endpoint: " << endpoint );
			return false;
		}
		else
			return true;
	});
}

void CouleursApp::setupMidi()
{
  if ( mAbletonMidiIn.getNumPorts() > 0 ) {    
    mAbletonMidiIn.openPort( 0 );
    cout << "Opening MIDI port 0" << endl;
    mAbletonMidiIn.midiSignal.connect( bind( &CouleursApp::abletonMidiListener, this, placeholders::_1 ) );
  }
  else {
    cout << "No MIDI ports found" << endl;
  }
    
  if ( mControllerMidiIn.getNumPorts() > MIDI_CONTROLLER_PORT ) {
    mControllerMidiIn.openPort( MIDI_CONTROLLER_PORT );
    cout << "Opening MIDI port " << MIDI_CONTROLLER_PORT << endl;
    mControllerMidiIn.midiSignal.connect( bind( &CouleursApp::controllerMidiListener, this, placeholders::_1 ) );
  }
  else {
    cout << "No MIDI ports found" << endl;
  }
}

void CouleursApp::controllerMidiListener( midi::Message msg )
{
  auto param = currentParams().getParameterForMidiNumber( msg.control );
  if ( param != nullptr ) {
    console() << "found param: " << param->name << endl;
    param->currentValue = lmap( (float)msg.value, 0.f, 127.f, param->min, param->max );
  }
  console() << "msg value: " << msg.value << " || control: " << msg.control << " || channel: " << msg.channel << endl;
}

void CouleursApp::abletonMidiListener( midi::Message msg )
{
  switch ( msg.status ) {
    case MIDI_START:
      console() << "MIDI START" << endl;
      mTimer.stop();
      mTimer.start();
      break;
    case MIDI_STOP:
      console() << "MIDI STOP" << endl;
      mTimer.stop();
      mTimer.start();
      break;
    case MIDI_TIME_CLOCK:
      break;
  }
}

void CouleursApp::resizeScene() 
{
  auto w = mHeadlessMode ? HEADLESS_WIDTH : toPixels( mSceneWindow->getWidth() );
  auto h = mHeadlessMode ? HEADLESS_HEIGHT : toPixels( mSceneWindow->getHeight() );
  mMultipassShader.resize( w, h );
}

void CouleursApp::initShaderWatching() 
{
  vector<fs::path> shaderPaths;
  auto patchPath = currentPatch().path();
  for ( auto &p: boost::filesystem::directory_iterator( getAssetPath( patchPath ) ) ) {
    auto extension = p.path().extension();
    if ( extension == ".frag" || extension == ".glsl" ) {
      console() << p.path().filename() << endl;
      auto assetPath = patchPath / p.path().filename();
      shaderPaths.push_back( getAssetPath( assetPath ) );
    }
  }  

  FileWatcher::instance().watch( shaderPaths, [this]( const WatchEvent &event ) {
    console() << "Shader needs reload" << std::endl;      
    mMultipassShader.reload();    
 	} );
}

void CouleursApp::loadCurrentPatch()
{
  mMultipassShader.load( currentPatch().path() );
}

void CouleursApp::fileDrop( FileDropEvent event )
{
  auto path = event.getFile( 0 );
  currentParams().load( path );
}

void CouleursApp::mouseMove( MouseEvent event ) 
{
  mMousePosition = toPixels( glm::clamp( event.getPos(), ivec2( 0., 0. ), mSceneWindow->getSize() ) );
}

void CouleursApp::keyDown( KeyEvent event ) 
{
  if ( event.getCode() == KeyEvent::KEY_s ) {
    saveParams();
  }
  else if ( event.getCode() == KeyEvent::KEY_f ) {
    exportFrame( to_string( getElapsedSeconds() ), true );
  }
  else if ( event.getCode() == KeyEvent::KEY_r ) {
    resetParams();
  }
  else if ( event.getCode() == KeyEvent::KEY_t ) {
    mTimeStopped = !mTimeStopped;
  }
  else if ( event.getCode() == KeyEvent::KEY_RIGHTBRACKET ) {
    mTime += .1f;
  }
  else if ( event.getCode() == KeyEvent::KEY_LEFTBRACKET ) {
    mTime -= .1f;
  }
  else if ( event.getCode() == KeyEvent::KEY_SPACE ) {    
    auto anims = currentParams().getAnimationsForMidiNumber( -1 );
    for ( size_t i = 0; i < anims.size(); i++ ) {      
      anims[i]->trigger();
    }
  }
  else if ( event.getCode() == KeyEvent::KEY_p ) {
    if (mPerformance.previous()) {
      dispatchAsync( [this] {
        loadCurrentPatch();
		  });
    }
  }
  else if ( event.getCode() == KeyEvent::KEY_n ) {
    if (mPerformance.next()) {
      dispatchAsync( [this] {
        loadCurrentPatch();
		  });
    }    
  }
}

void CouleursApp::exportFrame( string suffix, bool exportParams )
{
  CI_LOG_I( "Saving screenshot" );
  const char *homeDir = getenv( "HOME" );
  auto path = string( homeDir ) + string( "/Desktop/screenshot_" ) + currentPatch().name() + string("_") + suffix;
  auto surface = Surface8u( mMultipassShader.mMainFbo->getColorTexture()->createSource() );    
  writeImage( path + string( ".png" ), surface );

  if ( exportParams ) {
    currentParams().writeTo( path + string( ".json" ) );
  }
}

void CouleursApp::resetParams()
{
  CI_LOG_I( "Resetting params" );
  currentParams().reload();
}

void CouleursApp::saveParams()
{
  CI_LOG_I( "Saving config file" );
  currentParams().save();
}

void CouleursApp::update()
{
  // updateOSC();
  updateUI();
  updateTimer();
  updateParams();
  updateCamera();
}

// void CouleursApp::updateOSC()
// {
//   while ( mOSCIn.hasWaitingMessages() ) {
//     osc::Message message;
//     mOSCIn.getNextMessage( &message );
//     string address = message.getAddress();
//     float value = message.getArgAsFloat( 0 );
//     console() << "OSC address: " << address << " -- value: " << value << std::endl;   
//   }
// }

void CouleursApp::updateUI()
{
  // Draw UI
  {
    ui::ScopedMainMenuBar mainMenu;    
    
    if ( ui::BeginMenu( "Couleurs" ) ) {   
      if ( ui::MenuItem( "Save" ) ) {
        saveParams();
      }   
      if ( ui::MenuItem( "Export Window Res")) {
        exportFrame( to_string( getElapsedSeconds() ), true );
      }
      if ( ui::MenuItem( "Export Headless Res")) {
        if (mHeadlessMode) {
          mSaveHeadlessScreenshot = true;
        }        
      }
      if ( ui::MenuItem( "Reset" ) ) {
        resetParams();
      }
      if ( ui::MenuItem( "QUIT" ) ) {
        quit();
      }
      ui::EndMenu();
    }
  }
  
  {
    ui::ScopedWindow win( "Parameters" );
    auto params = currentParams().get();
    int id = 0;
    for (auto it = params.begin(); it != params.end(); it++ ) {      
      auto param = *it;
      ui::ScopedId scopedId( id );
      ui::SliderFloat( param->name.c_str(), &param->currentValue, param->min, param->max, "%.3f" );
      ui::SameLine();
 
      if ( ui::Button( "Mod" ) ) {
        if ( !param->hasModulator() ) {
          param->createModulator();
        } else {
          param->deleteModulator();          
        }
      }      
      
      if ( param->hasModulator() ) {
        ui::ScopedItemWidth scopedWidth( ImGui::GetWindowWidth() * .2f );
        ui::ListBoxHeader( "Waveform", vec2( 0, ui::GetTextLineHeightWithSpacing() * 4 ) );
			  if ( ui::Selectable( "Sine", param->modulator->mType == SINE ) ) {
          param->modulator->mType = SINE;
        }
			  if ( ui::Selectable( "Random", param->modulator->mType == RANDOM ) ) {
          param->modulator->mType = RANDOM;
        }
        if ( ui::Selectable( "Triangle", param->modulator->mType == TRIANGLE ) ) {
          param->modulator->mType = TRIANGLE;
        }
			  if ( ui::Selectable( "Noise", param->modulator->mType == NOISE ) ) {
          param->modulator->mType = NOISE;
        }
			  ui::ListBoxFooter();
        ui::SameLine();
        ui::SliderFloat( "Frequency", &param->modulator->mFrequency, 0, 10, "%.2f" );
        ui::SameLine();
        ui::SliderFloat( "Amount", &param->modulator->mAmount, 0, param->max / 2.f, "%.2f" );        
      }
      id++;
    }

    auto colorParams = currentParams().getColors();
    for (auto it = colorParams.begin(); it != colorParams.end(); it++ ) {
      auto colorParam = *it;
      ui::ColorEdit3( colorParam->name.c_str(), &( colorParam->value.r ) );
    }    
  }
  
  {
    ui::ScopedWindow win( "Patches" );
    for ( int i = 0; i < mPerformance.numPatches(); i++ ) {
      auto patchName = mPerformance.patchNameAtIndex( i );
      if ( i == mPerformance.currentPatchIndex() ) {
        ui::TextColored( ImVec4( 0.914, 0.392, 0.588, 1.f ), patchName.c_str(), i );
      }
      else {
        ui::Text( patchName.c_str(), i );
      }
    }
  }

  {
    ui::ScopedWindow win( "Perf" );
    ui::Text( "FPS: %d", (int)getAverageFps() );
  }
  
  {
    ui::ScopedWindow win( "AV Sync" );
    ui::SliderInt( "Section", &mSection, 0, mNumSections - 1 );
    ui::SliderInt( "BPM", &mBPM, 20, 200 );
    auto draw = ui::GetWindowDrawList();
    vec2 p = (vec2)ui::GetCursorScreenPos() + vec2( 0.f, 3.f );
    vec2 size( ui::GetContentRegionAvailWidth() * .7f, ui::GetTextLineHeightWithSpacing() );
    auto c = ImColor( .85f, .87f, .92f, .76f );
    draw->AddRectFilled( p, vec2( p.x + size.x * mTick, p.y + size.y ), c );
  }
  
  {
    if ( mMultipassShader.mShaderCompilationFailed ) {
      ui::ScopedStyleColor color( ImGuiCol_TitleBgActive, ImVec4( .9f, .1f, .1f, .85f ) );
      ui::ScopedWindow win( "Debug" );      
      ui::Text( "%s", mMultipassShader.mShaderCompileErrorMessage.c_str() );
      console() << "Shader exception: " << mMultipassShader.mShaderCompileErrorMessage << std::endl;      
    }
  }
}

void CouleursApp::updateTimer()
{
  double t = mTimer.getSeconds();
  float bps = mBPM / 60.f;
  float beatLengthSeconds = 1.f / bps;
  mTick = ( fmod( t, beatLengthSeconds ) ) / beatLengthSeconds;
}

void CouleursApp::updateParams()
{
  auto params = currentParams().get();
  for ( auto it = params.begin(); it != params.end(); it++ ) {
    (*it)->tick( getElapsedSeconds() );
  }
}

void CouleursApp::updateCamera()
{
  if ( mCapture && mCapture->checkNewFrame() ) {    
    auto surface = mCapture->getSurface();

    if ( !mCaptureTex ) {
      mCaptureTex = gl::Texture::create( *surface, gl::Texture::Format() );      
    }
    else {
      mCaptureTex->update( *surface );
    }
  }
}

void CouleursApp::exportGIFFrames()	
{	
  if ( !mLoopExportMode ) return;

  if ( getElapsedFrames() <= GIF_LENGTH ) {
    std::stringstream ss;
    ss << std::setw(3) << std::setfill('0') << getElapsedFrames();    
    exportFrame( ss.str(), false );
  }
  else {
    quit();
  }
 }

void CouleursApp::drawUI()
{
  gl::clear( ColorA( 0.f, 0.f, 0.05f, 1.f ) );
  gl::color( ColorAf::white() );
  gl::printError( "drawUI" );
}

void CouleursApp::drawScene()
{
  if ( !mSceneIsSetup ) return;

  // Draw Syphon texture
  mSyphonFBO->bindFramebuffer();
  gl::draw( mClientSyphon.getTexture(), mSceneWindow->getBounds() );
  mSyphonFBO->unbindFramebuffer();

  // Headless mode for high-resolution exports
  if ( mSaveHeadlessScreenshot ) {
    gl::setMatricesWindow( ivec2( HEADLESS_WIDTH, HEADLESS_HEIGHT ), true );
    gl::pushViewport( ivec2( HEADLESS_WIDTH, HEADLESS_HEIGHT ) );
    Rectf rect = Rectf( 0.f, 0.f, HEADLESS_WIDTH, HEADLESS_HEIGHT );
    mMultipassShader.draw( rect, mSyphonFBO->getColorTexture(), mCaptureTex );  
    exportFrame( to_string( getElapsedSeconds() ), true );
    quit();
  }
  
  // Draw patch  
  Rectf rect = Rectf( 0.f, 0.f, mSceneWindow->getWidth(), mSceneWindow->getHeight() );
  mMultipassShader.draw( rect, mSyphonFBO->getColorTexture(), mCaptureTex );
  mScreenSyphon.publishTexture( mMultipassShader.mMainFbo->getColorTexture(), false );

  // Draw red rect if error
  if ( mMultipassShader.mShaderCompilationFailed ) {
    gl::ScopedColor red( Color( 1.f, 0.f, 0.f ) );    
    float h = 20.f;
    gl::drawSolidRect( Rectf( 0.f, mSceneWindow->getHeight() - h, mSceneWindow->getWidth(), mSceneWindow->getHeight() ) );
  }  

  exportGIFFrames();

  gl::printError( "drawScene" );
}

void CouleursApp::bindUniforms( gl::GlslProgRef shader )
{
  // Common Uniforms
  vec2 resolution = toPixels( mSceneWindow->getSize() ); 
  shader->uniform( "u_resolution", resolution );
  if (!mTimeStopped) {
    mTime = (float)getElapsedSeconds();
  }
  shader->uniform( "u_frameNumber", (float)getElapsedFrames() );
  shader->uniform( "u_time", mTime );
  shader->uniform( "u_tick", mTick );
  shader->uniform( "u_section", mSection );
  shader->uniform( "u_mouse", vec2( mMousePosition.x, toPixels( mSceneWindow->getHeight() ) - mMousePosition.y ) );
  
  // Scalar Parameters
  auto params = currentParams().get();
  for ( auto it = params.begin(); it != params.end(); it++ ) {
      shader->uniform( (*it)->name, (*it)->currentValue );
  }

  // Color Parameters
  auto colorParams = currentParams().getColors();
  for ( auto it = colorParams.begin(); it != colorParams.end(); it++ ) {
      Colorf value = (*it)->value;
      shader->uniform( (*it)->name, vec3( value.r, value.g, value.b ) );
  }
}

void CouleursApp::clearFBO( gl::FboRef fbo ) 
{
  gl::ScopedFramebuffer scopedFramebuffer( fbo );
  gl::ScopedViewport scopedViewport( ivec2( 0 ), fbo->getSize() );
  gl::clear();
}

CINDER_APP( CouleursApp, RendererGl, [&]( App::Settings *settings ) 
{
   settings->setHighDensityDisplayEnabled();
   CouleursApp::getArgs() = settings->getCommandLineArgs();
})
