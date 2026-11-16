#pragma once
#include <string>
extern "C" {
#include "quickjs.h"
}
namespace visual {
// State and pending bytes are private JS ArrayBuffers, accounted by the realm heap.
JSValue text_codec_operation(JSContext *, const std::string &, int, JSValueConst *);
} // namespace visual
