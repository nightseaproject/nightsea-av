#include "Parameters.h"
#include "cinder/app/App.h"
#include "cinder/Log.h"

using namespace ci;

Parameters::Parameters( const fs::path &path ) : mPath( path ), mJson( app::loadAsset( path ) )
{
  init();
}

Parameters::~Parameters()
{
}

void Parameters::init()
{
  // SCALAR PARAMS
  JsonTree params = mJson.getChild( "params" );
  mParameters.clear();
  for ( auto it = params.begin(); it != params.end(); it++ ) {
    auto param = std::make_shared<Parameter>();
    param->name = (*it)["name"].getValue();
    param->baseValue = (*it)["value"].getValue<float>();
    param->currentValue = (*it)["value"].getValue<float>();

    // Defaults to 0 if min not specified
    try {
      param->min = (*it)["min"].getValue<float>();
    }
    catch ( const JsonTree::ExcChildNotFound &e ) {
      param->min = 0;
    }
    
    // Defaults to 1 if max not specified
    try {
      param->max = (*it)["max"].getValue<float>();
    }
    catch ( const JsonTree::ExcChildNotFound &e ) {
      param->max = 1;
    }

    // MIDI
    try {
      param->midiNumber = (*it)["midi"].getValue<int>();
    }
    catch ( const std::exception &e ) {
      // cinder::app::console() << "No midi for param " << param->name << std::endl;
    }

    // OSC
    try {
      param->oscChannel = (*it)["osc"].getValue<int>();
      cinder::app::console() << "Found channel " << param->oscChannel << " for param " << param->name << std::endl;
    } catch ( const JsonTree::ExcChildNotFound &e ) {      
    }

    // MODULATORS
    try {
      JsonTree modulatorTree = (*it).getChild( "modulator" );
      float frequency = modulatorTree["frequency"].getValue<float>();
      float amount = modulatorTree["amount"].getValue<float>();
      std::string type = modulatorTree["type"].getValue();
      ModulatorType modType = Modulator::stringToType( type );
      param->modulator = std::make_unique<Modulator>( modType, frequency, amount );;
      // ci::app::console() << "type: " << type << " freq: " << frequency << " amount: " << amount << std::endl;
    }
    catch ( const std::exception &e ) {
      // ci::app::console() << "NO MODULATOR" << std::endl;
      param->modulator = nullptr;
    }

    // ANIMATIONS
    try {
      JsonTree animations = (*it).getChild( "animations" );      
      for ( auto it = animations.begin(); it != animations.end(); it++ ) {
        auto animation = std::make_shared<Animation>();
        animation->mTargetValue = (*it)["target"].getValue<float>();
        animation->mDuration = (*it)["duration"].getValue<float>();    

        try {
          animation->mMidiMapping = (*it)["midi"].getValue<int>();
        }
        catch ( const std::exception &e ) {
          animation->mMidiMapping = -1;
        }

        try {
          animation->mCurve= (*it)["curve"].getValue();
        }
        catch ( const std::exception &e ) {
          animation->mCurve = "linear";
        }

        param->animations.push_back( animation );
      }
    }
    catch ( const std::exception &e ) {
      // ci::app::console() << "NO ANIMATIONS" << std::endl;
      param->animations.clear();      
    }    

    mParameters.push_back( param );
  }

  // COLOR PARAMS
  JsonTree colorParams = mJson.getChild( "colorParams" );
  mColorParameters.clear();
  for ( auto it = colorParams.begin(); it != colorParams.end(); it++ ) {
    auto colorParam = std::make_shared<ColorParameter>();
    colorParam->name = (*it)["name"].getValue();
    float r = (*it)["r"].getValue<float>();
    float g = (*it)["g"].getValue<float>();
    float b = (*it)["b"].getValue<float>();
    colorParam->value = Colorf( r, g, b );
    mColorParameters.push_back( colorParam );
  }

}

void Parameters::save()
{
  updateJsonTree( mJson );
  mJson.write( app::getAssetPath( mPath ) );
}

void Parameters::writeTo( const ci::fs::path &path )
{
  auto newJson = JsonTree( mJson );
  updateJsonTree( newJson );
  newJson.write( path );  
}

void Parameters::updateJsonTree( ci::JsonTree &oldTree )
{
  JsonTree params = oldTree.getChild( "params" );
  for ( size_t i = 0; i < mParameters.size(); i++ ) {
    auto param = mParameters[ i ];
    auto tree = params.getChild( i );
    tree.addChild( JsonTree( "value", param->hasModulator() ? param->baseValue : param->currentValue ) );
    if ( param->hasModulator() ) {      
      tree.addChild( JsonTree::makeObject( "modulator" ) );
      auto modTree = tree.getChild( "modulator" );      
      modTree.addChild( JsonTree( "type", Modulator::typeToString( param->modulator->mType ) ) );
      modTree.addChild( JsonTree( "frequency", param->modulator->mFrequency ) );
      modTree.addChild( JsonTree( "amount", param->modulator->mAmount ) );
      tree.addChild( modTree );      
    }
    else {
      if ( tree.hasChild( "modulator" ) ) {
        tree.addChild( JsonTree::makeObject( "modulator" ) );
      }
    }
    params.replaceChild( i, tree );
  }
  oldTree.getChild( "params" ) = params;

  JsonTree colorParams = oldTree.getChild( "colorParams" );
  for ( size_t i = 0; i < mColorParameters.size(); i++ ) {
    auto colorParam = mColorParameters[ i ];
    auto tree = colorParams.getChild( i );
    tree.addChild( JsonTree( "r", colorParam->value.r ) );
    tree.addChild( JsonTree( "g", colorParam->value.g ) );
    tree.addChild( JsonTree( "b", colorParam->value.b ) );
    colorParams.replaceChild( i, tree );
  }
  oldTree.getChild( "colorParams" ) = colorParams;
}

void Parameters::load( const ci::fs::path &path )
{
  try {
    mJson = JsonTree( ci::loadFile( path ) );
    init();
  }
  catch( Exception &exc ) {  
    CI_LOG_EXCEPTION( "Failed to load parameters: " << path, exc );
  }
}

void Parameters::reload()
{
  mJson = JsonTree( app::loadAsset( mPath ) );
  init();
}

std::shared_ptr<Parameter> Parameters::getParameterForMidiNumber( int number )
{
  for ( size_t i = 0; i < mParameters.size(); i++ ) {
    auto param = mParameters[ i ];
    if ( param->midiNumber == number ) {
      return param;
    }
  }
  return nullptr;
}

std::vector<std::shared_ptr<Parameter>> Parameters::getParametersForOSCChannel( int channel )
{
  std::vector<std::shared_ptr<Parameter>> parameters;
  for ( size_t i = 0; i < mParameters.size(); i++ ) {
    auto param = mParameters[ i ];
    if ( param->oscChannel == channel ) {
      parameters.push_back( param );
    }
  }
  return parameters;
}

std::vector<std::shared_ptr<Animation>> Parameters::getAnimationsForMidiNumber( int number )
{
  std::vector<std::shared_ptr<Animation>> animations;
  for ( size_t i = 0; i < mParameters.size(); i++ ) {
    auto param = mParameters[ i ];
    for ( size_t i = 0; i < param->animations.size(); i++ ) {
      auto anim = param->animations[ i ];
      if ( anim->mMidiMapping == number ) {
        animations.push_back( anim );
      }
    }
  }
  return animations;
}