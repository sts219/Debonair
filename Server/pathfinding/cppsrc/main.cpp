#include <napi.h>
#include "pathfinding.h"

Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
    return pathfinding::Init(env, exports);
}

NODE_API_MODULE(testaddon, InitAll)