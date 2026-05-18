#pragma once

/// Umbrella for brick-based algorithm layer (see docs/design/streaming-compression-design.md).

#include "BitProcessor.hpp"
#include "ByteView.hpp"
#include "EncodingTriple.hpp"
#include "LZDP.hpp"
#include "LZencoding.hpp"
#include "Models.hpp"
#include "RecordIO.hpp"
#include "Streaming.hpp"
#include "pipeline/Phase1Dpforward.hpp"
#include "pipeline/LZDPNonStreaming.hpp"
#include "pipeline/LZDPStreamingPipeline.hpp"
