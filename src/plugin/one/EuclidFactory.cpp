// Plugin entry point for Monomodule Euclid: Six with the generative Euclidean sequencer.
#include "OneProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new mnm::plugin::one::MnmOneProcessor(mnm::plugin::one::Variant::Euclid); }
