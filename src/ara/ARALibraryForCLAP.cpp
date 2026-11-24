// The ARA SDK's plug-in library, for the CLAP target only. JUCE builds the same four files into the VST3 / AU
// wrappers (juce_audio_plugin_client_ARA.cpp), but that file refuses to compile without a JUCE plugin format, and the
// CLAP wrapper (clap-juce-extensions) is not one. CLAP has no ARA; this only satisfies the shared code's references.
#include <juce_audio_processors/juce_audio_processors.h>

#if JucePlugin_Enable_ARA
JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wunused-parameter", "-Wgnu-zero-variadic-macro-arguments", "-Wmissing-prototypes",
                                     "-Wfloat-equal", "-Wc++20-extensions")
JUCE_BEGIN_IGNORE_WARNINGS_MSVC (4100)

#include <ARA_Library/PlugIn/ARAPlug.cpp>
#include <ARA_Library/Dispatch/ARAPlugInDispatch.cpp>
#include <ARA_Library/Utilities/ARAPitchInterpretation.cpp>
#include <ARA_Library/Utilities/ARAChannelFormat.cpp>

JUCE_END_IGNORE_WARNINGS_MSVC
JUCE_END_IGNORE_WARNINGS_GCC_LIKE
#endif
